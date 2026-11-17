#!/bin/bash
# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# SPDX-License-Identifier: GPL-3.0-or-later
# track0_raw.sh <cpmdw> <work dir> [<Research Machines raw images>...]
# Discs whose format gives track 0 a shape of its own (Research Machines, Osborne 8" double density, Vector Graphic) in
# raw images (core v1.2.0, setOwnTrack0): every such library format made new as a raw image (track 0 at its own size),
# and made as ImageDisk with files on it and converted to raw, reads back with the same files; each real Research
# Machines disc (a separate disc on each side, kept side after side) goes raw -> ImageDisk -> raw and raw -> extended
# DSK -> raw byte for byte. Prints one line per disc; exit status 1 if anything failed.
c=${1:?usage: track0_raw.sh <cpmdw> <work dir> [<RM raw images>...]}; w=${2:?}; shift 2
src=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$w"; fail=0
for f in rm-dd rm-qd osborne-8dd vector-26x256; do
  n="$w/$f"
  timeout 60 "$c" new "$n-new.img" --format=$f --container=raw --replace >/dev/null 2>"$n.err" || { echo "FAIL $f: new raw: $(cat "$n.err")"; fail=1; continue; }
  timeout 60 "$c" check "$n-new.img" --format=$f >/dev/null 2>&1 || { echo "FAIL $f: the new raw image fails its check"; fail=1; }
  timeout 60 "$c" new "$n.imd" --format=$f --container=imd --replace >/dev/null &&
  timeout 60 "$c" put "$n.imd" "$src/README.md" "$src/AGENTS.md" 0: --format=$f >/dev/null &&
  timeout 60 "$c" convert "$n.imd" "$n.img" --container=raw --format=$f --replace >/dev/null 2>"$n.err" || { echo "FAIL $f: to raw: $(cat "$n.err")"; fail=1; continue; }
  timeout 60 "$c" info "$n.img" | grep -q "^detection: open: CP/M disc ($f) (certain)" || { echo "FAIL $f: the raw image isn't read as $f"; fail=1; }
  [ "$(timeout 60 "$c" ls "$n.imd" --format=$f)" = "$(timeout 60 "$c" ls "$n.img")" ] || { echo "FAIL $f: the raw image lists other files"; fail=1; }
  rm -r "$n-a" "$n-b" 2>/dev/null; mkdir -p "$n-a" "$n-b"
  timeout 60 "$c" get "$n.imd" '0:*' "$n-a" --format=$f >/dev/null && timeout 60 "$c" get "$n.img" '0:*' "$n-b" >/dev/null &&
    diff -r "$n-a" "$n-b" >/dev/null && [ "$(ls "$n-a" | wc -l)" = 2 ] || { echo "FAIL $f: the raw image's files differ"; fail=1; continue; }
  echo "$f: new as raw ($(stat -c %s "$n-new.img") bytes, track 0 at its own size); ImageDisk with 2 files -> raw: read as $f, the same files"
done
for z in "$@"; do
  n="$w/$(basename "$z" .img)"; cp "$z" "$n.img"
  ok=1
  for k in imd edsk; do
    timeout 60 "$c" convert "$n.img" "$n.$k" --container=$k --replace >/dev/null &&
    timeout 60 "$c" convert "$n.$k" "$n-$k.img" --container=raw --replace >/dev/null 2>"$n.err" &&
    cmp -s "$z" "$n-$k.img" || { echo "FAIL $(basename "$z"): raw -> $k -> raw is not the same file $(cat "$n.err")"; fail=1; ok=0; }
  done
  [ $ok = 1 ] && echo "$(basename "$z"): raw -> ImageDisk -> raw and raw -> extended DSK -> raw, byte for byte"
done
exit $fail
