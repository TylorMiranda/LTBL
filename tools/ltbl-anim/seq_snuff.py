"""LTBL torch put-out sequences: ground snuff, shore/puddle douse (kneel), basin douse (trough/barrel/well).
Each ends by stowing the torch back over the left shoulder."""
import sys
import numpy as np
sys.path.insert(0, "/home/claude/anim/tools")
from author import Clip, Curve, QuatCurve, frame_from
from skel import qrot

CMU = "/home/claude/anim/cmu/data"
ref = np.load("/home/claude/anim/game/refposes.npz")
from skel import Skeleton
SKEL = Skeleton()

UPPER = ("NPC COM", "NPC Pelvis", "NPC Spine", "NPC Spine1", "NPC Spine2", "NPC Neck", "NPC Head")
LEGS = ("NPC L Thigh", "NPC L Calf", "NPC L Foot", "NPC L Toe0", "NPC R Thigh", "NPC R Calf", "NPC R Foot", "NPC R Toe0")

# stow keys (reverse of the draw)
STOW_UP = (np.array([-21.0, 13.0, 119.0]), frame_from([-0.15, -0.20, 0.97], [0.95, -0.3, 0.1]))
STOW_OVER = (np.array([-17.0, -3.0, 131.0]), frame_from([-0.20, -0.95, 0.20], [0.3, 0.1, 0.95]))
STOW_SLOT = (np.array([-10.5, -9.5, 121.0]), frame_from([0.05, -0.30, -0.95], [0.0, -1.0, 0.0]))


def hold_pose(c):
    s = c.s
    lt = s.rest_t.copy(); lq = s.rest_q.copy(); lt[:99] = ref["tor_t"]; lq[:99] = ref["tor_q"]
    wt, wq = s.fk(lt, lq)
    return wt[c.idx("NPC L Hand")], wq[c.idx("Shield")], wt[c.idx("NPC L Forearm")], wt, wq


def hand_for_head(head, d):
    """Wrist-side position of the Shield node so that the torch head lands on `head` with axis d."""
    from skel import qmul, qconj
    rot = frame_from(d[0], d[1])
    shield_pos = head - qrot(rot, np.array([0.0, 30.0, 0.8]))
    hand_rot = qmul(rot, qconj(SKEL.rest_q[SKEL.idx("Shield")]))
    return shield_pos - qrot(hand_rot, SKEL.rest_t[SKEL.idx("Shield")]), rot


def finish(c, stow_start, release_at, dur):
    """Common: stow keys appended by caller; seam ends."""
    c.blend_pose(ref["tor_t"], ref["tor_q"], Curve([(0.0, 1.0), (0.15, 0.0)], mode="ease"))
    c.blend_pose(ref["mt_t"], ref["mt_q"], Curve([(release_at + 0.15, 0.0), (dur, 1.0)], mode="ease"))
    c.smooth_rotations(1.0)
    c.torch_prop(lambda t: t < release_at)


def feet_lock(c, which=("L", "R"), weight=None):
    """Keep feet planted where the neutral stance puts them."""
    s = c.s
    lt = s.rest_t.copy(); lq = s.rest_q.copy(); lt[:99] = ref["mt_t"]; lq[:99] = ref["mt_q"]
    wt, _ = s.fk(lt, lq)
    for side in which:
        p = wt[c.idx(f"NPC {side} Foot")].copy()
        pole = (-0.25 if side == "L" else 0.25, 1.0, 0.0)
        c.leg_lock(side, lambda t, f, p=p: p, weight or (lambda t: 1.0), pole=pole)


def thigh_rest(c, side_hand, side_leg, weight, along=0.75, lift=4.0):
    """Hand resting on the top of a thigh near the knee."""
    pts = []
    for f in range(c.n):
        wt, _ = c.fk(f)
        a = wt[c.idx(f"NPC {side_leg} Thigh")]
        b = wt[c.idx(f"NPC {side_leg} Calf")]
        pts.append((c.times[f], a + (b - a) * along + np.array([0, 0, lift])))
    pole = [0.8, -0.3, -0.3] if side_hand == "R" else [-0.8, -0.3, -0.3]
    c.arm(side_hand, Curve(pts, mode="linear"), lambda t: pole, weight)


