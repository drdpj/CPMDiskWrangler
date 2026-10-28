#!/usr/bin/env bash
# Shared, unchanged, by Acorn Disk Wrangler and CP/M Disk Wrangler.
# Copyright (C) 2026 Daniel Jameson. GPL v3 or later.
#
# Collects the licence texts of every library a deployed build ships, so they can be compiled into the
# program (Help > Licences) and copied next to it.
#
#   ci/collect-licences.sh windows <deploy-dir>     MSYS2 MINGW64/UCRT64 shell, after windeployqt
#   ci/collect-licences.sh macos   <App.app>        Homebrew, after macdeployqt
#   ci/collect-licences.sh linux   <AppDir>         Debian/Ubuntu, after linuxdeploy
#
# Run from the source root. Writes:
#   licences/bundled/<package>/<licence files>
#   licences/bundled/INDEX.txt                       which file came from which package, and its stated licence
#   licences-bundled.qrc                             the above as resources under :/licences/Bundled/
# Then re-run qmake (or cmake) and build again: the build files pick up licences-bundled.qrc when it exists.
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
        [ -f "$orig" ] || orig=$(find "$prefix/share/qt5/plugins" -type f -name "$name" 2>/dev/null | head -n 1)
        # shellcheck disable=SC2015   # "|| true" covers either part failing
        pkg=$([ -n "$orig" ] && pacman -Qqo "$orig" 2>/dev/null || true)
        printf '%s\t%s\n' "$name" "${pkg:-?}" >> "$work/map"
    done
    ;;
macos)
    brew=$(brew --prefix)
    { find "$deployed/Contents/Frameworks" -maxdepth 1 \( -name '*.framework' -o -name '*.dylib' \) 2>/dev/null
      find "$deployed/Contents/PlugIns" -type f -name '*.dylib' 2>/dev/null; } | sort | while read -r f; do
        name=$(basename "$f")
        # shellcheck disable=SC2012   # Homebrew paths have no odd characters
        orig=$(ls -d "$brew"/opt/*/lib/"$name" "$brew"/opt/*/plugins/*/"$name" 2>/dev/null | head -n 1 || true)
        pkg=""
        if [ -n "$orig" ]; then                      # .../Cellar/<formula>/<version>/... -> <formula>
            real=$(cd "$(dirname "$orig")" && pwd -P)
            pkg=$(printf '%s\n' "$real" | sed -n 's|.*/Cellar/\([^/]*\)/.*|\1|p')
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
              cut -d, -f1 | cut -d: -f1)
        printf '%s\t%s\n' "$name" "${pkg:-?}" >> "$work/map"
    done
    ;;
*) note "collect-licences: unknown platform '$platform'"; exit 1 ;;
esac

[ -s "$work/map" ] || { note "collect-licences: no libraries found in $deployed - was it deployed?"; exit 1; }

# ---------------------------------------------------------------- each package's licence texts
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
        pacman -Qlq "$pkg" | grep '/share/licenses/' | grep -v '/$' > "$work/files" || true
        declared=$(pacman -Qi "$pkg" | sed -n 's/^Licenses *: *//p')
        ;;
    macos)
        keg=$(brew --prefix "$pkg")
        find -L "$keg" -maxdepth 1 -type f \( -iname 'licen[cs]e*' -o -iname 'copying*' -o -iname 'copyright*' \
             -o -iname 'notice*' \) > "$work/files" 2>/dev/null || true
        declared=$(brew info --json=v2 "$pkg" |
                   python3 -c 'import json,sys; print(json.load(sys.stdin)["formulae"][0].get("license") or "")')
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
