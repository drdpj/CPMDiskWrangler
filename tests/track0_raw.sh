#!/bin/bash
# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# SPDX-License-Identifier: GPL-3.0-or-later
# track0_raw.sh <cpmdw> <work dir> [<Research Machines raw images>...]
# Discs whose format gives track 0 a shape of its own (Research Machines, Osborne 8" double density, Vector Graphic) in
# raw images (core v1.2.0, setOwnTrack0): every such library format made new as a raw image (track 0 at its own size),
# and made as ImageDisk with files on it and converted to raw, reads back with the same files; each real Research
# Machines disc (a separate disc on each side, kept side after side) goes raw -> ImageDisk -> raw and raw -> extended
# DSK -> raw byte for byte. A raw image can't say what its tracks were, so a raw disc of such a format (and of one whose
# data tracks have sector IDs of their own, Microbee DS80) saved as ImageDisk must get the tracks a new ImageDisk of the
# format has: every track's mode (FM or MFM), sector size and sector IDs the same (read by imd_tracks below, which shares
# no code with the program), and each real Research Machines disc's track 0 FM on both sides.
# --track0=padded (core v1.3.0): track 0 padded with zeros to a full track instead, every track the same length; such
# a file is detected and reads as the own-size one does, goes to ImageDisk with the same tracks, and converts back to
# the own-size file byte for byte (and the real discs too). put --format=F keeps an own-size file at its own size.
# Prints one line per disc; exit status 1 if anything failed.
c=${1:?usage: track0_raw.sh <cpmdw> <work dir> [<RM raw images>...]}; w=${2:?}; shift 2
src=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$w"; fail=0
# imd_tracks <file.imd>: one line per track, "cyl head mode size-code IDs..." (the sector map sorted)
imd_tracks() {
  python3 -I -c '
import sys
b = open(sys.argv[1], "rb").read(); i = b.index(0x1a) + 1
while i < len(b):
    mode, cyl, head, ns, sz = b[i:i + 5]; i += 5
    ids = list(b[i:i + ns]); i += ns
    if head & 0x80: i += ns
    if head & 0x40: i += ns
    sizes = [128 << sz] * ns
    if sz == 0xff: sizes = [b[i + 2 * k] | b[i + 2 * k + 1] << 8 for k in range(ns)]; i += 2 * ns
    for k in range(ns):
        ty = b[i]; i += 1
        if ty: i += 1 if ty % 2 == 0 else sizes[k]
    print(cyl, head & 0x3f, mode, sz, *sorted(ids))
' "$1"
}
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
for f in rm-dd rm-qd osborne-8dd vector-26x256 microbee-ds80; do
  n="$w/shape-$f"
  timeout 60 "$c" new "$n.imd" --format=$f --container=imd --replace >/dev/null &&
  timeout 60 "$c" new "$n.img" --format=$f --container=raw --replace >/dev/null &&
  sz=$(stat -c %s "$n.img")
  timeout 60 "$c" put "$n.img" "$src/README.md" 0: --format=$f >/dev/null &&
  [ "$(stat -c %s "$n.img")" = "$sz" ] || { echo "FAIL $f: put --format changed the raw image's size from $sz to $(stat -c %s "$n.img")"; fail=1; continue; }
  timeout 60 "$c" convert "$n.img" "$n-back.imd" --container=imd --format=$f --replace >/dev/null 2>"$n.err" || { echo "FAIL $f: raw -> ImageDisk: $(cat "$n.err")"; fail=1; continue; }
  imd_tracks "$n.imd" > "$n.want"; imd_tracks "$n-back.imd" > "$n.got"
  cmp -s "$n.want" "$n.got" || { echo "FAIL $f: raw -> ImageDisk hasn't the tracks a new ImageDisk of $f has: $(diff "$n.want" "$n.got" | head -2 | tr '\n' ' ')"; fail=1; continue; }
  timeout 60 "$c" convert "$n-back.imd" "$n-back.img" --container=raw --replace >/dev/null && cmp -s "$n.img" "$n-back.img" || { echo "FAIL $f: raw -> ImageDisk -> raw is not the same file"; fail=1; continue; }
  echo "$f: raw -> ImageDisk has a new ImageDisk's tracks ($(awk '{print $3}' "$n.got" | sort -u | tr '\n' ' ')modes), and back to raw byte for byte"
