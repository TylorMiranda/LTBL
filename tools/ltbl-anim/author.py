"""Animation authoring: mocap layers + keyed IK on the Skyrim skeleton."""
import numpy as np
from skel import Skeleton, qmul, qconj, qrot, qnorm, qslerp, qalign, qfrommat, render_frames
from retarget import Retargeter, BVH, C

FPS = 30.0


def smooth(x):
    x = np.clip(x, 0.0, 1.0)
    return x * x * (3 - 2 * x)


class Curve:
    """Scalar or vector keyframe curve. mode: 'ease' (zero tangents) or 'cr' (Catmull-Rom)."""

    def __init__(self, keys, mode="cr"):
        self.keys = sorted(keys, key=lambda k: k[0])
        self.t = np.array([k[0] for k in self.keys])
        self.v = np.array([np.atleast_1d(np.array(k[1], float)) for k in self.keys])
        self.mode = mode

    def __call__(self, t):
        t_, v = self.t, self.v
        if t <= t_[0]:
            out = v[0]
        elif t >= t_[-1]:
            out = v[-1]
        else:
            i = np.searchsorted(t_, t) - 1
            u = (t - t_[i]) / (t_[i + 1] - t_[i])
            if self.mode == "ease" or len(t_) < 3:
                out = v[i] + (v[i + 1] - v[i]) * smooth(u)
            elif self.mode == "linear":
                out = v[i] + (v[i + 1] - v[i]) * u
            else:
                p0 = v[i - 1] if i > 0 else v[i]
                p3 = v[i + 2] if i + 2 < len(v) else v[i + 1]
                h = t_[i + 1] - t_[i]
                m1 = (v[i + 1] - p0) / ((t_[i + 1] - (t_[i - 1] if i > 0 else t_[i])) or 1) * h
                m2 = (p3 - v[i]) / (((t_[i + 2] if i + 2 < len(v) else t_[i + 1]) - t_[i]) or 1) * h
                if i == 0:
                    m1 = m1 * 0
                if i + 2 >= len(v):
                    m2 = m2 * 0
                u2, u3 = u * u, u * u * u
                out = (2 * u3 - 3 * u2 + 1) * v[i] + (u3 - 2 * u2 + u) * m1 + (-2 * u3 + 3 * u2) * v[i + 1] + (u3 - u2) * m2
        return out if len(out) > 1 else float(out[0])


class QuatCurve:
    def __init__(self, keys):
        self.t = np.array([k[0] for k in keys])
        qs = [qnorm(np.array(k[1], float)) for k in keys]
        for i in range(1, len(qs)):
            if np.dot(qs[i], qs[i - 1]) < 0:
                qs[i] = -qs[i]
        self.q = qs

    def __call__(self, t):
        if t <= self.t[0]:
            return self.q[0]
        if t >= self.t[-1]:
            return self.q[-1]
        i = np.searchsorted(self.t, t) - 1
        u = smooth((t - self.t[i]) / (self.t[i + 1] - self.t[i]))
        return qslerp(self.q[i], self.q[i + 1], u)


def frame_from(d, zhint):
    """Rotation whose local Y maps to d and local Z is as close as possible to zhint."""
    y = np.asarray(d, float)
    y = y / np.linalg.norm(y)
    z = np.asarray(zhint, float)
    x = np.cross(y, z)
    x /= np.linalg.norm(x)
    z = np.cross(x, y)
    return qfrommat(np.stack([x, y, z], 1))


