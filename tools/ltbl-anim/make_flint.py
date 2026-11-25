"""Build LTBL's flint stone mesh (SSE NIF), using the vanilla torch mesh's block formats as the template.
The stone is positioned in the right hand's local space so it attaches directly to the hand node."""
import struct
import sys
import numpy as np
sys.path.insert(0, "/home/claude/anim/tools")
from nif import Nif
from skel import Skeleton, qrot, qconj

TEMPLATE = "/home/claude/anim/game/meshes/weapons/torch/torch.nif"
OUT = sys.argv[1] if len(sys.argv) > 1 else "/home/claude/anim/out/ltblflint.nif"
DIFFUSE = "textures\\landscape\\volcanictundrarocks01.dds"
NORMAL = "textures\\landscape\\volcanictundrarocks01_n.dds"


def icosphere(sub=2):
    t = (1 + 5 ** 0.5) / 2
    v = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
         (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    v = [np.array(x, float) / np.linalg.norm(x) for x in v]
    for _ in range(sub):
        cache = {}
        nf = []

        def mid(a, b):
            k = (min(a, b), max(a, b))
            if k not in cache:
                m = v[a] + v[b]
                v.append(m / np.linalg.norm(m))
                cache[k] = len(v) - 1
            return cache[k]
        for a, b, c in f:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        f = nf
    return np.array(v), np.array(f)


def flint_geometry():
    rng = np.random.default_rng(1729)
    v, f = icosphere(2)
    # knapped look: flatten against a handful of random planes (conchoidal facets)
    for _ in range(9):
        n = rng.normal(size=3)
        n /= np.linalg.norm(n)
        d = rng.uniform(0.55, 0.8)
        proj = v @ n
        over = proj > d
        v[over] -= np.outer(proj[over] - d, n)
    v *= np.array([2.3, 1.6, 1.2])  # ~6.5 x 4.6 x 3.4 cm nodule (1 unit ~ 1.43 cm)
    v += rng.normal(scale=0.06, size=v.shape)
    return v, f


def hand_frame():
    """Place the stone between thumb and index of the right hand, in the hand bone's local space."""
    s = Skeleton()
    wt, wq = s.rest_world()
    h = s.idx("NPC R Hand")
    def loc(name):
        return qrot(qconj(wq[h]), wt[s.idx(name)] - wt[h])
    knuck = loc("NPC R Finger20")
    index = loc("NPC R Finger11")
    thumb = loc("NPC R Finger02")
    center = 0.5 * (index + thumb) + 0.15 * knuck
    x = knuck / np.linalg.norm(knuck)
    palm = np.cross(loc("NPC R Finger10"), loc("NPC R Finger40"))
    palm = -palm / np.linalg.norm(palm)
    z = palm - np.dot(palm, x) * x
    z /= np.linalg.norm(z)
    y = np.cross(z, x)
    return center, np.stack([x, y, z], 1)


def pack_vertices(v, f):
    # smooth normals
    nrm = np.zeros_like(v)
    for a, b, c in f:
        fn = np.cross(v[b] - v[a], v[c] - v[a])
        nrm[[a, b, c]] += fn
    nrm /= np.linalg.norm(nrm, axis=1, keepdims=True)
    # spherical UVs, tiled so the rock texture reads at pebble scale
    c = v - v.mean(0)
    u = 0.5 + np.arctan2(c[:, 1], c[:, 0]) / (2 * np.pi)
    w = 0.5 - np.arcsin(np.clip(c[:, 2] / np.linalg.norm(c, axis=1), -1, 1)) / np.pi
    uv = np.stack([u * 0.6, w * 0.6], 1)
    tan = np.cross(np.array([0, 0, 1.0]), nrm)
    bad = np.linalg.norm(tan, axis=1) < 1e-4
    tan[bad] = np.array([1.0, 0, 0])
    tan /= np.linalg.norm(tan, axis=1, keepdims=True)
    bit = np.cross(nrm, tan)

    def b8(x):
        return int(np.clip(round((x + 1) * 127.5), 0, 255))
    out = bytearray()
    for i in range(len(v)):
        out += struct.pack("<3ff", *v[i], bit[i, 0])
        out += np.array(uv[i], dtype="<f2").tobytes()
        out += bytes([b8(nrm[i, 0]), b8(nrm[i, 1]), b8(nrm[i, 2]), b8(bit[i, 1])])
        out += bytes([b8(tan[i, 0]), b8(tan[i, 1]), b8(tan[i, 2]), b8(bit[i, 2])])
    return out


def node_bytes(name_idx, children):
    b = struct.pack("<iI", name_idx, 0) + struct.pack("<iI", -1, 0x8000E)
    b += struct.pack("<3f", 0, 0, 0) + struct.pack("<9f", 1, 0, 0, 0, 1, 0, 0, 0, 1) + struct.pack("<f", 1.0)
    b += struct.pack("<i", -1)
    b += struct.pack("<I", len(children)) + b"".join(struct.pack("<i", c) for c in children)
    b += struct.pack("<I", 0)
    return bytearray(b)


def main():
    t = Nif(TEMPLATE)
    v, f = flint_geometry()
    center, R = hand_frame()
    v = v @ R.T + center
    vdesc = struct.unpack_from("<Q", t.blocks[90], 4 * 2 + 8 + 12 + 36 + 4 + 4 + 16 + 12)[0]
    verts = pack_vertices(v, f)
    tris = b"".join(struct.pack("<3H", *map(int, tri)) for tri in f)
    bc = v.mean(0)
    br = float(np.linalg.norm(v - bc, axis=1).max())

    shape = struct.pack("<iI", 1, 0) + struct.pack("<iI", -1, 0x8000E)
    shape += struct.pack("<3f", 0, 0, 0) + struct.pack("<9f", 1, 0, 0, 0, 1, 0, 0, 0, 1) + struct.pack("<f", 1.0) + struct.pack("<i", -1)
    shape += struct.pack("<4f", *bc, br) + struct.pack("<iii", -1, 2, -1) + struct.pack("<Q", vdesc)
    shape += struct.pack("<HHI", len(f), len(v), len(verts) + len(tris)) + verts + tris + struct.pack("<I", 0)

    shader = bytearray(t.blocks[91])
    # BSLightingShaderProperty (SSE): type, name, extra, ctrl, flags1, flags2, uv offset, uv scale,
    # texture set ref @40, emissive @44, emissive mult @56, clamp @60, alpha @64, refraction @68,
    # glossiness @72, specular color @76, specular strength @88
    struct.pack_into("<i", shader, 40, 3)
    struct.pack_into("<f", shader, 72, 60.0)   # knapped flint is glassy
    struct.pack_into("<f", shader, 88, 0.8)
    texset = struct.pack("<I", 9)
    for i in range(9):
        s = DIFFUSE if i == 0 else NORMAL if i == 1 else ""
        texset += struct.pack("<I", len(s)) + s.encode()

    blocks = [node_bytes(0, [1]), bytearray(shape), shader, bytearray(texset)]
    types = ["BSFadeNode", "BSTriShape", "BSLightingShaderProperty", "BSShaderTextureSet"]
    t.save(OUT, blocks, types, ["LTBLFlint", "LTBLFlint:0"])
    print("wrote", OUT, len(v), "verts", len(f), "tris", "radius", round(br, 2))


if __name__ == "__main__":
    main()
