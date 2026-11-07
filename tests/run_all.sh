#!/bin/bash
# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# run_all.sh <build dir> [<real images dir (fetch_real_images.sh)>] [<Acorn Disk Wrangler source dir>]
# Builds every test, runs the self-tests, the independent checks (cpmtools, libdsk, Python) and, given real
# images, detection on them, the cpmtools comparison and the GUI test. Exit status 0 only if everything passed.
src=$(cd "$(dirname "$0")/.." && pwd)
b=${1:?usage: run_all.sh <build dir> [real images] [acorn disk wrangler source]}; real=$2; adw=$3
mkdir -p "$b"; b=$(cd "$b" && pwd)
fail=0
step() { echo; echo "######## $*"; }
build() { mkdir -p "$b/$1" && (cd "$b/$1" && qmake "$2" ${3:+"$3"} >/dev/null && make -j8 >/dev/null 2>"$b/$1.log") || { echo "BUILD FAILED: $1 (see $b/$1.log)"; fail=1; }; }
step build
build app "$src/cpmdiskwrangler.pro"
for t in detecttest fstest voltest guitest screenshot mkicon chartest; do build $t "$src/tests/$t.pro"; done
build settingstest "$src/core/tests/settingstest.pro"      # in DiskWranglerCore
build xfertest "$src/core/tests/xfertest.pro"
build uitest "$src/core/tests/uitest.pro"
build devicetest "$src/core/tests/devicetest.pro"
[ -n "$adw" ] && build interop "$src/tests/interop_adw/interop_adw.pro" "ADW=$adw"
export QT_QPA_PLATFORM=offscreen
W=$(mktemp -d)
step "fstest: every format in every container, then independent checks"
for k in raw imd edsk dsk; do
  for f in ibm-3740 kpii kpiv osborne1 osb1sssd cpcdata cpcsys pcw pcw720 acorn-z80 p112 osborne-8dd vector-26x256 microbee-ss80 microbee-ds40 microbee-ds80 microbee-ds84 rm-dd rm-qd torch-cpn; do
    "$b/fstest/fstest" "$W/fs/$k" $f $k > "$W/fs-$k-$f.log" 2>&1 || { echo "FAIL fstest $f $k"; cat "$W/fs-$k-$f.log"; fail=1; }
  done
