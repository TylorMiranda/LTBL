"""Minimal TES5 plugin reader: iterate records with EDID and MODL/other subrecords."""
import struct
import zlib


def records(path, types=None):
    d = open(path, "rb").read()
    p = 0
    n = len(d)

    def walk(p, end):
        while p < end:
            typ = d[p:p + 4]
            size = struct.unpack_from("<I", d, p + 4)[0]
            if typ == b"GRUP":
                yield from walk(p + 24, p + size)
                p += size
                continue
            flags, fid = struct.unpack_from("<II", d, p + 8)
            body = d[p + 24:p + 24 + size]
            p += 24 + size
            if types and typ.decode() not in types:
                continue
            if flags & 0x40000:
                try:
                    body = zlib.decompress(body[4:])
                except Exception:
                    continue
            subs = {}
            q = 0
            big = None
            while q < len(body):
                st = body[q:q + 4].decode("latin1")
                sz = struct.unpack_from("<H", body, q + 4)[0]
                if st == "XXXX":
                    big = struct.unpack_from("<I", body, q + 6)[0]
                    q += 6 + sz
                    continue
                if big is not None:
                    sz = big
                    big = None
                subs.setdefault(st, []).append(body[q + 6:q + 6 + sz])
                q += 6 + sz
            yield typ.decode(), fid, subs

    yield from walk(0, n)


def zstr(b):
    return b.split(b"\x00")[0].decode("latin1")
