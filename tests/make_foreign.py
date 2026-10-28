# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# make_foreign.py <dir>: Acorn DFS images (E5- and zero-filled, 40/80 track, ssd/dsd, truncated) and copies with
# the catalogue wiped, for checking that detection does not read them as empty CP/M discs.
import os, random, sys
def side(tracks, fill, nfiles, seed):
    r = random.Random(seed)
    d = bytearray([fill]) * (tracks * 10 * 256)
    s0 = bytearray(256); s1 = bytearray(256)
    s0[0:8] = b"CLOCKSP5"; s1[0:4] = b"    "
    total = tracks * 10
    s1[6] = (total >> 8) & 3; s1[7] = total & 0xFF
    sec = 2; ents = []
    for i in range(nfiles):
        ln = r.randint(300, 9000)
        body = bytes(r.randrange(256) for _ in range(ln))
        ents.append((b"PROG%02d " % i, sec, ln))
        d[sec * 256: sec * 256 + ln] = body
        sec += (ln + 255) // 256
    ents.reverse()
    for k, (nm, st, ln) in enumerate(ents):
        o = 8 + 8 * k
        s0[o:o + 7] = nm[:7]; s0[o + 7] = ord('$')
        s1[o:o + 8] = bytes([0x00, 0x19, 0x23, 0x80, ln & 255, ln >> 8 & 255,
                             ((ln >> 16 & 3) << 4) | (st >> 8 & 3), st & 255])
    s1[5] = nfiles * 8
    d[0:256] = s0; d[256:512] = s1
    return bytes(d)
out = sys.argv[1]
for fill in (0xE5, 0x00):
    tag = "e5" if fill == 0xE5 else "00"
    open(f"{out}/dfs80_{tag}.ssd", "wb").write(side(80, fill, 6, 1))
    open(f"{out}/dfs40_{tag}.ssd", "wb").write(side(40, fill, 4, 2))
    a, b = side(80, fill, 5, 3), side(80, fill, 3, 4)
    open(f"{out}/dfs80_{tag}.dsd", "wb").write(b"".join(a[t*2560:(t+1)*2560] + b[t*2560:(t+1)*2560] for t in range(80)))
    # a truncated ssd (blank tail missing)
    s = side(80, fill, 3, 5); open(f"{out}/dfs80_{tag}_trunc.ssd", "wb").write(s[:60 * 2560])
# the same discs with the catalogue blanked: data everywhere, an E5 "directory" wherever one is looked for
import random as _r
_g = _r.Random(9)
for name, tracks, heads in (("wiped40.ssd", 40, 1), ("wiped80.ssd", 80, 1), ("wiped80.dsd", 80, 2)):
    d = bytearray(b'\xe5' * (tracks * heads * 2560))
    for k in range(20):
        off = _g.randrange(512, int(len(d) * 0.6)); n = _g.randrange(200, 3000)
        d[off:off + n] = bytes(_g.randrange(256) for _ in range(n))
    d[0:512] = b'\xe5' * 512
    if heads == 2: d[2560:3072] = b'\xe5' * 512
    open(f"{out}/{name}", "wb").write(d)
