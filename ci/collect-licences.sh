#!/usr/bin/env bash
# Shared, unchanged, by Acorn Disk Wrangler and CP/M Disk Wrangler.
# Copyright (C) 2026 Daniel Jameson. GPL v3 or later.
#
# Collects the licence texts of every library a deployed build ships, so they can be compiled into the
# program (Help > Licences) and copied next to it.
#
#   ci/collect-licences.sh windows <deploy-dir>     MSYS2 MINGW64/UCRT64 shell, after windeployqt
#   ci/collect-licences.sh macos   <App.app>        Qt's official build or Homebrew's, after macdeployqt
#   ci/collect-licences.sh linux   <AppDir>         Debian/Ubuntu, after linuxdeploy
#
# Run from the source root. Writes:
#   licences/bundled/<package>/<licence files>
#   licences/bundled/INDEX.txt                       which file came from which package, and its stated licence
#   licences-bundled.qrc                             the above as resources under :/licences/Bundled/
# Then re-run qmake (or cmake) and build again: the build files pick up licences-bundled.qrc when it exists.
#
# Qt's official build has no package manager behind it: its frameworks are traced to "qt-<version>", whose
# licences come from Qt's own source at that version (Qt's LGPL/GPL texts, and every third-party component
# Qt records in a qt_attribution.json - see ci/qt-attributions.py). That needs git and network access.
#
# It fails if any shipped library cannot be traced to a package with a licence text, so a release can't go
# out with one missing. Kept to bash 3.2 (macOS's /bin/bash): no associative arrays, no mapfile.
set -euo pipefail

platform=${1:?usage: collect-licences.sh windows|macos|linux <deployed-dir>}
deployed=${2:?usage: collect-licences.sh windows|macos|linux <deployed-dir>}
[ -e "$deployed" ] || { echo "collect-licences: $deployed not found" >&2; exit 1; }

out=licences/bundled
rm -rf "$out" licences-bundled.qrc
mkdir -p "$out"
index="$out/INDEX.txt"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
: > "$work/map"        # "<shipped file>\t<package>"
missing=0

note() { printf '%s\n' "$*" >&2; }

# ---------------------------------------------------------------- which package each shipped library is from
case "$platform" in
windows)
    prefix=${MINGW_PREFIX:-/mingw64}
    find "$deployed" -type f -iname '*.dll' | sort | while read -r f; do
        name=$(basename "$f")
        orig="$prefix/bin/$name"
        # only one of the two plugin folders exists, and find fails on the other: "|| true", or set -e ends the
        # script here without a word
        [ -f "$orig" ] || orig=$(find "$prefix/share/qt6/plugins" "$prefix/share/qt5/plugins" -type f -name "$name" \
                                      2>/dev/null | head -n 1 || true)
        # shellcheck disable=SC2015   # "|| true" covers either part failing
        pkg=$([ -n "$orig" ] && pacman -Qqo "$orig" 2>/dev/null || true)
        printf '%s\t%s\n' "$name" "${pkg:-?}" >> "$work/map"
    done
    ;;