# ---------------------------------------------------------------- ground
def build_ground():
    DUR, OUT_AT, RELEASE = 3.4, 1.25, 2.78
    c = Clip(DUR)
    c.set_pose(ref["tor_t"], ref["tor_q"])
    # CMU 115_06 "picking box up, bending knees": the squat and the rise
    tm = Curve([(0.0, 0.40), (0.15, 0.45), (0.85, 0.85), (1.60, 0.95), (2.20, 1.35), (3.4, 1.6)], mode="linear")
    c.mocap(f"{CMU}/115/115_06.bvh", tm, bones=[c.idx(n) for n in UPPER + LEGS], yaw_at=0.3,
            weight=Curve([(0.0, 0.0), (0.2, 1.0), (2.3, 1.0), (2.8, 0.0)], mode="ease"))
    feet_lock(c)
    hold_pos, hold_rot, hold_elbow, _, _ = hold_pose(c)
    g = np.array([-21.0, 40.0, 3.0])
    d_grind = ([-0.10, 0.35, -0.93], [0.95, 0.0, 0.3])
    p0, r0 = hand_for_head(g, d_grind)
    keys_p = [(0.0, hold_pos), (0.35, hold_pos + [2, 6, -18]), (0.85, p0)]
    keys_r = [(0.0, hold_rot), (0.40, frame_from([-0.1, 0.95, 0.2], [0.95, 0.0, 0.3])), (0.85, r0)]
    # grind: small circles, twisting the head into the dirt
    for k, t in enumerate(np.arange(0.95, 1.60, 0.11)):
        ang = k * 1.7
        off = np.array([2.5 * np.cos(ang), 2.0 * np.sin(ang), -0.5])
        pk, rk = hand_for_head(g + off, ([-0.10 + 0.08 * np.cos(ang), 0.35, -0.93], [0.95, 0.15 * np.sin(ang), 0.3]))
        keys_p.append((t, pk))
        keys_r.append((t, rk))
    keys_p += [(1.95, np.array([-16.0, 22.0, 70.0])), (2.35, STOW_UP[0]), (2.58, STOW_OVER[0]), (RELEASE, STOW_SLOT[0]),
               (3.03, np.array([-23.0, -1.0, 104.0])), (3.30, np.array([-17.6, 6.2, 69.1]))]
    keys_r += [(1.80, frame_from([-0.1, 0.95, 0.2], [0.95, 0.0, 0.3])), (1.95, frame_from([-0.1, 0.2, 0.97], [0.95, -0.3, 0.0])), (2.35, STOW_UP[1]), (2.58, STOW_OVER[1]), (RELEASE, STOW_SLOT[1])]
    pole = Curve([(0.0, (hold_elbow - hold_pos) / np.linalg.norm(hold_elbow - hold_pos)), (0.7, [-0.8, 0.0, 0.3]),
                  (1.9, [-0.8, 0.0, 0.3]), (2.3, [-0.5, -0.3, -0.8]), (2.55, [-0.8, 0.0, 0.6]), (2.8, [-0.6, -0.2, 0.8]), (3.1, [-0.7, -0.4, -0.5])], mode="ease")
    c.look_at(lambda t: g, Curve([(0.3, 0.0), (0.8, 0.7), (1.6, 0.7), (2.0, 0.0)], mode="ease"))
    c.arm("L", Curve(keys_p), pole, Curve([(0.0, 1.0), (3.30, 1.0), (3.4, 0.6)], mode="ease"), rot_curve=QuatCurve(keys_r), rot_bone="Shield")
    thigh_rest(c, "R", "R", Curve([(0.3, 0.0), (0.8, 1.0), (1.7, 1.0), (2.2, 0.0)], mode="ease"))
    c.curl_fingers("L", Curve([(0.0, 0.85), (RELEASE - 0.05, 0.85), (RELEASE + 0.1, 0.0)], mode="ease"))
    finish(c, 2.0, RELEASE, DUR)
    return c, dict(out_at=OUT_AT, release=RELEASE, dur=DUR)


