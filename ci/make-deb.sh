#!/bin/bash
# Shared, unchanged, by Acorn Disk Wrangler and CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
#
# ci/make-deb.sh <build dir> <output dir>
#
# Makes a .deb of the program built in <build dir> (by ci/build.sh linux: qmake PREFIX=/usr) that uses the
# system's Qt instead of carrying its own, so it is small. dpkg-shlibdeps works out the library packages from what
# the program links to (Qt, libc, libstdc++), at least the versions it was built against: built on Ubuntu 22.04
# (Qt 6.2) it installs on 22.04 and later and on Debian 12 and later. (Qt 6 runs programs built against an older
# Qt 6; on Ubuntu 24.04 and Debian 13 the libraries were renamed ...t64, and those packages provide the old names.)
# The program goes in /usr/bin, its .desktop file and icons where desktops look for them, so docks and menus show
# its name and icon. Run from the source folder, with APP and VERSION set as the workflow sets them (QT=5 for a
# Qt 5 build). Writes <output dir>/<APP>-<VERSION>-linux-<arch>.deb.
set -euo pipefail
b=$(cd "${1:?usage: make-deb.sh <build dir> <output dir>}" && pwd)
mkdir -p "${2:?usage: make-deb.sh <build dir> <output dir>}"
out=$(cd "$2" && pwd)
app=${APP:?set APP}
version=${VERSION:?set VERSION}
qt=${QT:-6}
src=$PWD
arch=$(dpkg --print-architecture)

# A Debian version must start with a digit and sort the way releases do: v1.0.0-beta.2 -> 1.0.0~beta.2 (before
# 1.0.0), v1.0.0-beta.2-3-gabc1234 -> 1.0.0~beta.2+3.gabc1234 (after the tag), an untagged build -> 0~gabc1234.
debver=${version%-qt5}
debver=${debver#v}
debver=$(printf '%s' "$debver" | sed -E 's/-([0-9]+)-g([0-9a-f]+)$/+\1.g\2/; s/-(alpha|beta|rc|pre)/~\1/; s/-dirty$/+dirty/')
[[ $debver =~ ^[0-9] ]] || debver="0~$debver"

w="$b/deb"
rm -rf "$w"
mkdir -p "$w/root/DEBIAN" "$w/debian"
make -C "$b" install INSTALL_ROOT="$w/root" >/dev/null
strip --strip-unneeded "$w/root/usr/bin/$app"

# the package's copyright file, and this program's own notices
doc="$w/root/usr/share/doc/$app"
mkdir -p "$doc"
cp "$src/THIRD-PARTY-NOTICES.md" "$doc/"
repo="${GITHUB_SERVER_URL:-https://github.com}/${GITHUB_REPOSITORY:-}"
if [ -z "${GITHUB_REPOSITORY:-}" ]; then
    repo=$(git -C "$src" config --get remote.origin.url 2>/dev/null | sed -E 's#^git@github.com:#https://github.com/#; s#\.git$##')
fi
cat > "$doc/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: $app
Source: $repo

Files: *
Copyright: 2026 Daniel Jameson
License: GPL-3+
 This program is free software: you can redistribute it and/or modify it under the terms of the GNU General
 Public License as published by the Free Software Foundation, either version 3 of the License, or (at your
 option) any later version.
 .
 On Debian systems, the full text of the GNU General Public License version 3 is in
 /usr/share/common-licenses/GPL-3. Material from others included in the program, and its licences, is listed in
 /usr/share/doc/$app/THIRD-PARTY-NOTICES.md.
EOF

# a changelog, as every package has one: the release notes are on the releases page
printf '%s (%s) unstable; urgency=medium\n\n  * Release %s. Notes:\n    %s/releases\n\n -- %s  %s\n' \
    "$app" "$debver" "$version" "$repo" "Daniel Jameson <2575676+drdpj@users.noreply.github.com>" \
    "$(date -R -d "@$(git -C "$src" log -1 --format=%ct 2>/dev/null || date +%s)")" | gzip -9n > "$doc/changelog.gz"

# the library packages, from what the program links to
printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$app" "$app" > "$w/debian/control"
depends=$(cd "$w" && dpkg-shlibdeps -O "root/usr/bin/$app" 2>/dev/null | sed -n 's/^shlibs:Depends=//p')
[ -n "$depends" ] || { echo "make-deb.sh: dpkg-shlibdeps found no dependencies" >&2; exit 1; }
# Qt's platform plugins aren't linked to, so dpkg-shlibdeps can't see them: X11 (and XWayland) needs this package
# for Qt 6 (Qt 5 keeps it in libqt5gui5); the native Wayland plugin is only recommended
if [ "$qt" = 6 ]; then depends="$depends, qt6-qpa-plugins"; recommends="qt6-wayland"
else recommends="qtwayland5"; fi

summary=$(sed -n 's/^Comment=//p' "$src/$app.desktop" | head -1)
name=$(sed -n 's/^Name=//p' "$src/$app.desktop" | head -1)
cat > "$w/root/DEBIAN/control" <<EOF
Package: $app
Version: $debver
Architecture: $arch
Maintainer: Daniel Jameson <2575676+drdpj@users.noreply.github.com>
Installed-Size: $(du -sk --exclude=DEBIAN "$w/root" | cut -f1)
Depends: $depends
Recommends: $recommends
Section: utils
Priority: optional
Homepage: $repo
Description: $summary
 $name is a desktop program for disc images: it lists,
 adds, extracts, renames and deletes files, and makes new discs. Files
 can be dragged between its windows, the desktop and other programs.
EOF

# dpkg's own triggers refresh the desktop menus and the icon cache, so no maintainer scripts are needed
find "$w/root" -type d -exec chmod 755 {} +
deb="$out/$app-$VERSION-linux-$arch.deb"
dpkg-deb --root-owner-group -Zxz --build "$w/root" "$deb" >/dev/null
echo "$deb"
dpkg-deb --field "$deb" Package Version Depends Recommends Installed-Size
