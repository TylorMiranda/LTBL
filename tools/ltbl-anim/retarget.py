"""Retarget CMU BVH motion onto the Skyrim (XPMSSE) skeleton.

Torso bones copy world-space rotation deltas from the source; limb bones additionally
re-aim along the source bone direction (handles T-pose vs A-pose rest differences).
Output: per-frame local translations/rotations for all skeleton bones, 30 fps.
"""
import numpy as np
from skel import Skeleton, qmul, qconj, qrot, qfrommat, qalign, qnorm, qslerp, NUM_TRACKS

# BVH (Y up, Z forward, X left) -> Skyrim (Z up, Y forward, -X left)
C = np.array([[-1, 0, 0], [0, 0, 1], [0, 1, 0]], float)
CQ = qfrommat(C)

TORSO = {  # skyrim bone: cmu joint
    "NPC COM": "Hips",
    "NPC Head": "Head",
}
LIMBS = {  # skyrim bone: (cmu joint, cmu child joint, skyrim child bone)
    "NPC Spine": ("LowerBack", "Spine", "NPC Spine1"),
    "NPC Spine1": ("Spine", "Spine1", "NPC Spine2"),
    "NPC Spine2": ("Spine1", "Neck1", "NPC Neck"),
    "NPC Neck": ("Neck1", "Head", "NPC Head"),
    "NPC L Clavicle": ("LeftShoulder", "LeftArm", "NPC L UpperArm"),
    "NPC L UpperArm": ("LeftArm", "LeftForeArm", "NPC L Forearm"),
    "NPC L Forearm": ("LeftForeArm", "LeftHand", "NPC L Hand"),
    "NPC R Clavicle": ("RightShoulder", "RightArm", "NPC R UpperArm"),
    "NPC R UpperArm": ("RightArm", "RightForeArm", "NPC R Forearm"),
    "NPC R Forearm": ("RightForeArm", "RightHand", "NPC R Hand"),
    "NPC L Thigh": ("LeftUpLeg", "LeftLeg", "NPC L Calf"),
    "NPC L Calf": ("LeftLeg", "LeftFoot", "NPC L Foot"),
    "NPC L Foot": ("LeftFoot", "LeftToeBase", "NPC L Toe0"),
    "NPC R Thigh": ("RightUpLeg", "RightLeg", "NPC R Calf"),
    "NPC R Calf": ("RightLeg", "RightFoot", "NPC R Foot"),
    "NPC R Foot": ("RightFoot", "RightToeBase", "NPC R Toe0"),
}


# ---------- BVH ----------
def _rot(axis, deg):
    r = np.radians(deg)
    c, s = np.cos(r), np.sin(r)
    if axis == "X":
        return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
    if axis == "Y":
        return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


class BVH:
    def __init__(self, path):
        tokens = open(path).read().split()
        i = 0
        self.joints, self.parents, offs, self.channels = [], [], [], []
        stack = []
        while tokens[i] != "MOTION":
            t = tokens[i]
            if t in ("ROOT", "JOINT"):
                self.joints.append(tokens[i + 1])
                self.parents.append(stack[-1] if stack else -1)
                i += 2
            elif t == "End":
                self.joints.append(self.joints[stack[-1]] + "_End")
                self.parents.append(stack[-1])
                self.channels.append([])
                i += 2
            elif t == "{":
                stack.append(len(self.joints) - 1)
                i += 1
            elif t == "}":
                stack.pop()
                i += 1
            elif t == "OFFSET":
                offs.append([float(x) for x in tokens[i + 1:i + 4]])
                i += 4
            elif t == "CHANNELS":
                n = int(tokens[i + 1])
                self.channels.append(tokens[i + 2:i + 2 + n])
                i += 2 + n
            else:
                i += 1
        self.offsets = np.array(offs)
        i += 1
        self.nframes = int(tokens[i + 1])
        self.ftime = float(tokens[i + 4])
        self.data = np.array(tokens[i + 5:], dtype=float).reshape(self.nframes, -1)
        self.jidx = {j: k for k, j in enumerate(self.joints)}

    def world(self, f):
        """World rotation matrices and positions of all joints at frame f (BVH space)."""
        n = len(self.joints)
        R = np.zeros((n, 3, 3))
        P = np.zeros((n, 3))
        frame = self.data[f]
        ci = 0
        for j in range(n):
            ch = self.channels[j]
            vals = frame[ci:ci + len(ch)]
            ci += len(ch)
            lt = self.offsets[j].copy()
            lr = np.eye(3)
            for c, v in zip(ch, vals):
                if c.endswith("position"):
                    lt["XYZ".index(c[0])] = v
                else:
                    lr = lr @ _rot(c[0], v)
            p = self.parents[j]
            if p < 0:
                R[j], P[j] = lr, lt
            else:
                R[j] = R[p] @ lr
                P[j] = P[p] + R[p] @ lt
        return R, P

    def rest_positions(self):
        n = len(self.joints)
        P = np.zeros((n, 3))
        for j in range(n):
            p = self.parents[j]
            P[j] = self.offsets[j] if p < 0 else P[p] + self.offsets[j]
        return P


M = np.diag([-1.0, 1.0, 1.0])  # mirror across the BVH YZ plane (swap left/right)