# ---------------------------------------------------------------- shore / puddle (kneel)
def build_water():
    DUR, OUT_AT, RELEASE = 3.6, 1.15, 3.02
    c = Clip(DUR)
    c.set_pose(ref["tor_t"], ref["tor_q"])
    # CMU 23_03 subject B kneels on one knee (and rises)
    tm = Curve([(0.0, 0.25), (0.10, 0.30), (0.95, 1.20), (1.80, 1.60), (2.60, 5.30), (3.6, 5.6)], mode="linear")
    c.mocap(f"{CMU}/023/23_03.bvh", tm, bones=[c.idx(n) for n in UPPER + LEGS], yaw_at=1.5,
            weight=Curve([(0.0, 0.0), (0.2, 1.0), (2.6, 1.0), (3.1, 0.0)], mode="ease"))
    feet_lock(c, which=("L",))
    hold_pos, hold_rot, hold_elbow, _, _ = hold_pose(c)
    w = np.array([-10.0, 52.0, -2.0])
    d_dip = ([-0.05, 0.55, -0.83], [0.95, 0.0, 0.3])
    p0, r0 = hand_for_head(w, d_dip)
    keys_p = [(0.0, hold_pos), (0.4, hold_pos + [2, 10, -22]), (0.95, p0)]
    keys_r = [(0.0, hold_rot), (0.45, frame_from([-0.05, 0.95, 0.2], [0.95, 0.0, 0.3])), (0.95, r0)]
    for k, t in enumerate(np.arange(1.10, 1.80, 0.17)):  # swish side to side under the surface
        x = 5.0 if k % 2 == 0 else -5.0
        pk, rk = hand_for_head(w + [x, 0, -0.5], ([-0.05 + x * 0.02, 0.55, -0.83], d_dip[1]))
        keys_p.append((t, pk))
        keys_r.append((t, rk))
    lift = np.array([-16.0, 30.0, 62.0])
    keys_p += [(2.05, lift), (2.18, lift + [0, 2, -7]), (2.30, lift + [0, 0, 3]), (2.62, STOW_UP[0]), (2.82, STOW_OVER[0]),
               (RELEASE, STOW_SLOT[0]), (3.27, np.array([-23.0, -1.0, 104.0])), (3.50, np.array([-17.6, 6.2, 69.1]))]
    keys_r += [(1.92, frame_from([-0.05, 0.95, 0.2], [0.95, 0.0, 0.3])), (2.05, frame_from([-0.05, 0.6, 0.8], [0.95, 0.0, 0.3])), (2.18, frame_from([-0.05, 0.75, 0.66], [0.95, 0.0, 0.3])),
               (2.30, frame_from([-0.05, 0.4, 0.9], [0.95, 0.0, 0.3])), (2.62, STOW_UP[1]), (2.82, STOW_OVER[1]), (RELEASE, STOW_SLOT[1])]
    pole = Curve([(0.0, (hold_elbow - hold_pos) / np.linalg.norm(hold_elbow - hold_pos)), (0.8, [-0.8, -0.2, 0.2]),
                  (2.1, [-0.8, -0.2, 0.2]), (2.55, [-0.5, -0.3, -0.8]), (2.8, [-0.8, 0.0, 0.6]), (3.05, [-0.6, -0.2, 0.8]), (3.35, [-0.7, -0.4, -0.5])], mode="ease")
    c.look_at(lambda t: w, Curve([(0.3, 0.0), (0.8, 0.7), (1.8, 0.7), (2.2, 0.0)], mode="ease"))
    c.arm("L", Curve(keys_p), pole, Curve([(0.0, 1.0), (3.50, 1.0), (3.6, 0.6)], mode="ease"), rot_curve=QuatCurve(keys_r), rot_bone="Shield")
    thigh_rest(c, "R", "L", Curve([(0.4, 0.0), (0.9, 1.0), (1.8, 1.0), (2.3, 0.0)], mode="ease"), along=0.85, lift=5.0)
    c.curl_fingers("L", Curve([(0.0, 0.85), (RELEASE - 0.05, 0.85), (RELEASE + 0.1, 0.0)], mode="ease"))
    finish(c, 2.3, RELEASE, DUR)
    return c, dict(out_at=OUT_AT, release=RELEASE, dur=DUR)


