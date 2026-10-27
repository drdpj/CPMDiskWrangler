# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# decode_container.py <image.imd|image.dsk> <out.logical> <tracks> <sectrk> <seclen> <alt|outback|outout> <heads>
# An independent decoder (shares no code with the C++ program): reads an ImageDisk or (extended) CPCEMU DSK file,
# places each track's sectors in ascending sector-ID order, and writes the logical tracks in order.
import sys

def imd_tracks(b):
    i = b.index(0x1a) + 1
    t = {}
    while i < len(b):
        mode, cyl, head, ns, sz = b[i:i + 5]; i += 5
        smap = list(b[i:i + ns]); i += ns
        if head & 0x80: i += ns
        if head & 0x40: i += ns
        if sz == 0xff:
            sizes = [b[i + 2 * k] | b[i + 2 * k + 1] << 8 for k in range(ns)]; i += 2 * ns
        else:
            sizes = [128 << sz] * ns
        secs = {}
        for k in range(ns):
            ty = b[i]; i += 1
            if ty == 0: data = None
            elif ty % 2 == 0: data = bytes([b[i]]) * sizes[k]; i += 1
            else: data = b[i:i + sizes[k]]; i += sizes[k]
            secs[smap[k]] = data
        t[(cyl, head & 0x3f)] = [secs[r] for r in sorted(secs)]
    return t

def dsk_tracks(b):
    ext = b.startswith(b'EXTENDED')
    ntr, nsd = b[0x30], b[0x31]
    std = b[0x32] | b[0x33] << 8
    off = 0x100; t = {}
    for c in range(ntr):
        for h in range(nsd):
            size = b[0x34 + c * nsd + h] * 256 if ext else std
            if size == 0: continue
            assert b[off:off + 10] == b'Track-Info', 'no Track-Info at %x' % off
            ns = b[off + 0x15]
            p = off + max(0x100, (0x18 + 8 * ns + 255) // 256 * 256)
            secs = {}
            for k in range(ns):
                si = off + 0x18 + 8 * k
                r, n = b[si + 2], b[si + 3]
                ln = (b[si + 6] | b[si + 7] << 8) if ext else 128 << b[off + 0x14]
                secs[r] = b[p:p + min(ln, 128 << n)]; p += ln
            t[(c, h)] = [secs[r] for r in sorted(secs)]
            off += size
    return t

src, dst = sys.argv[1], sys.argv[2]
tracks, spt, sl, order, heads = int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), sys.argv[6], int(sys.argv[7])
b = open(src, 'rb').read()
t = imd_tracks(b) if b.startswith(b'IMD ') else dsk_tracks(b)
cyls = (tracks + heads - 1) // heads
out = bytearray()
for lt in range(tracks):
    if heads == 1 or order == 'alt': c, h = lt // heads, lt % heads
    elif lt < cyls: c, h = lt, 0
    elif order == 'outback': c, h = 2 * cyls - 1 - lt, 1
    else: c, h = lt - cyls, 1
    secs = t[(c, h)]
    assert len(secs) == spt and all(s is not None and len(s) == sl for s in secs), 'track %d/%d has the wrong shape' % (c, h)
    out += b''.join(secs)
open(dst, 'wb').write(out)
