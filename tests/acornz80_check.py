#!/usr/bin/env python3
# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# acornz80_check.py <image.dsd> <outdir>: extracts every file of an interleaved raw Acorn Z80 CP/M disc into <outdir>.
# Independent Acorn Z80 CP/M reader, from Acorn Application Note 007 only: 80 cyl x 2 sides x 10 x 256, file
# interleaved (cyl0 h0, cyl0 h1, ...); logical tracks 0-79 = side 0 cyl 0-79, 80-159 = side 1 cyl 79-0; 3 reserved
# tracks; 512-byte logical sectors from physical pairs 0/1 4/5 8/9 2/3 6/7; 2K blocks, 128 entries (4K directory).
import sys, os
d = open(sys.argv[1], 'rb').read(); out = sys.argv[2]
pairs = [0, 4, 8, 2, 6]
def ltrack(t):
    c, h = (t, 0) if t < 80 else (159 - t, 1)
    o = (c * 2 + h) * 2560
    tr = d[o:o + 2560]
    return b''.join(tr[p * 256:(p + 2) * 256] for p in pairs)
data = b''.join(ltrack(t) for t in range(3, 160))
block = lambda n: data[n * 2048:(n + 1) * 2048]
dirb = data[:4096]
files = {}
for i in range(128):
    e = dirb[i * 32:(i + 1) * 32]
    if e[0] > 15: continue
    name = bytes(c & 0x7f for c in e[1:12])
    if any(c < 32 for c in name): print("skip entry", i, e.hex()); continue
    ext = (e[14] & 0x3f) * 32 + (e[12] & 0x1f)
    files.setdefault((e[0], name), []).append((ext, e))
for (u, name), l in files.items():
    l.sort(); buf = b''
    for ext, e in l:
        ptrs = [p for p in e[16:32] if p]
        recs = (e[12] & 1) * 128 + e[15]   # EXM = 1 for 2K blocks, DSM < 256
        chunk = b''.join(block(p) for p in ptrs)[:recs * 128]
        buf += chunk
    n = name[:8].decode().strip() + ('.' + name[8:].decode().strip() if name[8:].strip() else '')
    open(os.path.join(out, n), 'wb').write(buf)
    print(u, n, len(buf))
