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
for t in detecttest fstest guitest mkicon; do build $t "$src/tests/$t.pro"; done
[ -n "$adw" ] && build interop "$src/tests/interop_adw/interop_adw.pro" "ADW=$adw"
export QT_QPA_PLATFORM=offscreen
W=$(mktemp -d)
step "fstest: every format in every container, then independent checks"
for k in raw imd edsk; do
  for f in ibm-3740 kpii kpiv osborne1 osb1sssd cpcdata cpcsys pcw pcw720 acorn-z80 p112 osborne-8dd rm-dd rm-qd; do
    "$b/fstest/fstest" "$W/fs/$k" $f $k > "$W/fs-$k-$f.log" 2>&1 || { echo "FAIL fstest $f $k"; cat "$W/fs-$k-$f.log"; fail=1; }
  done
done
"$src/tests/verify_independent.sh" "$W/fs/raw" "$W/fs/imd" "$W/fs/edsk" > "$W/verify.log" 2>&1 || { grep -v "^ok\|^note" "$W/verify.log"; fail=1; }
echo "independent checks: $(grep -c '^ok' "$W/verify.log") passed, $(grep -c '^FAIL' "$W/verify.log") failed"
"$b/fstest/fstest" atomic "$W/atomic" | tail -1 | grep -q ok || { echo "FAIL atomic"; fail=1; }
"$b/fstest/fstest" library | tail -1 | grep -q ok || { echo "FAIL library"; fail=1; }
"$b/fstest/fstest" roundtrip "$W"/fs/imd/*.imd "$W"/fs/edsk/*.dsk | grep -v "^same" && fail=1
step "not CP/M: Acorn DFS discs, and DFS discs with the catalogue wiped"
mkdir -p "$W/foreign" && python3 "$src/tests/make_foreign.py" "$W/foreign"
"$b/detecttest/detecttest" "$W"/foreign/* > "$W/foreign.log" 2>&1
nf=$(ls "$W"/foreign | wc -l); nn=$(grep -c 'confidence none' "$W/foreign.log")
echo "read as not CP/M: $nn of $nf"; [ "$nn" -eq "$nf" ] || { grep -B2 'confidence [lmh]' "$W/foreign.log"; fail=1; }
if [ -n "$real" ]; then
  O="$real/Myrcurial_o1next/research"; Z="$real/udo-munk_z80pack/cpmsim/disks/library"
  step "real images: round trip, detection, cpmtools comparison"
  "$b/fstest/fstest" roundtrip "$O"/*/*.IMD "$O"/roms/OZROM-1E/*.IMD | grep -v "^same" && fail=1
  "$b/detecttest/detecttest" "$O"/*/*.IMD "$O"/roms/OZROM-1E/*.IMD "$O"/copower88/*/*.td0 "$Z"/*.dsk \
      "$real"/lesbird_H89/disks/*.H8D $(ls "$real"/rm/*.img 2>/dev/null) > "$W/detect.log" 2>&1
  echo "detection: $(grep -c 'confidence high; writable' "$W/detect.log") known and writable, \
$(grep -c 'confidence high; read-only' "$W/detect.log") found by search (read-only until confirmed), \
$(grep -c 'confidence none' "$W/detect.log") not CP/M; $(grep -c 'check: [1-9][0-9]* errors' "$W/detect.log") with check errors"
  grep -q 'check: [1-9][0-9]* errors' "$W/detect.log" && fail=1
  (cd "$W" && "$src/tests/cmp_cpmtools.sh" "$b/detecttest/detecttest" ibm-3740 "$Z"/cpm1*.dsk "$Z"/cpm22-2.dsk "$Z"/cpm22-62khd.dsk "$Z"/cpm3-2.dsk "$Z"/mpm*.dsk "$Z"/*tests.dsk) | tee "$W/cmp.log"
  grep -q "[1-9][0-9]* differ" "$W/cmp.log" && fail=1
  step "GUI"
  adwdir=""
  if [ -n "$adw" ]; then "$b/interop/interop_adw" make "$W/adw" >/dev/null; adwdir="--adw $W/adw"; fi
  rmimg=$(ls "$real"/rm/*.img 2>/dev/null | head -1)
  "$b/guitest/guitest" "$W/gui" "$O/shipped-software/OS1DBASE.IMD" "$O/copower88/kaypro/copwrcpm.td0" $adwdir ${rmimg:+--rm "$rmimg"} 2>/dev/null | grep -E "FAIL|all ok|FAILURES" || fail=1
  [ -n "$adw" ] && { "$b/interop/interop_adw" read "$W/gui/out.json" | grep -q "GAME.COM" && echo "Acorn Disk Wrangler reads our drag: ok" || { echo "FAIL: Acorn Disk Wrangler could not read our drag"; fail=1; }; }
fi
echo; [ $fail -eq 0 ] && echo "ALL PASSED" || echo "SOME TESTS FAILED"
rm -rf "$W"
exit $fail