def mirrored_name(n):
    if n.startswith("Left"):
        return "Right" + n[4:]
    if n.startswith("Right"):
        return "Left" + n[5:]
    return n


class Retargeter:
    def __init__(self, skel=None):
        self.skel = skel or Skeleton()
        s = self.skel
        self.rest_wt, self.rest_wq = s.rest_world()
        # bone axis (unit, in bone-local space) toward the mapped child
        self.local_axis = {}
        for b, (_, _, child) in LIMBS.items():
            bi, ci = s.idx(b), s.idx(child)
            d = self.rest_wt[ci] - self.rest_wt[bi]
            self.local_axis[b] = qrot(qconj(self.rest_wq[bi]), d / np.linalg.norm(d))
        self.foot_bones = [s.idx(n) for n in ("NPC L Foot", "NPC R Foot", "NPC L Toe0", "NPC R Toe0")]
        self.rest_ground = min(self.rest_wt[i][2] for i in self.foot_bones)

    def frame(self, bvh, f, mirror=False):
        """Return (local_t, local_q) for all bones for BVH frame f."""
        s = self.skel
        R, P = bvh.world(f)
        yaw = getattr(self, "yaw", 0.0)
        if yaw:
            c, sn = np.cos(-yaw), np.sin(-yaw)
            Ry = np.array([[c, 0, sn], [0, 1, 0], [-sn, 0, c]])
            R = np.einsum("ij,njk->nik", Ry, R)
            P = P @ Ry.T
        if mirror:
            R2, P2 = R.copy(), P.copy()
            for j, name in enumerate(bvh.joints):
                k = bvh.jidx.get(mirrored_name(name), j)
                R2[j] = M @ R[k] @ M
                P2[j] = M @ P[k]
            R, P = R2, P2

        def src_q(joint):
            return qfrommat(C @ R[bvh.jidx[joint]] @ C.T)

        def src_p(joint):
            return C @ P[bvh.jidx[joint]]

        n = len(s.names)
        wq = self.rest_wq.copy()
        targets = {}
        for b, j in TORSO.items():
            targets[s.idx(b)] = qmul(src_q(j), self.rest_wq[s.idx(b)])
        for b, (j, jc, _) in LIMBS.items():
            bi = s.idx(b)
            wc = qmul(src_q(j), self.rest_wq[bi])
            dc = qrot(wc, self.local_axis[b])
            ds = src_p(jc) - src_p(j)
            if np.linalg.norm(ds) < 1e-6:
                targets[bi] = wc
            else:
                targets[bi] = qmul(qalign(dc, ds / np.linalg.norm(ds)), wc)

        # Build local rotations in hierarchy order; unmapped bones keep rest local rotation.
        local_q = self.skel.rest_q.copy()
        local_t = self.skel.rest_t.copy()
        for i in range(n):
            p = s.parents[i]
            if i in targets:
                parent_w = wq[p] if p >= 0 else np.array([0, 0, 0, 1.0])
                local_q[i] = qnorm(qmul(qconj(parent_w), targets[i]))
            wq[i] = qmul(wq[p], local_q[i]) if p >= 0 else local_q[i]

        # Hips motion -> COM translation (in place horizontally, keep crouch offset over the feet)
        hips = src_p("Hips")
        feet_mid = 0.5 * (src_p("LeftFoot") + src_p("RightFoot"))
        com = s.idx("NPC COM")
        off = (hips - feet_mid) * self.scale
        local_t[com] = np.array([self.rest_wt[com][0] + 0.0, self.rest_wt[com][1] + off[1] - self.rest_off[1], 0.0])
        local_t[com][2] = self.rest_wt[com][2]
        # ground the lowest foot point on the floor
        wt, _ = s.fk(local_t, local_q)
        ground = min(wt[i][2] for i in self.foot_bones)
        local_t[com][2] += self.rest_ground - ground
        return local_t, local_q

    def calibrate(self, bvh):
        """Scale source to target by leg length; record rest hips offset over feet."""
        Pr = bvh.rest_positions()
        j = bvh.jidx
        src_leg = np.linalg.norm(Pr[j["LeftUpLeg"]] - Pr[j["LeftLeg"]]) + np.linalg.norm(Pr[j["LeftLeg"]] - Pr[j["LeftFoot"]])
        s = self.skel
        tl = np.linalg.norm(self.rest_wt[s.idx("NPC L Thigh")] - self.rest_wt[s.idx("NPC L Calf")]) + np.linalg.norm(self.rest_wt[s.idx("NPC L Calf")] - self.rest_wt[s.idx("NPC L Foot")])
        self.scale = tl / src_leg
        self.rest_off = (C @ (Pr[j["Hips"]] - 0.5 * (Pr[j["LeftFoot"]] + Pr[j["RightFoot"]]))) * self.scale

    def clip(self, bvh, t0, t1, fps=30, mirror=False):
        self.calibrate(bvh)
        times = np.arange(t0, t1 + 1e-9, 1.0 / fps)
        out_t, out_q = [], []
        for t in times:
            f = min(int(round(t / bvh.ftime)), bvh.nframes - 1)
            lt, lq = self.frame(bvh, f, mirror)
            out_t.append(lt)
            out_q.append(lq)
        return np.array(out_t), np.array(out_q)
