"""Minimal SSE NIF header/block reader-writer (bsver 100)."""
import struct

class Nif:
    def __init__(self, path):
        d = open(path, 'rb').read()
        i = d.index(b'\n') + 1
        self.headline = d[:i]
        p = i
        self.ver, = struct.unpack_from('<I', d, p); p += 4
        self.endian = d[p]; p += 1
        self.uver, self.nblocks, self.bsver = struct.unpack_from('<III', d, p); p += 12
        self.meta = []
        for _ in range(3):
            l = d[p]; self.meta.append(d[p + 1:p + 1 + l]); p += 1 + l
        nt, = struct.unpack_from('<H', d, p); p += 2
        self.types = []
        for _ in range(nt):
            l, = struct.unpack_from('<I', d, p); p += 4; self.types.append(d[p:p + l].decode()); p += l
        self.btype = list(struct.unpack_from(f'<{self.nblocks}H', d, p)); p += 2 * self.nblocks
        sizes = struct.unpack_from(f'<{self.nblocks}I', d, p); p += 4 * self.nblocks
        ns, self.maxlen = struct.unpack_from('<II', d, p); p += 8
        self.strings = []
        for _ in range(ns):
            l, = struct.unpack_from('<I', d, p); p += 4; self.strings.append(d[p:p + l].decode('latin1')); p += l
        ng, = struct.unpack_from('<I', d, p); p += 4 + 4 * ng
        self.blocks = []
        for s in sizes:
            self.blocks.append(bytearray(d[p:p + s])); p += s
        self.footer = d[p:]

    def tname(self, i):
        return self.types[self.btype[i]]

    def save(self, path, blocks, types, strings, roots=(0,)):
        out = bytearray(self.headline)
        out += struct.pack('<IBIII', self.ver, self.endian, self.uver, len(blocks), self.bsver)
        for m in self.meta:
            out += bytes([len(m)]) + m
        tl = sorted(set(types), key=types.index)
        out += struct.pack('<H', len(tl))
        for t in tl:
            out += struct.pack('<I', len(t)) + t.encode()
        out += struct.pack(f'<{len(blocks)}H', *[tl.index(t) for t in types])
        out += struct.pack(f'<{len(blocks)}I', *[len(b) for b in blocks])
        out += struct.pack('<II', len(strings), max([len(s) for s in strings] + [0]))
        for s in strings:
            out += struct.pack('<I', len(s)) + s.encode('latin1')
        out += struct.pack('<I', 0)
        for b in blocks:
            out += b
        out += struct.pack('<I', len(roots)) + b''.join(struct.pack('<i', r) for r in roots)
        open(path, 'wb').write(out)