# ---------------------------------------------------------------- basin (trough, barrel, well)
def build_basin():
    DUR, OUT_AT, RELEASE = 3.0, 0.95, 2.42
    c = Clip(DUR)
    c.set_pose(ref["tor_t"], ref["tor_q"])
    # CMU 26_09 "bend, pick up": the first part of the bend, then back up
    tm = Curve([(0.0, 0.6), (0.1, 0.65), (0.70, 1.25), (1.40, 1.32), (1.90, 0.65), (3.0, 0.6)], mode="linear")
    c.mocap(f"{CMU}/026/26_09.bvh", tm, bones=[c.idx(n) for n in UPPER], yaw_at=1.0,
            weight=Curve([(0.0, 0.0), (0.2, 0.8), (1.9, 0.8), (2.3, 0.0)], mode="ease"))
    feet_lock(c)
    hold_pos, hold_rot, hold_elbow, _, _ = hold_pose(c)
    b = np.array([-8.0, 44.0, 52.0])
    d_dip = ([0.0, 0.30, -0.95], [0.95, 0.0, 0.3])
    p0, r0 = hand_for_head(b, d_dip)
    keys_p = [(0.0, hold_pos), (0.3, hold_pos + [3, 8, 6]), (0.70, p0)]
    keys_r = [(0.0, hold_rot), (0.3, frame_from([0.1, 0.6, 0.79], [0.95, -0.2, 0.1])), (0.48, frame_from([0.0, 0.97, 0.1], [0.95, 0.0, 0.3])), (0.70, r0)]
    for k, t in enumerate(np.arange(0.84, 1.40, 0.14)):  # stir under the surface
        ang = k * 1.6
        pk, rk = hand_for_head(b + [3 * np.cos(ang), 3 * np.sin(ang), -1.0], d_dip)
        keys_p.append((t, pk))
        keys_r.append((t, rk))
    lift = np.array([-15.0, 28.0, 84.0])
    keys_p += [(1.62, lift), (1.74, lift + [0, 2, -7]), (1.86, lift + [0, 0, 3]), (2.08, STOW_UP[0]), (2.26, STOW_OVER[0]),
               (RELEASE, STOW_SLOT[0]), (2.67, np.array([-23.0, -1.0, 104.0])), (2.90, np.array([-17.6, 6.2, 69.1]))]
    keys_r += [(1.50, frame_from([0.0, 0.97, 0.1], [0.95, 0.0, 0.3])), (1.62, frame_from([0.0, 0.5, 0.87], [0.95, 0.0, 0.3])), (1.74, frame_from([0.0, 0.7, 0.71], [0.95, 0.0, 0.3])),
               (1.86, frame_from([0.0, 0.35, 0.94], [0.95, 0.0, 0.3])), (2.08, STOW_UP[1]), (2.26, STOW_OVER[1]), (RELEASE, STOW_SLOT[1])]
    pole = Curve([(0.0, (hold_elbow - hold_pos) / np.linalg.norm(hold_elbow - hold_pos)), (0.6, [-0.8, -0.3, -0.3]),
                  (1.6, [-0.8, -0.3, -0.3]), (2.0, [-0.5, -0.3, -0.8]), (2.25, [-0.8, 0.0, 0.6]), (2.45, [-0.6, -0.2, 0.8]), (2.75, [-0.7, -0.4, -0.5])], mode="ease")
    c.look_at(lambda t: b, Curve([(0.2, 0.0), (0.6, 0.7), (1.5, 0.7), (1.9, 0.0)], mode="ease"))
    c.arm("L", Curve(keys_p), pole, Curve([(0.0, 1.0), (2.90, 1.0), (3.0, 0.6)], mode="ease"), rot_curve=QuatCurve(keys_r), rot_bone="Shield")
    c.curl_fingers("L", Curve([(0.0, 0.85), (RELEASE - 0.05, 0.85), (RELEASE + 0.1, 0.0)], mode="ease"))
    finish(c, 1.9, RELEASE, DUR)
    return c, dict(out_at=OUT_AT, release=RELEASE, dur=DUR)


if __name__ == "__main__":
    for name, fn in (("ground", build_ground), ("water", build_water), ("basin", build_basin)):
        c, meta = fn()
        c.render(f"/home/claude/anim/sheets/ltbl_{name}.png", f"LTBL {name}: put out and stow  {meta}")
        np.savez(f"/home/claude/anim/out/{name}.npz", T=c.T, Q=c.Q)
        print(name, c.n, meta, flush=True)