macos)
    brew=$(brew --prefix 2>/dev/null || echo /opt/homebrew)
    qtprefix=$(qmake -query QT_INSTALL_PREFIX)
    qtplugins=$(qmake -query QT_INSTALL_PLUGINS)
    qtversion=$(qmake -query QT_VERSION)
    { find "$deployed/Contents/Frameworks" -maxdepth 1 \( -name '*.framework' -o -name '*.dylib' \) 2>/dev/null
      find "$deployed/Contents/PlugIns" -type f -name '*.dylib' 2>/dev/null; } | sort | while read -r f; do
        name=$(basename "$f")
        # shellcheck disable=SC2012   # Homebrew paths have no odd characters
        orig=$(ls -d "$brew"/opt/*/lib/"$name" "$brew"/opt/*/plugins/*/"$name" "$brew"/opt/*/share/qt/plugins/*/"$name" \
               2>/dev/null | head -n 1 || true)
        pkg=""
        if [ -n "$orig" ]; then                      # .../Cellar/<formula>/<version>/... -> <formula>
            real=$(cd "$(dirname "$orig")" && pwd -P)
            pkg=$(printf '%s\n' "$real" | sed -n 's|.*/Cellar/\([^/]*\)/.*|\1|p')
        elif [ -e "$qtprefix/lib/$name" ] || ls "$qtplugins"/*/"$name" > /dev/null 2>&1; then
            pkg="qt-$qtversion"                      # Qt's official build
        fi
        printf '%s\t%s\n' "$name" "${pkg:-?}" >> "$work/map"
    done
    ;;
linux)
    find "$deployed" -type f -name '*.so*' | sort | while read -r f; do
        name=$(basename "$f")
        # The AppDir's copy came from the system; dpkg knows which package installed a file of that name.
        pkg=$(dpkg -S "*/$name" 2>/dev/null | awk -F': ' -v n="$name" '
                  { k = split($2, p, "/") } p[k] == n && $2 ~ /^\/(usr\/)?lib/ { print $1; exit }' |
              cut -d, -f1 | cut -d: -f1 || true)      # not found: reported below as untraced
        printf '%s\t%s\n' "$name" "${pkg:-?}" >> "$work/map"
    done
    ;;
*) note "collect-licences: unknown platform '$platform'"; exit 1 ;;
esac

echo "collect-licences: shipped library -> package"
sed 's/^/    /' "$work/map"
[ -s "$work/map" ] || { note "collect-licences: no libraries found in $deployed - was it deployed?"; exit 1; }

# ---------------------------------------------------------------- each package's licence texts
# A package that installs no licence file of its own (Homebrew keeps only files named plainly LICENSE, COPYING
# ..., so it drops Qt's LICENSE.LGPLv3 and the like) gets the standard texts of the licences it declares, from
# the SPDX licence list. The index says which texts came from there.
spdx_texts() {             # spdx_texts <package> <SPDX expression>: 0 if every licence in it was found
    local id ok=0 got=1
    for id in $(printf '%s\n' "$2" | sed 's/spdx://g' | tr '(),' '   '); do
        case "$id" in AND|OR|WITH|and|or|with) continue ;; esac
        mkdir -p "$out/$1"
        if curl -fsSL --retry 3 -o "$out/$1/$id.txt" \
               "https://raw.githubusercontent.com/spdx/license-list-data/main/text/$id.txt"; then
            got=0
        else
            rm -f "$out/$1/$id.txt"; note "collect-licences: no SPDX text for '$id' ($1)"; ok=1
        fi
    done
    return $((ok | got))
}

copy_into() {              # copy_into <package> <file>: keeps the file name, numbering a clash
    local dest="$out/$1" base n
    mkdir -p "$dest"
    base=$(basename "$2" | tr ' ' _)   # qmake cannot depend on a resource whose path has a space
    n=2
    while [ -e "$dest/$base" ] && ! cmp -s "$2" "$dest/$base"; do base="$(basename "$2" | tr ' ' _).$n"; n=$((n + 1)); done
    cp -L "$2" "$dest/$base"
}

cut -f2 "$work/map" | sort -u | grep -v '^?$' > "$work/pkgs" || true
{
    echo "Libraries distributed with this build, the package each came from, and that package's stated licence."
    echo "Each package's licence texts follow in this list."
    echo
} > "$index"

