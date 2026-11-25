"""Render a contact sheet of stick-figure poses from a BVH file (side and front views)."""
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def parse_bvh(path):
    with open(path) as f:
        tokens = f.read().split()
    i = 0
    joints, parents, offsets, channels = [], [], [], []
    stack = []
    while tokens[i] != "MOTION":
        t = tokens[i]
        if t in ("ROOT", "JOINT"):
            name = tokens[i + 1]
            joints.append(name)
            parents.append(stack[-1] if stack else -1)
            i += 2
        elif t == "End":
            joints.append(tokens[i - 0] + "_end_" + str(len(joints)))
            parents.append(stack[-1])
            channels.append([])
            i += 2
        elif t == "{":
            stack.append(len(joints) - 1)
            i += 1
        elif t == "}":
            stack.pop()
            i += 1
        elif t == "OFFSET":
            offsets.append([float(x) for x in tokens[i + 1:i + 4]])
            i += 4
        elif t == "CHANNELS":
            n = int(tokens[i + 1])
            channels.append(tokens[i + 2:i + 2 + n])
            i += 2 + n
        else:
            i += 1
    i += 1
    assert tokens[i] == "Frames:"
    nframes = int(tokens[i + 1])
    ftime = float(tokens[i + 4])
    data = np.array(tokens[i + 5:], dtype=float).reshape(nframes, -1)
    return joints, parents, np.array(offsets), channels, data, ftime


def rot(axis, deg):
    r = np.radians(deg)
    c, s = np.cos(r), np.sin(r)
    if axis == "X":
        return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
    if axis == "Y":
        return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def fk(joints, parents, offsets, channels, frame):
    n = len(joints)
    pos = np.zeros((n, 3))
    R = [None] * n
    ci = 0
    for j in range(n):
        ch = channels[j]
        vals = frame[ci:ci + len(ch)]
        ci += len(ch)
        local_t = offsets[j].copy()
        local_r = np.eye(3)
        for c, v in zip(ch, vals):
            if c.endswith("position"):
                local_t["XYZ".index(c[0])] = v
            else:
                local_r = local_r @ rot(c[0], v)
        p = parents[j]
        if p < 0:
            R[j] = local_r
            pos[j] = local_t
        else:
            R[j] = R[p] @ local_r
            pos[j] = pos[p] + R[p] @ offsets[j] if not ch or not any(c.endswith("position") for c in ch) else pos[p] + R[p] @ local_t
    return pos


def main(path, out, cols=8, rows=3):
    joints, parents, offsets, channels, data, ftime = parse_bvh(path)
    n = len(data)
    idx = np.linspace(0, n - 1, cols * rows).astype(int)
    fig, axes = plt.subplots(rows, cols, figsize=(cols * 2.2, rows * 3.2))
    for ax, fi in zip(axes.flat, idx):
        p = fk(joints, parents, offsets, channels, data[fi])
        # BVH is Y-up; plot side view (Z horizontal) and front view offset (X horizontal)
        root = p[0]
        for j, par in enumerate(parents):
            if par < 0:
                continue
            col = "C3" if "Left" in joints[j] or "left" in joints[j].lower() and "end" not in joints[j] else "C0"
            ax.plot([p[par, 2] - root[2], p[j, 2] - root[2]], [p[par, 1], p[j, 1]], color=col, lw=1.5)
            ax.plot([p[par, 0] - root[0] + 40, p[j, 0] - root[0] + 40], [p[par, 1], p[j, 1]], color=col, lw=1.0, alpha=0.6)
        ax.set_title(f"{fi * ftime:.1f}s", fontsize=8)
        ax.set_aspect("equal")
        ax.set_xlim(-30, 70)
        ax.set_ylim(-5, 40) if p[:, 1].max() < 50 else ax.set_ylim(-5, p[:, 1].max() + 5)
        ax.axis("off")
    fig.suptitle(f"{path.split('/')[-1]}  {n * ftime:.1f}s  (left: side view, right: front view, red = left side)", fontsize=10)
    fig.tight_layout()
    fig.savefig(out, dpi=70)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