done
for f in rm-dd rm-qd osborne-8dd vector-26x256; do
  n="$w/pad-$f"; own="$w/shape-$f.img"           # own: the raw image with README.md from the loop above
  timeout 60 "$c" new "$n-new.img" --format=$f --container=raw --track0=padded --replace >/dev/null 2>"$n.err" || { echo "FAIL $f: new --track0=padded: $(cat "$n.err")"; fail=1; continue; }
  timeout 60 "$c" check "$n-new.img" --format=$f >/dev/null 2>&1 || { echo "FAIL $f: the new padded raw image fails its check"; fail=1; continue; }
  full=$(python3 -I -c 'import sys; print(int(sys.argv[1]) * int(sys.argv[2]))' "$(timeout 60 "$c" convert "$n-new.img" "$n-x.imd" --container=imd --format=$f --replace >/dev/null && imd_tracks "$n-x.imd" | wc -l)" \
         "$(imd_tracks "$w/shape-$f.imd" | awk '$1 == 1 && $2 == 0 {print (NF - 4) * (128 * 2 ^ $4)}')")
  [ "$(stat -c %s "$n-new.img")" = "$full" ] || { echo "FAIL $f: the new padded raw image is $(stat -c %s "$n-new.img") bytes, not every track full ($full)"; fail=1; continue; }
  timeout 60 "$c" convert "$own" "$n.img" --container=raw --track0=padded --replace >/dev/null 2>"$n.err" || { echo "FAIL $f: own -> padded: $(cat "$n.err")"; fail=1; continue; }
  [ "$(stat -c %s "$n.img")" = "$full" ] || { echo "FAIL $f: own -> padded is $(stat -c %s "$n.img") bytes, not $full"; fail=1; continue; }
  t0=$(imd_tracks "$w/shape-$f.imd" | awk '$1 == 0 && $2 == 0 {print (NF - 4) * (128 * 2 ^ $4)}')
  python3 -I -c 'import sys; b = open(sys.argv[1], "rb").read(); t = int(sys.argv[2]); full = int(sys.argv[3]) // int(sys.argv[4]); sys.exit(0 if b[t:full] == bytes(full - t) else 1)' \
    "$n.img" "$t0" "$full" "$(imd_tracks "$w/shape-$f.imd" | wc -l)" || { echo "FAIL $f: track 0's padding isn't zeros"; fail=1; continue; }
  # A padded single-sided 80-track rm-qd is the size of a padded two-sided 40-track rm-dd kept side after side, and
  # nothing in a raw file tells them apart (a blank second side or the rest of one side): detected as rm-qd, but only
  # likely (read-only until the format is named). An own-size rm-qd (366080 bytes) has no such twin (363520).
  conf=certain; [ $f = rm-qd ] && conf=likely
  timeout 60 "$c" info "$n.img" | grep -q "^detection: open: CP/M disc ($f) ($conf)" || { echo "FAIL $f: the padded raw image isn't detected as $f ($conf)"; fail=1; continue; }
  [ "$(timeout 60 "$c" ls "$n.img" --format=$f)" = "$(timeout 60 "$c" ls "$own" --format=$f)" ] || { echo "FAIL $f: the padded raw image lists other files"; fail=1; continue; }
  timeout 60 "$c" convert "$n.img" "$n.imd" --container=imd --format=$f --replace >/dev/null && imd_tracks "$n.imd" > "$n.got" &&
    cmp -s "$w/shape-$f.want" "$n.got" || { echo "FAIL $f: padded -> ImageDisk hasn't a new ImageDisk's tracks"; fail=1; continue; }
  timeout 60 "$c" convert "$n.img" "$n-own.img" --container=raw --track0=own --format=$f --replace >/dev/null && cmp -s "$own" "$n-own.img" || { echo "FAIL $f: padded -> own size is not the own-size file"; fail=1; continue; }
  timeout 60 "$c" convert "$n.imd" "$n-2.img" --container=raw --track0=padded --replace >/dev/null && cmp -s "$n.img" "$n-2.img" || { echo "FAIL $f: ImageDisk -> padded is not the padded file"; fail=1; continue; }
  timeout 60 "$c" convert "$n.img" "$n-3.img" --container=raw --format=$f --replace >/dev/null && cmp -s "$n.img" "$n-3.img" || { echo "FAIL $f: padded -> raw (no --track0) doesn't keep it padded"; fail=1; continue; }
  echo "$f: --track0=padded ($full bytes, track 0's $t0 bytes then zeros): detected ($conf), the same files, ImageDisk's tracks, and back to own size byte for byte"
