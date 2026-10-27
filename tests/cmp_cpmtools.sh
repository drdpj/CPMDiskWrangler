#!/bin/bash
# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# cmp_cpmtools.sh <detecttest> FORMAT IMAGE... : extract every file with cpmtools and with detecttest -x; compare

dt=$1; fmt=$2; shift 2
for img in "$@"; do
  t=$(mktemp -d); mkdir -p $t/ct
  "$dt" -x $t/us "$img" >/dev/null 2>&1
  n=0; bad=0
  for u in $(seq 0 15); do
    files=$(cpmls -f $fmt "$img" 2>/dev/null | awk -v u="$u:" '$0==u{f=1;next} /^[0-9]+:$/{f=0} f{print}')
    for f in $files; do
      n=$((n+1)); mkdir -p $t/ct/$u
      cpmcp -f $fmt "$img" "$u:$f" $t/ct/$u/$f 2>/dev/null
      if ! cmp -s $t/ct/$u/$f "$t/us/$(basename "$img")/$u/$f"; then bad=$((bad+1)); echo "  DIFFER $u:$f"; fi
    done
  done
  ours=$(find $t/us -type f | wc -l)
  echo "$(basename "$img"): cpmtools lists $n files, we extracted $ours; $bad differ"
  rm -rf $t
done
