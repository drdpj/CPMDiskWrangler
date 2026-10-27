#!/bin/bash
# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# fetch_real_images.sh <dir>: clones the public repositories whose real CP/M disc images the tests use.
# (Shallow, and skipping files over 5 MB.) The images belong to their owners; they are not part of this repo.
set -e
d=${1:?usage: fetch_real_images.sh <dir>}
mkdir -p "$d"; cd "$d"
for r in Myrcurial/o1next udo-munk/z80pack lesbird/H89 FujiNetWIFI/fujinet-go-h89-desktop; do
  n=$(echo $r | tr / _)
  [ -d "$n" ] || git clone -q --depth 1 --filter=blob:limit=5m https://github.com/$r.git "$n"
done
echo "fetched into $d"
