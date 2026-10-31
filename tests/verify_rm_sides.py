#!/usr/bin/env python3
# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# verify_rm_sides.py <raw image> <diskdef file> <diskdef name> <outdir> [<expect dir side 0> [<expect dir side 1>]]
# Independent check of a Research Machines double-sided raw image (a separate CP/M disc on each side, side 0 then
# side 1 in the file, each side starting with a 16 x 128 FM track 0): each side is cut out, its first track padded to
# the 9 x 512 bytes cpmtools expects of it, and cpmtools (cpmls, cpmcp, fsck.cpm) reads it with the disc definition.
# Files are written to <outdir>/side0 and <outdir>/side1; with expect directories every file must match exactly.
# Needs cpmtools. Shares no code with the program.
import os, subprocess, sys, tempfile, shutil

img, defs, name, out = sys.argv[1:5]
expect = sys.argv[5:7]
b = open(img, 'rb').read()
T0, TRK = 16 * 128, 9 * 512                      # track 0 as stored; every other track
cyls = (len(b) // 2 - T0) // TRK + 1             # cylinders per side
side_len = T0 + (cyls - 1) * TRK
if len(b) != 2 * side_len:
    sys.exit("FAIL: %d bytes is not two sides of %d cylinders" % (len(b), cyls))
fail = 0
tmp = tempfile.mkdtemp()
shutil.copy(defs, os.path.join(tmp, 'diskdefs'))
for side in (0, 1):
    raw = b[side * side_len:(side + 1) * side_len]
    logical = b'\xe5' * TRK + raw[T0:]           # track 0 padded to a 9 x 512 track (its contents are not read)
    path = os.path.join(tmp, 'side%d.img' % side)
    open(path, 'wb').write(logical)
    env = dict(os.environ, CPMTOOLSFMT='')
    run = lambda *a: subprocess.run(a, cwd=tmp, capture_output=True, text=True)
    fs = run('fsck.cpm', '-n', '-f', name, path)
    bad = fs.returncode != 0 or any(w in fs.stdout.lower() + fs.stderr.lower() for w in ('error', 'bad', 'illegal'))
    print('%s side %d: fsck.cpm %s' % ('FAIL' if bad else 'ok  ', side, 'reports problems: ' + (fs.stdout + fs.stderr).strip()[:200] if bad else 'clean'))
    fail |= bad
    ls = run('cpmls', '-f', name, path)
    listed = [l.split()[0] for l in ls.stdout.splitlines() if l.strip() and not l.rstrip().endswith(':')]
    d = os.path.join(out, 'side%d' % side)
    os.makedirs(d, exist_ok=True)
    print('     side %d: cpmtools lists %d file(s)' % (side, len(listed)))
    if len(expect) > side and expect[side]:
        want = {}
        for root, _, files in os.walk(expect[side]):
            for fn in files:
                want['%s:%s' % (os.path.basename(root), fn)] = open(os.path.join(root, fn), 'rb').read()
        diff = []
        for key, data in sorted(want.items()):
            dst = os.path.join(d, key.replace(':', '_'))
            r = run('cpmcp', '-f', name, path, key, dst)
            if r.returncode != 0 or not os.path.exists(dst) or open(dst, 'rb').read() != data:
                diff.append(key)
        ok = not diff and len(want) == len(listed)
        print('%s side %d: cpmtools lists %d files and reads all %d exactly%s' % ('ok  ' if ok else 'FAIL', side, len(listed), len(want), '' if ok else ' (differ: %s)' % diff))
        fail |= not ok
shutil.rmtree(tmp)
sys.exit(1 if fail else 0)