class Clip:
    def __init__(self, duration, skel=None):
        self.s = skel or Skeleton()
        self.n = int(round(duration * FPS)) + 1
        self.times = np.arange(self.n) / FPS
        self.T = np.repeat(self.s.rest_t[None], self.n, 0).copy()
        self.Q = np.repeat(self.s.rest_q[None], self.n, 0).copy()
        self.annotations = []
        self.props = [None] * self.n
        self.idx = self.s.idx

    # ---------- layers ----------
    def set_pose(self, t99, q99, bones=None):
        for f in range(self.n):
            self._apply(f, t99, q99, 1.0, bones)

    def _apply(self, f, t99, q99, w, bones=None):
        rng = bones if bones is not None else range(min(len(t99), len(self.T[f])))
        for b in rng:
            if w >= 1:
                self.T[f, b] = t99[b]
                self.Q[f, b] = q99[b]
            elif w > 0:
                self.T[f, b] = self.T[f, b] * (1 - w) + t99[b] * w
                self.Q[f, b] = qslerp(self.Q[f, b], q99[b], w)

    def blend_pose(self, t99, q99, weight, bones=None):
        for f, t in enumerate(self.times):
            self._apply(f, t99, q99, weight(t), bones)

    def mocap(self, bvh_path, timemap, weight=lambda t: 1.0, mirror=False, bones=None, yaw_at=None):
        bvh = BVH(bvh_path)
        rt = Retargeter(self.s)
        rt.calibrate(bvh)
        yaw = 0.0
        if yaw_at is not None:
            R, P = bvh.world(int(yaw_at / bvh.ftime))
            # facing: hips local +Z in BVH world, projected on XZ plane
            fwd = R[bvh.jidx["Hips"]] @ np.array([0, 0, 1.0])
            yaw = np.arctan2(fwd[0], fwd[2])
        rt.yaw = yaw
        for f, t in enumerate(self.times):
            w = weight(t)
            if w <= 0:
                continue
            mt = timemap(t)
            fr = min(int(round(mt / bvh.ftime)), bvh.nframes - 1)
            lt, lq = rt.frame(bvh, fr, mirror)
            self._apply(f, lt, lq, w, bones)

    def fk(self, f):
        return self.s.fk(self.T[f], self.Q[f])

    # ---------- IK ----------
    def two_bone(self, f, a, b, c, target, pole, w=1.0):
        """a=upper bone, b=lower bone, c=end bone indices. Moves c's origin to target."""
        if w <= 0:
            return
        s = self.s
        wt, wq = self.fk(f)
        A, B, Cp = wt[a], wt[b], wt[c]
        target = Cp + (np.asarray(target) - Cp) * w
        la, lb = np.linalg.norm(B - A), np.linalg.norm(Cp - B)
        d = target - A
        dist = np.clip(np.linalg.norm(d), abs(la - lb) + 1e-3, la + lb - 1e-3)
        dn = d / np.linalg.norm(d)
        cosA = (la * la + dist * dist - lb * lb) / (2 * la * dist)
        pole = np.asarray(pole, float)
        pn = pole - np.dot(pole, dn) * dn
        if np.linalg.norm(pn) < 1e-6:
            pn = (B - A) - np.dot(B - A, dn) * dn
        pn /= np.linalg.norm(pn)
        newB = A + la * (cosA * dn + np.sqrt(max(0, 1 - cosA * cosA)) * pn)
        newC = A + dn * dist
        # rotate a
        ra = qalign(B - A, newB - A)
        wa = qmul(ra, wq[a])
        pa = s.parents[a]
        self.Q[f, a] = qnorm(qmul(qconj(wq[pa]), wa))
        wt, wq = self.fk(f)
        rb = qalign(wt[c] - wt[b], newC - wt[b])
        wb = qmul(rb, wq[b])
        self.Q[f, b] = qnorm(qmul(qconj(wq[s.parents[b]]), wb))

    def set_world_rot(self, f, bone, wrot, w=1.0):
        if w <= 0:
            return
        wt, wq = self.fk(f)
        target = qslerp(wq[bone], wrot, w) if w < 1 else wrot
        self.Q[f, bone] = qnorm(qmul(qconj(wq[self.s.parents[bone]]), target))

    def arm(self, side, pos_curve, pole_curve, weight, rot_curve=None, rot_bone=None):
        S = "L" if side == "L" else "R"
        a, b, c = self.idx(f"NPC {S} UpperArm"), self.idx(f"NPC {S} Forearm"), self.idx(f"NPC {S} Hand")
        rb = self.idx(rot_bone) if rot_bone else c
        for f, t in enumerate(self.times):
            w = weight(t)
            if w <= 0:
                continue
            if rot_curve is not None:
                # hand rotation first (so the IK target refers to the wrist which is unaffected)
                self._set_hand_from(f, c, rb, rot_curve(t), w)
            self.two_bone(f, a, b, c, pos_curve(t), pole_curve(t), w)
            if rot_curve is not None:
                self._set_hand_from(f, c, rb, rot_curve(t), w)

    def aim_hand(self, side, dir_curve, weight, finger="Finger20"):
        """Rotate the hand so its knuckle line points along dir_curve(t)."""
        s = self.s
        S = side
        hand = self.idx(f"NPC {S} Hand")
        rest_wt, rest_wq = s.rest_world()
        knuck = rest_wt[self.idx(f"NPC {S} {finger}")] - rest_wt[hand]
        local = qrot(qconj(rest_wq[hand]), knuck / np.linalg.norm(knuck))
        for f, t in enumerate(self.times):
            w = weight(t)
            if w <= 0:
                continue
            wt, wq = self.fk(f)
            cur = qrot(wq[hand], local)
            d = np.asarray(dir_curve(t), float)
            r = qalign(cur, d / np.linalg.norm(d))
            self.set_world_rot(f, hand, qmul(r, wq[hand]), w)

    def _set_hand_from(self, f, hand, rb, wrot_rb, w):
        """Set hand world rotation so that bone rb (a child of the hand, e.g. Shield) gets wrot_rb."""
        if rb == hand:
            self.set_world_rot(f, hand, wrot_rb, w)
            return
        local = self.s.rest_q[rb] if True else None
        hand_w = qmul(wrot_rb, qconj(self.Q[f, rb]))
        self.set_world_rot(f, hand, hand_w, w)

    def leg_lock(self, side, target_fn, weight, pole=(0, 1, 0)):
        S = side
        a, b, c = self.idx(f"NPC {S} Thigh"), self.idx(f"NPC {S} Calf"), self.idx(f"NPC {S} Foot")
        for f, t in enumerate(self.times):
            w = weight(t)
            if w <= 0:
                continue
            wt, wq = self.fk(f)
            foot_rot = wq[c].copy()
            self.two_bone(f, a, b, c, target_fn(t, f), pole, w)
            self.set_world_rot(f, c, foot_rot, 1.0)

    def look_at(self, point_curve, weight, share=(0.25, 0.35, 0.4)):
        s = self.s
        bones = [self.idx("NPC Spine2"), self.idx("NPC Neck"), self.idx("NPC Head")]
        rest_wt, rest_wq = s.rest_world()
        head = bones[2]
        fwd_local = qrot(qconj(rest_wq[head]), np.array([0, 1.0, 0]))
        for f, t in enumerate(self.times):
            w = weight(t)
            if w <= 0:
                continue
            for bi, sh in zip(bones, share):
                wt, wq = self.fk(f)
                cur = qrot(wq[head], fwd_local)
                want = np.asarray(point_curve(t)) - wt[head]
                want /= np.linalg.norm(want)
                r = qalign(cur, want)
                r = qslerp(np.array([0, 0, 0, 1.0]), r, sh * w / max(1e-6, 1 - sum(share[:bones.index(bi)])) if bi != head else w)
                self.set_world_rot(f, bi, qmul(r, wq[bi]))

    def curl_fingers(self, side, amount_curve, thumb=0.5):
        """Curl fingers about each finger bone's local bend axis (derived from rest pose)."""
        s = self.s
        rest_wt, rest_wq = s.rest_world()
        S = side
        hand = self.idx(f"NPC {S} Hand")
        thumb_dir = rest_wt[self.idx(f"NPC {S} Finger00")] - rest_wt[hand]
        for f, t in enumerate(self.times):
            a = amount_curve(t)
            if a <= 0:
                continue
            for fi in range(5):
                for seg in range(3):
                    b = self.idx(f"NPC {S} Finger{fi}{seg}")
                    child = self.idx(f"NPC {S} Finger{fi}{seg + 1}") if seg < 2 else None
                    p = rest_wt[child] if child else rest_wt[b] + (rest_wt[b] - rest_wt[s.parents[b]])
                    along = p - rest_wt[b]
                    palm = np.cross(rest_wt[self.idx(f"NPC {S} Finger10")] - rest_wt[hand],
                                    rest_wt[self.idx(f"NPC {S} Finger40")] - rest_wt[hand])
                    if S == "R":
                        palm = -palm
                    axis_w = np.cross(along, palm)
                    axis_l = qrot(qconj(rest_wq[b]), axis_w / np.linalg.norm(axis_w))
                    ang = (1.2 if seg == 0 else 1.4) * a * (thumb if fi == 0 else 1.0)
                    r = np.concatenate([axis_l * np.sin(ang / 2), [np.cos(ang / 2)]])
                    self.Q[f, b] = qnorm(qmul(self.Q[f, b], r))

    # ---------- finishing ----------
    def smooth_rotations(self, sigma_frames=1.0):
        if sigma_frames <= 0:
            return
        k = int(3 * sigma_frames)
        ker = np.exp(-0.5 * (np.arange(-k, k + 1) / sigma_frames) ** 2)
        ker /= ker.sum()
        Q = self.Q.copy()
        for f in range(1, self.n):
            flip = np.sum(Q[f] * Q[f - 1], -1) < 0
            Q[f, flip] *= -1
        T = self.T.copy()
        out_q = np.zeros_like(Q)
        out_t = np.zeros_like(T)
        for f in range(self.n):
            idx = np.clip(np.arange(f - k, f + k + 1), 0, self.n - 1)
            out_q[f] = np.tensordot(ker, Q[idx], 1)
            out_t[f] = np.tensordot(ker, T[idx], 1)
        self.Q = out_q / np.linalg.norm(out_q, axis=-1, keepdims=True)
        self.T = out_t

    def torch_prop(self, visible):
        sh = self.idx("Shield")
        for f, t in enumerate(self.times):
            if visible(t):
                wt, wq = self.fk(f)
                self.props[f] = (wt[sh] + qrot(wq[sh], np.array([0.2, -10.9, 1.0])), wt[sh] + qrot(wq[sh], np.array([0.2, 35.0, 0.8])))
            else:
                self.props[f] = None

    def torch_head(self, f):
        sh = self.idx("Shield")
        wt, wq = self.fk(f)
        return wt[sh] + qrot(wq[sh], np.array([0.0, 30.0, 0.8]))

    def render(self, out, title, count=24):
        idx = np.linspace(0, self.n - 1, count).astype(int)
        frames = [self.fk(i)[0] for i in idx]
        render_frames(self.s, frames, [self.times[i] for i in idx], out, title, props=[self.props[i] for i in idx])
