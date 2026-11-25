"""Skyrim skeleton loading, quaternion math, FK and stick-figure rendering."""
import re
import numpy as np

SKEL_XML = "/mnt/user-data/uploads/XP32 Maximum Skeleton Special Extended/meshes/actors/character/character assets/skeleton.xml"
NUM_TRACKS = 99


# ---------- quaternions (x, y, z, w) ----------
def qmul(a, b):
    ax, ay, az, aw = np.moveaxis(a, -1, 0)
    bx, by, bz, bw = np.moveaxis(b, -1, 0)
    return np.stack([
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    ], -1)


def qconj(q):
    return q * np.array([-1, -1, -1, 1.0])


def qnorm(q):
    return q / np.linalg.norm(q, axis=-1, keepdims=True)


def qrot(q, v):
    qv = np.concatenate([v, np.zeros(v.shape[:-1] + (1,))], -1)
    return qmul(qmul(q, qv), qconj(q))[..., :3]


def qfrommat(m):
    m = np.asarray(m)
    t = np.trace(m)
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        q = [(m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s]
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = [0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s]
    elif m[1, 1] > m[2, 2]:
        s = np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = [(m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s]
    else:
        s = np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = [(m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s]
    return qnorm(np.array(q))


def qaxisangle(axis, ang):
    axis = np.asarray(axis, float)
    axis = axis / np.linalg.norm(axis)
    return np.concatenate([axis * np.sin(ang / 2), [np.cos(ang / 2)]])


def qalign(a, b):
    """Shortest rotation taking unit vector a to unit vector b."""
    a = a / np.linalg.norm(a)
    b = b / np.linalg.norm(b)
    c = np.cross(a, b)
    d = np.dot(a, b)
    if d < -0.999999:
        ortho = np.cross(a, [1, 0, 0])
        if np.linalg.norm(ortho) < 1e-6:
            ortho = np.cross(a, [0, 1, 0])
        return qaxisangle(ortho, np.pi)
    q = np.array([c[0], c[1], c[2], 1 + d])
    return qnorm(q)


def qslerp(a, b, t):
    d = np.dot(a, b)
    if d < 0:
        b = -b
        d = -d
    if d > 0.9995:
        return qnorm(a + t * (b - a))
    th = np.arccos(d)
    return (np.sin((1 - t) * th) * a + np.sin(t * th) * b) / np.sin(th)


# ---------- skeleton ----------
class Skeleton:
    def __init__(self, path=SKEL_XML):
        s = open(path).read()
        sk = s[s.find('class="hkaSkeleton"'):]
        sk = sk[:sk.find("</hkobject>\n\n")]
        self.names = re.findall(r'<hkparam name="name">([^<]*)</hkparam>', sk)[1:]
        self.parents = list(map(int, re.search(r'name="parentIndices" numelements="\d+">(.*?)</hkparam>', sk, re.S).group(1).split()))
        rp = re.search(r'name="referencePose" numelements="\d+">(.*?)</hkparam>', sk, re.S).group(1)
        nums = [list(map(float, g.split())) for g in re.findall(r"\(([^)]*)\)", rp)]
        n = len(self.names)
        self.rest_t = np.array([nums[3 * i] for i in range(n)])
        self.rest_q = np.array([nums[3 * i + 1] for i in range(n)])
        self.rest_s = np.array([nums[3 * i + 2] for i in range(n)])
        self.index = {nm: i for i, nm in enumerate(self.names)}

    def idx(self, short):
        for i, nm in enumerate(self.names):
            if nm == short or nm.startswith(short + " [") or nm.startswith(short):
                return i
        raise KeyError(short)

    def fk(self, local_t, local_q):
        n = len(local_t)
        wt = np.zeros((n, 3))
        wq = np.zeros((n, 4))
        for i in range(n):
            p = self.parents[i]
            if p < 0:
                wt[i] = local_t[i]
                wq[i] = local_q[i]
            else:
                wq[i] = qmul(wq[p], local_q[i])
                wt[i] = wt[p] + qrot(wq[p], local_t[i])
        return wt, wq

    def rest_world(self):
        return self.fk(self.rest_t, self.rest_q)


DRAW_BONES = [
    ("NPC Pelvis", "NPC L Thigh"), ("NPC L Thigh", "NPC L Calf"), ("NPC L Calf", "NPC L Foot"), ("NPC L Foot", "NPC L Toe0"),
    ("NPC Pelvis", "NPC R Thigh"), ("NPC R Thigh", "NPC R Calf"), ("NPC R Calf", "NPC R Foot"), ("NPC R Foot", "NPC R Toe0"),
    ("NPC Pelvis", "NPC Spine"), ("NPC Spine", "NPC Spine1"), ("NPC Spine1", "NPC Spine2"), ("NPC Spine2", "NPC Neck"), ("NPC Neck", "NPC Head"),
    ("NPC Spine2", "NPC L Clavicle"), ("NPC L Clavicle", "NPC L UpperArm"), ("NPC L UpperArm", "NPC L Forearm"), ("NPC L Forearm", "NPC L Hand"), ("NPC L Hand", "NPC L Finger20"),
    ("NPC Spine2", "NPC R Clavicle"), ("NPC R Clavicle", "NPC R UpperArm"), ("NPC R UpperArm", "NPC R Forearm"), ("NPC R Forearm", "NPC R Hand"), ("NPC R Hand", "NPC R Finger20"),
]


def render_frames(skel, frames_wt, times, out, title="", props=None, cols=8):
    """frames_wt: list of world-position arrays. props: optional list of (start,end) segments per frame for a prop."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    pairs = [(skel.idx(a), skel.idx(b)) for a, b in DRAW_BONES]
    rows = int(np.ceil(len(frames_wt) / cols))
    fig, axes = plt.subplots(rows, cols, figsize=(cols * 2.4, rows * 3.0))
    axes = np.atleast_1d(axes).ravel()
    for k, ax in enumerate(axes):
        ax.axis("off")
        if k >= len(frames_wt):
            continue
        p = frames_wt[k]
        for a, b in pairs:
            left = "L " in skel.names[b]
            col = "C3" if left else "C0"
            # side view: Y (forward) horizontal, Z up; front view: X horizontal shifted
            ax.plot([p[a, 1], p[b, 1]], [p[a, 2], p[b, 2]], color=col, lw=1.6)
            ax.plot([p[a, 0] + 90, p[b, 0] + 90], [p[a, 2], p[b, 2]], color=col, lw=1.0, alpha=0.6)
        if props and props[k] is not None:
            s, e = props[k]
            ax.plot([s[1], e[1]], [s[2], e[2]], color="C1", lw=3)
            ax.plot([s[0] + 90, e[0] + 90], [s[2], e[2]], color="C1", lw=2, alpha=0.6)
        ax.plot([-50, 140], [0, 0], color="0.8", lw=0.8)
        ax.set_xlim(-50, 140)
        ax.set_ylim(-5, 140)
        ax.set_aspect("equal")
        ax.set_title(f"{times[k]:.2f}s", fontsize=8)
    fig.suptitle(title + "   (left: side view, facing right; right: front view; red = left side; orange = torch)", fontsize=9)
    fig.tight_layout()
    fig.savefig(out, dpi=72)
    plt.close(fig)
