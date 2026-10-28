#!/usr/bin/env bash
# Shared, unchanged, by Acorn Disk Wrangler and CP/M Disk Wrangler.
# Copyright (C) 2026 Daniel Jameson. GPL v3 or later.
#
# Builds a release and deploys it with Qt's shared libraries, ready to run on another machine:
#
#   ci/build.sh windows    MSYS2 MINGW64 (or UCRT64) shell  -> deploy/          (exe, DLLs, plugins)
#   ci/build.sh macos      Homebrew qt@5 on the PATH        -> build/<app>.app  (frameworks inside)
#   ci/build.sh linux      Debian/Ubuntu qtbase5-dev,       -> build/AppDir     (for an AppImage)
#                          linuxdeploy + its Qt plugin on the PATH
#
# Run from the source root. The program is whichever *.pro is there. Running it again after
# ci/collect-licences.sh rebuilds with the bundled libraries' licences compiled in (see .github/workflows/build.yml).
set -euo pipefail

platform=${1:?usage: build.sh windows|macos|linux}
app=$(basename "$(ls ./*.pro)" .pro)
mkdir -p build

case "$platform" in
windows)
    (cd build && qmake-qt5 "../$app.pro" CONFIG+=release CONFIG-=debug_and_release && mingw32-make -j"$(nproc)")
    mkdir -p deploy
    cp "build/$app.exe" deploy/
    windeployqt-qt5 --release --compiler-runtime --no-translations \
        --no-system-d3d-compiler --no-opengl-sw --no-angle "deploy/$app.exe"
    # windeployqt leaves out what Qt itself links to (ICU, HarfBuzz, FreeType, zlib ...): copy the whole closure
    prefix=${MINGW_PREFIX:-/mingw64}
    for _ in 1 2 3 4; do
        for f in deploy/*.exe deploy/*.dll deploy/*/*.dll; do
            ldd "$f" | awk -v p="$prefix/" 'index($3, p) == 1 { print $3 }'
        done | sort -u | xargs -r cp -n -t deploy/
    done
    ;;
macos)
    (cd build && qmake "../$app.pro" CONFIG+=release && make -j"$(sysctl -n hw.ncpu)")
    macdeployqt "build/$app.app" -always-overwrite
    ;;
linux)
    (cd build && qmake "../$app.pro" CONFIG+=release PREFIX=/usr && make -j"$(nproc)" &&
     make install INSTALL_ROOT="$PWD/AppDir")
    (cd build && QMAKE=$(command -v qmake) linuxdeploy-x86_64.AppImage --appdir AppDir --plugin qt)
    ;;
*) echo "build.sh: unknown platform '$platform'" >&2; exit 1 ;;
esac
