#!/bin/bash
# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# verify_independent.sh <fstest outdir>...
# Checks fstest's images with tools that share no code with this program:
#   * ImageDisk / DSK images are decoded by libdsk (dsktrans) into a raw image, put into logical track order by
#     the small Python routine below, and compared byte for byte with the logical image fstest wrote.
#   * cpmtools (cpmls, cpmcp, fsck.cpm) then reads that logical image with the same disc definition: every file
#     must match what fstest expected, and fsck.cpm must find nothing wrong.
# Needs cpmtools and libdsk-utils (Ubuntu: apt install cpmtools libdsk-utils).
fail=0
defs="$(cd "$(dirname "$0")/.." && pwd)/resources/builtin-diskdefs"   # this checkout's own definitions
for dir in "$@"; do
  for dd in "$dir"/*.diskdef; do
    name=$(basename "$dd" .diskdef); base="$dir/$name"
    img=$(ls "$base".imd "$base".dsk "$base".img 2>/dev/null | head -1)
    t=$(mktemp -d); cp "$dd" $t/diskdefs
    sides=$(awk '/^ *sides/{print $2}' "$base".diskdef)
    fmtname=$(head -1 "$dd" | awk '{print $2}')
    tracks=$(awk '/^ *tracks/{print $2}' "$dd"); sectrk=$(awk '/^ *sectrk/{print $2}' "$dd"); seclen=$(awk '/^ *seclen/{print $2}' "$dd")
    real_sides=$(grep -A30 "^diskdef $name\$" "$defs" | awk '/^ *sides/{print $2; exit}')
    case "$img" in
      *.imd|*.dsk)
        it=imd; [ "${img##*.}" = dsk ] && it=edsk
        heads=$(grep -A30 "^diskdef $name\$" "$defs" | awk '/^ *heads/{print $2; exit}')
        [ -z "$heads" ] && { [ "$tracks" -gt 86 ] && heads=2 || heads=1; }
        boot=$(awk '/^ *boottrk/{print $2}' "$dd")
        if ! python3 "$(dirname "$0")"/decode_container.py "$img" $t/py.logical "$tracks" "$sectrk" "$seclen" "${real_sides:-alt}" "$heads" "${boot:-0}" >$t/t0 2>$t/err; then
          echo "FAIL $name: the Python decoder could not read the $it file: $(tail -1 $t/err)"; fail=1; rm -rf $t; continue; fi
        want=$(awk '/^ *track0/{print "track0", $2, $3, ($4=="fm" && "'$it'"=="imd") ? "fm" : "-"}' "$dd")
        if [ -n "$want" ]; then
          got=$(cat $t/t0); [ "$it" = edsk ] && got=$(echo "$got" | awk '{print $1,$2,$3,"-"}')
          [ "$got" = "$want" ] && echo "ok   $name: track 0 is $(cat $t/t0 | cut -d' ' -f2-) in the $it file" || { echo "FAIL $name: track 0 is '$got', expected '$want'"; fail=1; }
        fi
        if cmp -s $t/py.logical "$base".logical; then echo "ok   $name: an independent decoder reads the same sectors from the $it file"; else echo "FAIL $name: the independent decoder's reading of the $it file differs"; fail=1; fi
        # libdsk too, when its geometry probe gets this disc right (it guesses some layouts wrongly)
        geo=$(dskid -type $it "$img" 2>/dev/null | awk '/Cylinders:/{c=$2}/Heads:/{h=$2}/Sectors:/{s=$2}/Sector size:/{z=$3}END{print c"/"h"/"s"/"z}')
        cyls=$(( (tracks + heads - 1) / heads ))
        if [ "$geo" = "$cyls/$heads/$sectrk/$seclen" ] && dsktrans -itype $it "$img" $t/phys.raw -otype raw >/dev/null 2>&1; then
          python3 - "$t/phys.raw" "$t/libdsk.logical" "$tracks" "$sectrk" "$seclen" "${real_sides:-alt}" "$heads" <<'PY'
import sys
src, dst, tracks, spt, sl, order, heads = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:6]), sys.argv[6], int(sys.argv[7])
b = open(src, 'rb').read(); ts = spt * sl; cyl = (tracks + heads - 1) // heads; out = bytearray()
for t in range(tracks):
    if heads == 1 or order == 'alt': c, h = t // heads, t % heads
    elif t < cyl: c, h = t, 0
    elif order == 'outback': c, h = 2 * cyl - 1 - t, 1
    else: c, h = t - cyl, 1
    p = (c * heads + h) * ts
    out += b[p:p + ts]
open(dst, 'wb').write(out)
PY
          if cmp -s $t/libdsk.logical "$base".logical; then echo "ok   $name: libdsk reads the same sectors"; else echo "FAIL $name: libdsk's reading differs"; fail=1; fi
        else
          echo "note $name: libdsk's geometry probe says $geo (expected $cyls/$heads/$sectrk/$seclen); libdsk check skipped"
        fi
        logical=$t/py.logical ;;
      *) logical="$base".logical
         if [ -z "$real_sides" ] || [ "$real_sides" = alt ]; then
           cmp -s "$img" "$base".logical && echo "ok   $name: raw image is in logical order" || { echo "FAIL $name: raw image is not the logical image"; fail=1; }
         fi ;;
    esac
    ( cd $t && fsck.cpm -n -f "$fmtname" "$logical" >$t/fsck 2>&1 ); fr=$?
    if [ $fr -ne 0 ] || grep -q -i -E "error|bad|illegal" $t/fsck; then echo "FAIL $name: fsck.cpm: $(head -3 $t/fsck | tr '\n' ' ')"; fail=1; else echo "ok   $name: fsck.cpm clean"; fi
    n=0; bad=0
    for f in $(cd "$base".expect && find . -type f | sed 's|^\./||'); do
      u=${f%%/*}; fn=${f#*/}; n=$((n+1))
      ( cd $t && cpmcp -f "$fmtname" "$logical" "$u:$fn" $t/got ) 2>$t/cperr
      if ! cmp -s $t/got "$base".expect/$f; then bad=$((bad+1)); echo "     $u:$fn differs: $(cat $t/cperr) $(stat -c %s $t/got 2>/dev/null) vs $(stat -c %s "$base".expect/$f)"; fi
      rm -f $t/got
    done
    listed=$( (cd $t && cpmls -f "$fmtname" "$logical") | grep -v -E ':$|^$' | wc -l)
    if [ $bad -eq 0 ] && [ "$listed" -eq "$n" ]; then echo "ok   $name: cpmtools lists $listed files and reads all $n exactly"; else echo "FAIL $name: cpmtools lists $listed, $bad of $n files differ"; fail=1; fi
    rm -rf $t
  done
done
exit $fail
