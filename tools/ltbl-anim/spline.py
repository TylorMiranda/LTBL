"""Decode hkaSplineCompressedAnimation (Havok 2010, Skyrim) from serde-hkx XML into per-frame local transforms."""
import re
import struct
import numpy as np


def _param(xml, name):
    m = re.search(rf'<hkparam name="{name}"[^>]*>(.*?)</hkparam>', xml, re.S)
    return m.group(1).strip() if m else None


class Reader:
    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos

    def u8(self):
        v = self.d[self.p]
        self.p += 1
        return v

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]
        self.p += 2
        return v

    def f32(self):
        v = struct.unpack_from("<f", self.d, self.p)[0]
        self.p += 4
        return v

    def align(self, n):
        self.p = (self.p + n - 1) // n * n

    def bytes(self, n):
        v = self.d[self.p:self.p + n]
        self.p += n
        return v


ROT_SIZE = {0: 4, 1: 5, 2: 6, 3: 3, 4: 2, 5: 16}
ROT_ALIGN = {0: 4, 1: 1, 2: 2, 3: 1, 4: 2, 5: 4}


def read_quat(r, qt):
    r.align(ROT_ALIGN[qt])
    if qt == 1:  # THREECOMP40
        b = r.bytes(5)
        val = int.from_bytes(b, "little")
        mask = (1 << 12) - 1
        comps = [((val >> (12 * i)) & mask) for i in range(3)]
        shift = (val >> 36) & 3
        sign = (val >> 38) & 1
        frac = 1.41421356237 / 4094.0
        c = [(x - 2047) * frac for x in comps]
    elif qt == 2:  # THREECOMP48
        x, y, z = struct.unpack("<HHH", r.bytes(6))
        shift = ((y >> 14) & 2) | ((x >> 15) & 1)
        sign = (z >> 15) & 1
        mask = (1 << 15) - 1
        frac = 1.41421356237 / 32766.0
        c = [((v & mask) - 16383) * frac for v in (x, y, z)]
    elif qt == 5:
        return np.array(struct.unpack("<ffff", r.bytes(16)))
    elif qt == 0:  # POLAR32
        val = struct.unpack("<I", r.bytes(4))[0]
        raise NotImplementedError("POLAR32")
    else:
        raise NotImplementedError(f"rot quant {qt}")
    ss = sum(v * v for v in c)
    w = np.sqrt(max(0.0, 1.0 - ss))
    if sign:
        w = -w
    out = []
    it = iter(c)
    for i in range(4):
        out.append(w if i == shift else next(it))
    return np.array(out)


def find_span(n, p, u, U):
    if u >= U[n + 1]:
        return n
    if u <= U[p]:
        return p
    low, high = p, n + 1
    mid = (low + high) // 2
    while u < U[mid] or u >= U[mid + 1]:
        if u < U[mid]:
            high = mid
        else:
            low = mid
        mid = (low + high) // 2
    return mid


def eval_bspline(ctrl, knots, degree, u):
    n = len(ctrl) - 1
    span = find_span(n, degree, u, knots)
    d = [np.array(ctrl[span - degree + j], float) for j in range(degree + 1)]
    for r in range(1, degree + 1):
        for j in range(degree, r - 1, -1):
            i = span - degree + j
            denom = knots[i + degree + 1 - r] - knots[i]
            a = 0.0 if denom == 0 else (u - knots[i]) / denom
            d[j] = (1 - a) * d[j - 1] + a * d[j]
    return d[degree]


class Track3:
    """position or scale track"""

    def __init__(self, default):
        self.default = np.array(default, float)
        self.static = self.default.copy()
        self.spline = None  # (knots, degree, ctrl (n,3) with nan for non-spline axes)

    def at(self, u):
        if self.spline is None:
            return self.static
        knots, deg, ctrl = self.spline
        v = eval_bspline(ctrl, knots, deg, u)
        return np.where(self.spline_mask, v, self.static)


