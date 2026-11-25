"""Minimal Skyrim SE BSA (v105) reader: list and extract files."""
import struct
import sys
import os


def read_bsa(path):
    f = open(path, "rb")
    magic, ver, off, flags, nfold, nfile, folen, filen, ftypes = struct.unpack("<4sIIIIIIII", f.read(36))
    assert magic == b"BSA\x00" and ver == 105, (magic, ver)
    compressed_default = bool(flags & 0x4)
    embed_names = bool(flags & 0x100)
    folders = []
    for _ in range(nfold):
        h, count, pad, foff = struct.unpack("<QIIQ", f.read(24))
        folders.append(count)
    entries = []
    for count in folders:
        ln = f.read(1)[0]
        name = f.read(ln)[:-1].decode("latin1")
        for _ in range(count):
            h, size, foff = struct.unpack("<QII", f.read(16))
            entries.append([name, None, size, foff])
    names = f.read(filen).split(b"\x00")
    for e, n in zip(entries, names):
        e[1] = n.decode("latin1")
    return f, entries, compressed_default, embed_names


def extract(path, want, outdir):
    import lz4.frame
    f, entries, comp_def, embed = read_bsa(path)
    out = []
    for folder, name, size, foff in entries:
        full = folder + "\\" + name
        if not want(full.lower()):
            continue
        comp = comp_def ^ bool(size & 0x40000000)
        size &= 0x3FFFFFFF
        f.seek(foff)
        if embed:
            ln = f.read(1)[0]
            f.read(ln)
            size -= ln + 1
        if comp:
            orig = struct.unpack("<I", f.read(4))[0]
            data = lz4.frame.decompress(f.read(size - 4))
        else:
            data = f.read(size)
        dst = os.path.join(outdir, full.replace("\\", "/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        open(dst, "wb").write(data)
        out.append(full)
    return out


if __name__ == "__main__":
    f, entries, _, _ = read_bsa(sys.argv[1])
    pat = sys.argv[2].lower() if len(sys.argv) > 2 else ""
    for folder, name, size, foff in entries:
        full = folder + "\\" + name
        if pat in full.lower():
            print(full)