done
"$src/tests/verify_independent.sh" "$W/fs/raw" "$W/fs/imd" "$W/fs/edsk" "$W/fs/dsk" > "$W/verify.log" 2>&1 || { grep -v "^ok\|^note" "$W/verify.log"; fail=1; }
echo "independent checks: $(grep -c '^ok' "$W/verify.log") passed, $(grep -c '^FAIL' "$W/verify.log") failed"
"$b/fstest/fstest" atomic "$W/atomic" | tail -1 | grep -q ok || { echo "FAIL atomic"; fail=1; }
"$b/fstest/fstest" library | tail -1 | grep -q ok || { echo "FAIL library"; fail=1; }
"$b/fstest/fstest" complement | tail -1 | grep -q ok || { echo "FAIL complement (Sharp inverted data)"; fail=1; }
"$b/fstest/fstest" roundtrip "$W"/fs/imd/*.imd "$W"/fs/edsk/*.dsk "$W"/fs/dsk/*.dsk | grep -v "^same" && fail=1
step "Research Machines double-sided discs: a separate filesystem on each side, made new"
for k in raw imd edsk; do
  for f in rm-dd+ds rm-qd+ds; do
    "$b/fstest/fstest" "$W/fsds/$k" $f $k > "$W/fsds-$k-$f.log" 2>&1 || { echo "FAIL fstest $f $k"; cat "$W/fsds-$k-$f.log"; fail=1; }
  done
done
for f in rm-dd rm-qd; do   # independent: cpmtools reads each side of the raw file; every file back exactly
  python3 -I "$src/tests/verify_rm_sides.py" "$W/fsds/raw/$f+ds.img" "$W/fsds/raw/$f+ds.diskdef" $f "$W/rmout/$f" \
      "$W/fsds/raw/$f+ds.expect" "$W/fsds/raw/$f+ds.expect1" || { echo "FAIL independent check of $f+ds"; fail=1; }
done
step "national character sets: mappings, names, and the evidence scan"
if [ -n "$real" ] && ls "$real"/nat/*.imd >/dev/null 2>&1; then
  args=(); for z in "$real"/nat/*.imd; do args+=("$z" dk-no); done
else args=(); fi
"$b/chartest/chartest" "${args[@]}" 2>&1 | grep -E "FAIL|all ok|FAILED" ; [ "${PIPESTATUS[0]}" = 0 ] || fail=1
step "settings: installed vs AppImage (portable mode)"
"$b/settingstest/settingstest" 2>/dev/null | tail -1 | grep -q "all ok" && echo "settings: ok" || { "$b/settingstest/settingstest"; fail=1; }
step "core: transfer layer (xfertest), UI helpers and viewers (uitest), containers and saving (devicetest)"
for t in xfertest uitest devicetest; do "$b/$t/$t" 2>/dev/null | tail -1 | grep -q "all ok" && echo "$t: ok" || { "$b/$t/$t"; fail=1; }; done
step "not CP/M: Acorn DFS discs, and DFS discs with the catalogue wiped"
mkdir -p "$W/foreign" && python3 "$src/tests/make_foreign.py" "$W/foreign"
"$b/detecttest/detecttest" "$W"/foreign/* > "$W/foreign.log" 2>&1
nf=$(ls "$W"/foreign | wc -l); nn=$(grep -c 'confidence none' "$W/foreign.log")
echo "read as not CP/M: $nn of $nf"; [ "$nn" -eq "$nf" ] || { grep -B2 'confidence [lmh]' "$W/foreign.log"; fail=1; }
nh=$(grep -c 'verdict: handoff' "$W/foreign.log")          # the DFS ones are Acorn Disk Wrangler's (DiskWranglerCore v0.4.0 on)
nd=$(grep -c 'verdict: none' "$W/foreign.log"); echo "handed to Acorn Disk Wrangler: $nh, not recognised (wiped): $nd"
[ $((nh + nd)) -eq "$nf" ] && [ "$nh" -ge 1 ] || { grep 'verdict:' "$W/foreign.log"; fail=1; }
step "DiskWranglerCore's filesystem interface: Volume, Disc, detection by tiers (voltest)"
"$b/voltest/voltest" "$W/voltest" > "$W/voltest.log" 2>&1 && tail -1 "$W/voltest.log" | grep -q "all ok" && echo "voltest: ok" || { grep FAIL "$W/voltest.log"; echo "FAIL voltest"; fail=1; }
if [ -n "$real" ]; then
  O="$real/Myrcurial_o1next/research"; Z="$real/udo-munk_z80pack/cpmsim/disks/library"
  step "real images: round trip, detection, cpmtools comparison"
  "$b/fstest/fstest" roundtrip "$O"/*/*.IMD "$O"/roms/OZROM-1E/*.IMD | grep -v "^same" && fail=1
  "$b/detecttest/detecttest" "$O"/*/*.IMD "$O"/roms/OZROM-1E/*.IMD "$O"/copower88/*/*.td0 "$Z"/*.dsk \
      "$real"/lesbird_H89/disks/*.H8D $(ls "$real"/rm/*.img 2>/dev/null) > "$W/detect.log" 2>&1
  echo "detection: $(grep -c 'confidence high; writable' "$W/detect.log") known and writable, \
$(grep -c 'confidence high; read-only' "$W/detect.log") found by search (read-only until confirmed), \
$(grep -c 'confidence none' "$W/detect.log") not CP/M; $(grep -c 'check: [1-9][0-9]* errors' "$W/detect.log") with check errors; \
$(grep -c 'verdict: ask' "$W/detect.log") would ask"
  grep -q 'check: [1-9][0-9]* errors' "$W/detect.log" && fail=1
  for v in "$real"/vector/*; do        # a Vector Graphic disc: track 0 FM 26 x 128, then 26 x 256, skew 5 (not in the repo)
    [ -f "$v" ] || continue
    "$b/detecttest/detecttest" "$v" 2>&1 | grep -q 'confidence high; writable' && echo "$(basename "$v"): found, writable" || { echo "FAIL: $(basename "$v") not found"; fail=1; }
  done
  for v in "$real"/sharp/*; do         # Sharp discs: sds400 (inverted data, side 1 first) and an MZ-800 P-CP/M disc (outback, boot track on side 1)
    [ -f "$v" ] || continue
    out=$("$b/detecttest/detecttest" "$v" 2>&1); first=$(echo "$out" | grep -m1 '^ *1\. ')
    case "$v" in
      *sds400*) echo "$out" | grep -q 'confidence high; writable' && echo "$first" | grep -q 'sharp-sds400' && echo "$(basename "$v"): sharp-sds400, writable" || { echo "FAIL: $(basename "$v"): $first"; fail=1; } ;;
      *mz-*) echo "$first" | grep -q outback && echo "$(basename "$v"): outback first" || { echo "FAIL: $(basename "$v") not outback first: $first"; fail=1; } ;;
    esac
  done
  for v in "$real"/ties/*; do          # a raw image whose layouts all read the same: one row (the 35 x 2 x 16 x 256 reading), not a wall of ties
    [ -f "$v" ] || continue
    out=$("$b/detecttest/detecttest" "$v" 2>&1)
    close=$(echo "$out" | grep '^ *[0-9][0-9]*\. ' | awk 'NR==1 {top=$2} NR>1 && $2 >= top-1 {n++} END {print n+0}')
    if [ "$close" -eq 0 ] && echo "$out" | grep -q '35 cylinders, 2 sides, 16 x 256'; then echo "$(basename "$v"): one reading at the top, 16 x 256 on two sides"; else echo "FAIL: $(basename "$v") has $close other readings within a point of the best"; fail=1; fi
  done
  (cd "$W" && "$src/tests/cmp_cpmtools.sh" "$b/detecttest/detecttest" ibm-3740 "$Z"/cpm1*.dsk "$Z"/cpm22-2.dsk "$Z"/cpm22-62khd.dsk "$Z"/cpm3-2.dsk "$Z"/mpm*.dsk "$Z"/*tests.dsk) | tee "$W/cmp.log"
  grep -q "[1-9][0-9]* differ" "$W/cmp.log" && fail=1
  for z in $(ls "$real"/rm/*.img 2>/dev/null); do         # the owner's Research Machines discs: cpmtools reads both sides
    printf 'RM %s: ' "$(basename "$z")"
    k=rm-qd; [ "$(stat -c %s "$z")" = 363520 ] && k=rm-dd      # 40 cylinders a side, else 80
    rm -rf "$W/rmreal"; python3 -I "$src/tests/verify_rm_sides.py" "$z" "$W/fsds/raw/$k+ds.diskdef" $k "$W/rmreal" | tr '\n' ' ' | grep -q FAIL && { echo FAIL; fail=1; } || echo ok
  done
  for z in $(ls "$real"/acorn/*.dsd 2>/dev/null); do
    n=$(basename "$z"); rm -rf "$W/az"; mkdir -p "$W/az/py"
    "$b/detecttest/detecttest" -x "$W/az" "$z" > "$W/az.log" 2>&1
    grep -q "confidence high; writable" "$W/az.log" && grep -q "acorn-z80" "$W/az.log" || { echo "FAIL $n: not detected as acorn-z80, high, writable"; fail=1; }
    python3 "$src/tests/acornz80_check.py" "$z" "$W/az/py" > /dev/null
    d=0; for f in "$W/az/py"/*; do l=$(basename "$f" | tr A-Z a-z); cmp -s "$f" "$W/az/$n/0/$l" || { echo "FAIL $n: $l differs"; d=1; }; done
    [ $d -eq 0 ] && echo "$n: $(ls "$W/az/py" | wc -l) files match the independent Acorn Z80 reader" || fail=1
  done
  # Torch CP/N discs (the owner's, not in the repo): detected as torch-cpn, and every file as the independent reader
  # (torch_check.py, from the format notes alone) reads it; then a file added to a copy, checked the same way
  for z in $(ls "$real"/torch/*.dsd 2>/dev/null); do
    n=$(basename "$z"); rm -rf "$W/tz"; mkdir -p "$W/tz"
    "$b/detecttest/detecttest" -x "$W/tz" "$z" > "$W/tz.log" 2>&1
    grep -q "confidence high; writable" "$W/tz.log" && grep -q "torch-cpn" "$W/tz.log" || { echo "FAIL $n: not detected as torch-cpn, high, writable"; fail=1; }
    python3 "$src/tests/torch_check.py" "$z" "$W/tz/py" > "$W/tz/check" || { echo "FAIL $n: torch_check.py: $(head -3 "$W/tz/check")"; fail=1; }
    diff -r "$W/tz/py" "$W/tz/$n" > /dev/null && echo "$n: $(find "$W/tz/py" -type f | wc -l) files match the independent Torch CP/N reader" || { echo "FAIL $n: files differ from torch_check.py's"; fail=1; }
    cp "$z" "$W/tz/edit.dsd"
    "$b/fstest/fstest" addto "$W/tz/edit.dsd" ADDED.TXT 70000 > /dev/null 2>&1
    python3 "$src/tests/torch_check.py" "$W/tz/edit.dsd" "$W/tz/py2" > "$W/tz/check2"
    for f in "$W/tz/py"/*/*; do cmp -s "$f" "$W/tz/py2/${f#"$W/tz/py/"}" || { echo "FAIL $n: $(basename "$f") changed by adding a file"; fail=1; }; done
    # the new entry took the first unused or deleted slot (deleted: index address FFFF)
    python3 -c "import sys; d = open(sys.argv[1], 'rb').read(4096); e = [d[i:i + 16] for i in range(0, 4096, 16)]