def read_vec_track(r, flags, sq, default):
    tr = Track3(default)
    spline_axes = [(flags >> (4 + i)) & 1 for i in range(3)]
    static_axes = [(flags >> i) & 1 for i in range(3)]
    if any(spline_axes):
        nitems = r.u16()
        deg = r.u8()
        knots = list(r.bytes(nitems + deg + 2))
        r.align(4)
        mins, maxs = [0] * 3, [0] * 3
        for i in range(3):
            if spline_axes[i]:
                mins[i] = r.f32()
                maxs[i] = r.f32()
            elif static_axes[i]:
                tr.static[i] = r.f32()
        ctrl = np.tile(tr.static, (nitems + 1, 1))
        for k in range(nitems + 1):
            for i in range(3):
                if spline_axes[i]:
                    if sq == 0:
                        q = r.u8() / 255.0
                    else:
                        q = r.u16() / 65535.0
                    ctrl[k, i] = mins[i] + (maxs[i] - mins[i]) * q
        r.align(4)
        tr.spline = (knots, deg, ctrl)
        tr.spline_mask = np.array(spline_axes, bool)
    elif any(static_axes):
        for i in range(3):
            if static_axes[i]:
                tr.static[i] = r.f32()
        r.align(4)
    return tr


class QuatTrack:
    def __init__(self):
        self.static = np.array([0, 0, 0, 1.0])
        self.spline = None

    def at(self, u):
        if self.spline is None:
            return self.static
        knots, deg, ctrl = self.spline
        v = eval_bspline(ctrl, knots, deg, u)
        return v / np.linalg.norm(v)


def read_quat_track(r, flags, qt):
    tr = QuatTrack()
    if flags & 0xF0:
        nitems = r.u16()
        deg = r.u8()
        knots = list(r.bytes(nitems + deg + 2))
        ctrl = []
        for _ in range(nitems + 1):
            ctrl.append(read_quat(r, qt))
        # keep hemisphere continuity
        for k in range(1, len(ctrl)):
            if np.dot(ctrl[k], ctrl[k - 1]) < 0:
                ctrl[k] = -ctrl[k]
        r.align(4)
        tr.spline = (knots, deg, np.array(ctrl))
    elif flags & 0x0F:
        tr.static = read_quat(r, qt)
        r.align(4)
    return tr


def decode(xml_path):
    xml = open(xml_path).read()
    ntr = int(_param(xml, "numberOfTransformTracks"))
    nfl = int(_param(xml, "numberOfFloatTracks"))
    nframes = int(_param(xml, "numFrames"))
    nblocks = int(_param(xml, "numBlocks"))
    maxf = int(_param(xml, "maxFramesPerBlock"))
    fdur = float(_param(xml, "frameDuration"))
    boffs = list(map(int, _param(xml, "blockOffsets").split()))
    data = bytes(int(x) for x in _param(xml, "data").split())
    blocks = []
    for b in range(nblocks):
        r = Reader(data, boffs[b])
        masks = [tuple(r.bytes(4)) for _ in range(ntr)]
        r.bytes(nfl)
        r.align(4)
        tracks = []
        for qtypes, pf, rf, sf in masks:
            pq = qtypes & 3
            rq = (qtypes >> 2) & 0xF
            sq = (qtypes >> 6) & 3
            pos = read_vec_track(r, pf, pq, [0, 0, 0])
            rot = read_quat_track(r, rf, rq)
            scl = read_vec_track(r, sf, sq, [1, 1, 1])
            tracks.append((pos, rot, scl))
        blocks.append(tracks)
    T = np.zeros((nframes, ntr, 3))
    Q = np.zeros((nframes, ntr, 4))
    for f in range(nframes):
        b = min(f // (maxf - 1), nblocks - 1)
        u = float(f - b * (maxf - 1))
        for i, (pos, rot, scl) in enumerate(blocks[b]):
            T[f, i] = pos.at(u)
            Q[f, i] = rot.at(u)
    return T, Q, fdur
