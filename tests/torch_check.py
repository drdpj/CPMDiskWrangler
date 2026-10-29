#!/usr/bin/env python3
# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# torch_check.py <image> <outdir>: an independent reader of Torch CP/N discs, written from the format notes alone
# (not from this program's code). Extracts every file into <outdir>/<user>/<name in lower case>, and checks the disc:
# every index chain sound, no sector used twice, the allocation map marking exactly the sectors in use (and the first
# two tracks), the record flags. Prints "ok" or the problems; exit status 1 on problems.
#
# The notes: 80 cylinders x 2 sides x 10 x 256 bytes, the image cylinder by cylinder (cyl 0 side 0, cyl 0 side 1, ...).
# A sector address is a 16-bit word: cylinder (11 bits), side (1 bit), sector (4 bits). The directory is cylinder 0
# side 0 sectors 0-9 and side 1 sectors 0-5: 16-byte entries, little-endian words - bytes 0-1 the address of the
# file's L2 or L3 index sector (bit 15 set: L2), bytes 2-3 the highest record number, byte 4 the user, bytes 5-12
# the name and 13-15 the extension (7 bits each; bit 7 attributes). Side 1 sectors 6-7: the allocation map, 256
# words. What the real discs showed besides (and this reader assumes): an L3 sector lists data sectors, an L2
# lists L3 sectors; data pointers carry 0x4000 / 0x8000 for the first / second 128-byte record; map word
# (cyl * 2 + side), bit (sector); empty entries are all zero; words 160-255 FC00 or
# FFFF; an entry with address FFFF is a deleted file (its
# sectors free in the map; noted, not extracted).
import os, sys

img = open(sys.argv[1], 'rb').read()
out = sys.argv[2]
problems = []
notes = []

def sector(cyl, side, sec):
    o = ((cyl * 2 + side) * 10 + sec) * 256
    return img[o:o + 256]

def addr(w):
    w &= 0x3FFF
    return (w >> 5, (w >> 4) & 1, w & 15)

def words(b):
    return [b[i] | b[i + 1] << 8 for i in range(0, len(b), 2)]

directory = b''.join(sector(0, 0, s) for s in range(10)) + b''.join(sector(0, 1, s) for s in range(6))
used = {}
for s in range(10): used[(0, 0, s)] = 'directory'
for s in range(10): used[(0, 1, s)] = 'directory/map'

def claim(a, who):
    if a in used: problems.append(f"{who}: sector {a} is also used by {used[a]}")
    used[a] = who

def ok(a, who, what):
    c, h, s = a
    if c >= 80 or s >= 10: problems.append(f"{who}: {what} {a} is off the disc"); return False
    if c == 0: problems.append(f"{who}: {what} {a} is in the directory cylinder"); return False
    return True

count = 0
for i in range(256):
    e = directory[i * 16:(i + 1) * 16]
    if e == bytes(16): continue
    first = e[0] | e[1] << 8
    high = e[2] | e[3] << 8
    user = e[4]
    name = bytes((c & 0x7F) or 0x20 for c in e[5:13]).decode('latin1').strip()
    ext = bytes((c & 0x7F) or 0x20 for c in e[13:16]).decode('latin1').strip()
    full = name + ('.' + ext if ext else '')
    who = f"{user}:{full}"
    if first == 0xFFFF:                     # a deleted file: its sectors are free
        notes.append(f"{who}: deleted ({high + 1} records)")
        continue
    if user > 31 or not name or any(ch < ' ' for ch in full):
        problems.append(f"directory entry {i} is not a file entry: {e.hex(' ')}"); continue
    count += 1
    data = b''
    records = high + 1
    top = addr(first)
    if not ok(top, who, "index sector"): continue
    claim(top, who)
    l3s = [top]
    if first & 0x8000:
        l3s = []
        for p in words(sector(*top)):
            if not p: break
            if p & 0xC000: problems.append(f"{who}: an L2 pointer has flag bits")
            a = addr(p)
            if not ok(a, who, "L3 sector"): continue
            claim(a, who)
            l3s.append(a)
    ptrs = []
    for l3 in l3s:
        for p in words(sector(*l3)):
            if not p: break
            ptrs.append(p)
    if len(ptrs) != (records + 1) // 2:
        problems.append(f"{who}: {len(ptrs)} data sectors for {records} records")
    for k, p in enumerate(ptrs):
        want = 0x4000 if k == len(ptrs) - 1 and records % 2 else 0xC000
        if p & 0xC000 != want: problems.append(f"{who}: data pointer {k} has flags {p & 0xC000:04x}, expected {want:04x}")
        a = addr(p)
        if not ok(a, who, "data sector"): continue
        claim(a, who)
        data += sector(*a)
    data = data[:records * 128]
    os.makedirs(os.path.join(out, str(user)), exist_ok=True)
    open(os.path.join(out, str(user), full.lower()), 'wb').write(data)

mapb = sector(0, 1, 6) + sector(0, 1, 7)
mw = words(mapb)
for t in range(256):
    if mw[t] & 0xFC00 != 0xFC00 or (t >= 160 and mw[t] not in (0xFC00, 0xFFFF)):
        problems.append(f"map word {t} is {mw[t]:04x}")
        continue
    if t >= 160: continue
    for s in range(10):
        a = (t >> 1, t & 1, s)
        marked = bool(mw[t] >> s & 1)
        if marked != (a in used):
            problems.append(f"map: sector {a} is {'marked' if marked else 'not marked'} but {'in use' if a in used else 'free'}")

for n in notes: print('note:', n)
for p in problems[:30]: print(p)
print("ok" if not problems else f"{len(problems)} problems", f"({count} files)")
sys.exit(1 if problems else 0)