while read -r pkg; do
    : > "$work/files"
    declared=""
    case "$platform" in
    windows)
        pacman -Qlq "$pkg" < /dev/null | grep '/share/licenses/' | grep -v '/$' > "$work/files" || true
        declared=$(pacman -Qi "$pkg" < /dev/null | sed -n 's/^Licenses *: *//p')
        ;;
    macos)
      case "$pkg" in
      qt-*)                                          # Qt's official build: licences from Qt's own source
        v=${pkg#qt-}
        git -c advice.detachedHead=false clone -q --depth 1 --branch "v$v" https://github.com/qt/qtbase.git "$work/qtbase" < /dev/null
        mkdir -p "$work/qt"
        for l in LGPL-3.0-only GPL-2.0-only GPL-3.0-only; do
            cp "$work/qtbase/LICENSES/$l.txt" "$work/qt/Qt $v - $l.txt"
        done
        python3 -I ci/qt-attributions.py "$work/qtbase" "$work/qt/third-party" >&2
        find "$work/qt" -type f | sort > "$work/files"
        declared="LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only, with the third-party code Qt contains"
        ;;
      *)
        keg=$(brew --prefix "$pkg" < /dev/null)
        find -L "$keg" -maxdepth 1 -type f \( -iname 'licen[cs]e*' -o -iname 'copying*' -o -iname 'copyright*' \
             -o -iname 'notice*' \) > "$work/files" 2>/dev/null || true
        # its "license" is an SPDX id, or {"any_of"/"all_of": [...]}, or {"id": {"with": "exception"}}
        declared=$(brew info --json=v2 "$pkg" < /dev/null | python3 -c '
import json, sys
def expr(x):
    if isinstance(x, str): return x
    if isinstance(x, list): return " AND ".join(expr(i) for i in x)
    for k, op in (("any_of", " OR "), ("all_of", " AND ")):
        if k in x: return "(" + op.join(expr(i) for i in x[k]) + ")"
    return " AND ".join(k + (" WITH " + v["with"] if isinstance(v, dict) and "with" in v else "") for k, v in x.items())
print(expr(json.load(sys.stdin)["formulae"][0].get("license") or ""))')
        ;;
      esac
        ;;
    linux)
        [ -e "/usr/share/doc/$pkg/copyright" ] && echo "/usr/share/doc/$pkg/copyright" > "$work/files"
        # machine-readable (DEP-5) copyright files name their licences on "License:" lines
        declared=$(sed -n 's/^License: *\([^ ]*\).*/\1/p' "/usr/share/doc/$pkg/copyright" 2>/dev/null |
                   sort -u | paste -sd, - | sed 's/,/, /g')
        declared=${declared:-see its copyright file}
        ;;
    esac
    libs=$(awk -F'\t' -v p="$pkg" '$2 == p { printf "%s%s", s, $1; s = ", " }' "$work/map")
    if [ -s "$work/files" ]; then
        while read -r f; do copy_into "$pkg" "$f"; done < "$work/files"
        printf '%s  [%s]\n    %s\n\n' "$pkg" "${declared:-licence not stated}" "$libs" >> "$index"
    elif [ -n "$declared" ] && [ "$platform" != linux ] && spdx_texts "$pkg" "$declared"; then
        printf '%s  [%s]\n    %s\n    (installs no licence file: standard texts of the licences above, from SPDX)\n\n' \
            "$pkg" "$declared" "$libs" >> "$index"
        note "collect-licences: $pkg installs no licence file; used the SPDX texts for: $declared"
    else
        note "collect-licences: no licence text found for package $pkg ($libs)"
        missing=1
    fi
done < "$work/pkgs"

untraced=$(awk -F'\t' '$2 == "?" { printf "%s%s", s, $1; s = ", " }' "$work/map")
if [ -n "$untraced" ]; then
    note "collect-licences: could not tell which package these came from: $untraced"
    missing=1
fi
[ "$missing" = 0 ] || { note "collect-licences: fix the above, or the release would ship without those licences"; exit 1; }

# ---------------------------------------------------------------- the resource file
xml() { printf '%s' "$1" | sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g' -e 's/"/\&quot;/g'; }
{
    echo '<!-- Made by ci/collect-licences.sh for a release build; not kept in the repository. -->'
    echo '<RCC>'
    echo '    <qresource prefix="/licences">'
    (cd "$out" && find . -type f | sed 's|^\./||' | LC_ALL=C sort) | while read -r rel; do
        alias="Bundled/$rel"
        [ "$rel" = INDEX.txt ] && alias="Bundled/ Index of bundled libraries"    # the space sorts it first
        printf '        <file alias="%s">%s</file>\n' "$(xml "$alias")" "$(xml "$out/$rel")"
    done
    echo '    </qresource>'
    echo '</RCC>'
} > licences-bundled.qrc

echo "collect-licences: $(wc -l < "$work/pkgs" | tr -d ' ') packages, $(wc -l < "$work/map" | tr -d ' ') libraries -> licences-bundled.qrc"