n = next(i for i, x in enumerate(e) if x[5:10] == b'ADDED'); sys.exit(any(x == bytes(16) or x[:2] == b'\\xff\\xff' for x in e[:n]))" "$W/tz/edit.dsd" \
      || { echo "FAIL $n: the added file didn't take the first free directory slot"; fail=1; }
    [ -s "$W/tz/py2/0/added.txt" ] && grep -q "^ok" "$W/tz/check2" && echo "$n: a file added to a copy checks clean, the others unchanged" || { echo "FAIL $n: adding a file: $(head -3 "$W/tz/check2")"; fail=1; }
  done
  step "GUI"
  adwdir=""
  if [ -n "$adw" ]; then "$b/interop/interop_adw" make "$W/adw" >/dev/null; adwdir="--adw $W/adw"; fi
  rmimg=$(ls "$real"/rm/*.img 2>/dev/null | head -1)
  "$b/guitest/guitest" "$W/gui" "$O/shipped-software/OS1DBASE.IMD" "$O/copower88/kaypro/copwrcpm.td0" $adwdir ${rmimg:+--rm "$rmimg"} 2>/dev/null > "$W/gui.log"
  grep -E "FAIL|all ok|FAILURES" "$W/gui.log"; grep -q "^all ok" "$W/gui.log" || fail=1
  # screenshot: the main window and each dialog, on copies of the three ImageDisk discs fstest wrote above
  mkdir -p "$W/shots" "$W/shotsrc"
  for k in ibm-3740 pcw torch-cpn; do cp "$W/fs/imd/$k.imd" "$W/shotsrc/$k.imd"; done
  timeout 120 "$b/screenshot/screenshot" "$W/shots" "$W/shotsrc/ibm-3740.imd" "$W/shotsrc/pcw.imd" "$W/shotsrc/torch-cpn.imd" > "$W/shots.log" 2>&1 \
    && echo "screenshot: $(ls "$W/shots" | wc -l) PNGs" || { echo "FAIL screenshot (see $W/shots.log)"; fail=1; }
  [ -n "$adw" ] && { "$b/interop/interop_adw" read "$W/gui/out.json" | grep -q "GAME.COM" && echo "Acorn Disk Wrangler reads our drag: ok" || { echo "FAIL: Acorn Disk Wrangler could not read our drag"; fail=1; }; }
fi
echo; [ $fail -eq 0 ] && echo "ALL PASSED" || echo "SOME TESTS FAILED"
rm -rf "$W"
exit $fail
