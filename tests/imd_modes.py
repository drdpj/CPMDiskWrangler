# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# imd_modes.py <image.imd> <mode> <cylinders> <heads> <sectors> <sector size>
# An independent check (shares no code with the program) of an ImageDisk file's track records: there must be exactly
# cylinders x heads tracks, each with the given mode (0/1/2 FM 500/300/250 kbps, 3/4/5 MFM 500/300/250 kbps) and the
# given number of sectors of the given size. Prints one line; exit status 0 only if every track is as expected
# (issue #5: new single-density discs were written with MFM modes).
import sys

path, want, cyls, heads, nsec, size = sys.argv[1], *map(int, sys.argv[2:7])
b = open(path, 'rb').read()
if not b.startswith(b'IMD '):
    sys.exit('FAIL %s: not an ImageDisk file' % path)
i = b.index(0x1a) + 1
seen, bad = set(), []
while i < len(b):
    mode, cyl, head, ns, sz = b[i:i + 5]; i += 5
    i += ns                                              # sector numbering map
    if head & 0x80: i += ns                              # cylinder map
    if head & 0x40: i += ns                              # head map
    if sz == 0xff:
        sizes = [b[i + 2 * k] | b[i + 2 * k + 1] << 8 for k in range(ns)]; i += 2 * ns
    else:
        sizes = [128 << sz] * ns
    for k in range(ns):
        ty = b[i]; i += 1
        if ty == 0: continue                             # no data
        i += 1 if ty % 2 == 0 else sizes[k]              # compressed (one byte) or the whole sector
    h = head & 0x3f
    seen.add((cyl, h))
    if mode != want or ns != nsec or any(s != size for s in sizes):
        bad.append('cylinder %d head %d: mode %d, %d x %s' % (cyl, h, mode, ns, '/'.join(sorted({str(s) for s in sizes}))))
expected = {(c, h) for c in range(cyls) for h in range(heads)}
if seen != expected:
    bad.append('tracks %d, expected %d cylinders x %d heads' % (len(seen), cyls, heads))
kind = 'FM' if want <= 2 else 'MFM'
if bad:
    print('FAIL %s: %d problem(s), first: %s' % (path, len(bad), bad[0]))
    sys.exit(1)
print('ok   %s: %d tracks, mode %d (%s), %d x %d' % (path, len(seen), want, kind, nsec, size))
