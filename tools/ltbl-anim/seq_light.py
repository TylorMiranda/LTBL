"""LTBL torch lighting: reach over left shoulder, draw torch, strike flint at the head, raise to carry."""
import sys
import numpy as np
sys.path.insert(0, "/home/claude/anim/tools")
from author import Clip, Curve, QuatCurve, frame_from, smooth
from skel import qrot

CMU = "/home/claude/anim/cmu/data"
ref = np.load("/home/claude/anim/game/refposes.npz")

DUR = 3.6
EQUIP_AT = 0.62     # hand closes on the handle behind the shoulder
FLAME_AT = 2.40


def build():
    c = Clip(DUR)
    # 1) standing base: neutral stance
    c.set_pose(ref["mt_t"], ref["mt_q"])
    # 2) CMU 79_86 (archer drawing an arrow from a back quiver), mirrored so the LEFT hand reaches back.
    #    our 0.10..0.95 s  <->  mocap 1.00..1.85 s
    tm = Curve([(0.0, 0.95), (0.10, 1.0), (0.62, 1.48), (0.95, 1.85), (1.4, 1.85)], mode="linear")
    body_w = Curve([(0.0, 0.0), (0.18, 0.75), (0.9, 0.75), (1.5, 0.0)], mode="ease")
    upper = [c.idx(n) for n in ("NPC COM", "NPC Pelvis", "NPC Spine", "NPC Spine1", "NPC Spine2", "NPC Neck", "NPC Head")]
    c.mocap(f"{CMU}/079/79_86.bvh", tm, weight=body_w, mirror=True, bones=upper, yaw_at=1.0)
    larm = [c.idx(n) for n in ("NPC L Clavicle", "NPC L UpperArm", "NPC L Forearm", "NPC L Hand")]
    arm_w = Curve([(0.0, 0.0), (0.12, 1.0), (0.5, 1.0), (0.62, 0.0)], mode="ease")
    c.mocap(f"{CMU}/079/79_86.bvh", tm, weight=arm_w, mirror=True, bones=larm, yaw_at=1.0)

    # eyes on the work (before arm IK so the shoulders are final)
    head_pt = np.array([-2.0, 34.0, 96.0])
    look_w = Curve([(1.15, 0.0), (1.40, 0.85), (2.55, 0.85), (2.95, 0.0)], mode="ease")
    c.look_at(lambda t: head_pt, look_w)

    # 3) left hand keys (torch frame: Y = torch axis toward the head, Z = palm normal)
    tor_hand = None
    from skel import Skeleton
    s = c.s
    lt = s.rest_t.copy(); lq = s.rest_q.copy(); lt[:99] = ref["tor_t"]; lq[:99] = ref["tor_q"]
    wt, wq = s.fk(lt, lq)
    hold_pos = wt[c.idx("NPC L Hand")]
    hold_rot = wq[c.idx("Shield")]
    hold_elbow = wt[c.idx("NPC L Forearm")]

    strike_hand = np.array([-14.0, 17.0, 79.0])
    strike_rot = frame_from([0.38, 0.30, 0.87], [0.9, -0.25, -0.35])
    L_pos = Curve([
        (0.50, [-12.0, -6.0, 124.0]),
        (0.62, [-10.5, -9.5, 121.0]),   # grip handle behind shoulder
        (0.80, [-17.0, -3.0, 131.0]),   # pull up over the shoulder
        (1.00, [-21.0, 13.0, 119.0]),
        (1.25, strike_hand),            # bring the head forward for lighting
        (2.40, strike_hand + [0.5, 0.5, 0.5]),
        (2.55, strike_hand + [-1.0, 1.0, 2.5]),
        (3.15, hold_pos),
        (3.6, hold_pos),
    ])
    L_rot = QuatCurve([
        (0.62, frame_from([0.05, -0.30, -0.95], [0.0, -1.0, 0.0])),
        (0.80, frame_from([-0.20, -0.95, 0.20], [0.3, 0.1, 0.95])),
        (1.00, frame_from([-0.15, -0.20, 0.97], [0.95, -0.3, 0.1])),
        (1.25, strike_rot),
        (2.40, strike_rot),
        (3.15, hold_rot),
        (3.6, hold_rot),
    ])
    L_pole = Curve([
        (0.5, [-0.6, -0.2, 0.8]),
        (0.8, [-0.8, 0.0, 0.6]),
        (1.1, [-0.5, -0.3, -0.8]),
        (3.1, (hold_elbow - hold_pos) / np.linalg.norm(hold_elbow - hold_pos)),
    ], mode="ease")
    L_w = Curve([(0.45, 0.0), (0.60, 1.0), (3.6, 1.0)], mode="ease")
    c.arm("L", L_pos, L_pole, L_w, rot_curve=L_rot, rot_bone="Shield")

    # 4) right hand: flint from the belt pouch, three strikes across the torch head, flint back
    lt2 = s.rest_t.copy(); lq2 = s.rest_q.copy(); lt2[:99] = ref["mt_t"]; lq2[:99] = ref["mt_q"]
    wt2, _ = s.fk(lt2, lq2)
    r_idle = wt2[c.idx("NPC R Hand")]
    head = c.torch_head(int(1.9 * 30))  # actual torch head (through the Shield mount) mid-strike
    # keys below are where the FLINT goes (corrected to the stone, not the wrist, after solving)
    side = np.array([3.0, -2.0, 0.0])        # brush the surface of the head, not through it
    up = head + [6.0, -2.5, 6.0] + side
    down = head + [-5.0, 1.5, -6.0] + side
    pouch = np.array([13.0, 7.0, 75.0])
    R_pos = Curve([
        (0.85, r_idle), (1.10, pouch), (1.18, pouch + [0, 0, -1.0]),
        (1.42, up), (1.56, down), (1.70, up + [0, 0, -1.0]), (1.84, down), (1.98, up), (2.12, down + [0, 0, -1]),
        (2.28, up + [-1, 0, -2]), (2.40, down + [1, 0, 1]), (2.56, head + [10, -4, -2]),
        (2.82, pouch), (2.90, pouch + [0, 0, -1.0]), (3.25, r_idle), (3.6, r_idle),
    ])
    R_pole = Curve([(0.0, [0.8, -0.4, -0.4]), (1.4, [0.7, 0.0, -0.7]), (2.6, [0.7, 0.0, -0.7]), (3.3, [0.8, -0.4, -0.4])], mode="ease")
    R_w = Curve([(0.8, 0.0), (1.0, 1.0), (3.3, 1.0), (3.5, 0.0)], mode="ease")
    c.arm("R", R_pos, R_pole, R_w)
    aim_w = Curve([(1.15, 0.0), (1.35, 1.0), (2.50, 1.0), (2.75, 0.0)], mode="ease")
    c.aim_hand("R", lambda t: [-0.55, 0.75, -0.35], aim_w)
    # move the wrist so the stone itself follows the strike path
    from make_flint import hand_frame
    flint_local, _ = hand_frame()
    rh = c.idx("NPC R Hand")
    strike_w = Curve([(1.20, 0.0), (1.38, 1.0), (2.45, 1.0), (2.62, 0.0)], mode="ease")
    for _ in range(4):
        pts = []
        for f, t in enumerate(c.times):
            wt, wq = c.fk(f)
            tip = wt[rh] + qrot(wq[rh], flint_local)
            want = c.torch_head(f) + (np.asarray(R_pos(t)) - head)  # strike offsets follow the moving torch
            err = (want - tip) * strike_w(t)
            pts.append((t, wt[rh] + err))
        c.arm("R", Curve(pts, mode="linear"), R_pole, lambda t: 1.0 if strike_w(t) > 0 else 0.0)
        c.aim_hand("R", lambda t: [-0.55, 0.75, -0.35], aim_w)


    # 6) hands: left grips the handle from the grab on; right pinches the flint
    c.curl_fingers("L", Curve([(0.52, 0.0), (0.64, 0.85), (3.6, 0.85)], mode="ease"))
    c.curl_fingers("R", Curve([(1.02, 0.0), (1.14, 0.55), (2.84, 0.55), (2.96, 0.0)], mode="ease"), thumb=0.8)

    # 7) seam the ends into the engine's poses: neutral idle at start, torch carry idle at the end
    c.blend_pose(ref["mt_t"], ref["mt_q"], Curve([(0.0, 1.0), (0.12, 0.0)], mode="ease"))
    c.blend_pose(ref["tor_t"], ref["tor_q"], Curve([(3.25, 0.0), (3.6, 1.0)], mode="ease"))
    c.smooth_rotations(1.0)
    c.torch_prop(lambda t: t >= EQUIP_AT)
    c.annotations = []
    return c


if __name__ == "__main__":
    c = build()
    c.render("/home/claude/anim/sheets/ltbl_light.png", "LTBL light: draw, strike, raise")
    np.savez("/home/claude/anim/out/light.npz", T=c.T, Q=c.Q)
    print("frames", c.n)