done
# A disc whose format isn't in the library (a diskdef of its own: track 0 16 x 128 FM, then 10 x 512), opened
# without that diskdef: the reading records track 0's shape (FM, as a raw file can't say), so raw -> ImageDisk gives
# track 0 FM at its own size and back to raw is byte for byte; and its ImageDisk copy goes to a padded raw image.
u="$w/unknown"; mkdir -p "$u"
printf 'diskdef t0test\n  seclen 512\n  tracks 40\n  sectrk 10\n  blocksize 2048\n  maxdir 64\n  skew 1\n  boottrk 2\n  os 2.2\n  track0 16 128 fm\nend\n' > "$u/t0.diskdef"
if timeout 60 "$c" new "$u/u.img" --diskdefs="$u/t0.diskdef" --format=t0test --container=raw --replace >/dev/null &&
   timeout 60 "$c" put "$u/u.img" "$src/README.md" "$src/AGENTS.md" 0: --diskdefs="$u/t0.diskdef" --format=t0test >/dev/null &&
   timeout 60 "$c" info "$u/u.img" | grep -q "boot tracks (track 0: 16 x 128 FM)" &&
   timeout 60 "$c" convert "$u/u.img" "$u/u.imd" --container=imd --replace >/dev/null &&
   [ "$(imd_tracks "$u/u.imd" | awk '$1 == 0 {print $3, NF - 4, $4}')" = "2 16 0" ] &&
   timeout 60 "$c" convert "$u/u.imd" "$u/back.img" --container=raw --replace >/dev/null && cmp -s "$u/u.img" "$u/back.img" &&
   timeout 60 "$c" convert "$u/u.imd" "$u/pad.img" --container=raw --track0=padded --replace >/dev/null &&
   [ "$(stat -c %s "$u/pad.img")" = 204800 ] && [ "$(timeout 60 "$c" ls "$u/pad.img")" = "$(timeout 60 "$c" ls "$u/u.img")" ]; then
  echo "a format not in the library with its own track 0: read with track 0 16 x 128 FM; raw -> ImageDisk (track 0 FM) -> raw byte for byte; padded"
else echo "FAIL a format not in the library with its own track 0: $(timeout 60 "$c" info "$u/u.img" | sed -n 4,5p | tr '\n' ' ')"; fail=1; fi
timeout 60 "$c" new "$w/x.imd" --format=rm-dd --container=imd --track0=padded --replace >/dev/null 2>&1; [ $? = 2 ] || { echo "FAIL --track0 with --container=imd isn't a usage error"; fail=1; }
for z in "$@"; do
  n="$w/$(basename "$z" .img)"; cp "$z" "$n.img"
  ok=1
  for k in imd edsk; do
    timeout 60 "$c" convert "$n.img" "$n.$k" --container=$k --replace >/dev/null &&
    timeout 60 "$c" convert "$n.$k" "$n-$k.img" --container=raw --replace >/dev/null 2>"$n.err" &&
    cmp -s "$z" "$n-$k.img" || { echo "FAIL $(basename "$z"): raw -> $k -> raw is not the same file $(cat "$n.err")"; fail=1; ok=0; }
  done
  timeout 60 "$c" convert "$n.img" "$n-pad.img" --container=raw --track0=padded --replace >/dev/null &&
  # a disc on each side, a catalogue on each: the padded file is detected (certain) as the own-size one is
  timeout 60 "$c" info "$n-pad.img" | grep -q "^detection: open: CP/M disc ([a-z0-9-]*) (certain)" &&
  [ "$(timeout 60 "$c" ls "$n-pad.img" --vol=side1)" = "$(timeout 60 "$c" ls "$n.img" --vol=side1)" ] &&
  timeout 60 "$c" convert "$n-pad.img" "$n-pad.imd" --container=imd --replace >/dev/null &&
  timeout 60 "$c" convert "$n-pad.img" "$n-own.img" --container=raw --track0=own --replace >/dev/null &&
  cmp -s "$z" "$n-own.img" && [ "$(imd_tracks "$n.imd")" = "$(imd_tracks "$n-pad.imd")" ] || { echo "FAIL $(basename "$z"): raw -> padded -> ImageDisk / -> raw isn't the same disc"; fail=1; ok=0; }
  t0=$(imd_tracks "$n.imd" | awk '$1 == 0 {printf "%s ", $3}'); rest=$(imd_tracks "$n.imd" | awk '$1 > 0 {print $3}' | sort -u | tr '\n' ' ')
  case "$t0" in [012]" "[012]" ") ;; *) echo "FAIL $(basename "$z"): ImageDisk track 0 modes are '$t0', not FM on both sides"; fail=1; ok=0;; esac
  case "$rest" in [345]" ") ;; *) echo "FAIL $(basename "$z"): ImageDisk data tracks' modes are '$rest', not one MFM mode"; fail=1; ok=0;; esac
  [ $ok = 1 ] && echo "$(basename "$z"): raw -> ImageDisk (track 0 FM, modes $t0, the rest $rest) -> raw and raw -> extended DSK -> raw, byte for byte; padded: detected (certain), both sides' files, and back"
done
exit $fail
