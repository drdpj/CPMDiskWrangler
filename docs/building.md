# Building and releasing

This file is shared, unchanged, by [Acorn Disk Wrangler](https://github.com/drdpj/AcornDiskWrangler) and
[CP/M Disk Wrangler](https://github.com/drdpj/CPMDiskWrangler). Below, `<app>` is `acorndiskwrangler` or
`cpmdiskwrangler`.

## What it needs
Qt 6 (or Qt 5.15) with its widgets module, and a C++17 compiler. qmake builds with whichever Qt its `qmake`
belongs to; CMake uses Qt 6 if it finds it, and `-DUSE_QT5=ON` makes it use Qt 5.

    qmake <app>.pro && make                         # or: cmake -S . -B build && cmake --build build

The version shown in Help > About is `git describe --tags --always --dirty`, taken when qmake or CMake runs (re-run
it after a new commit or tag). Without git history, as in a source zip, it is "unknown" unless you give it:
`qmake APP_VERSION=v1.0.0 ...` or `cmake -DAPP_VERSION=v1.0.0 ...`.

## Linux
Ubuntu/Debian: `sudo apt install qt6-base-dev qmake6 build-essential` (Qt 5: `qtbase5-dev`), then build with
`qmake6` (or `qmake` for Qt 5).

Linux desktops take an application's name and icon from a `.desktop` file, not from the running program, so a
program run straight from the build folder shows a generic icon in the dock. `make install` puts the program, its
`.desktop` file and its icons where the desktop looks:

    qmake6 PREFIX=$HOME/.local <app>.pro && make && make install      # just for you (~/.local/bin on PATH)
    qmake6 <app>.pro && make && sudo make install                     # everyone (/usr/local)

CMake: `cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local && cmake --build build && cmake --install build`.
If the dock still shows a generic icon, log out and in again.

## Windows (MSYS2)
1. Install [MSYS2](https://www.msys2.org/), open the **MSYS2 UCRT64** shell, and install the compiler and Qt 6:

       pacman -Syu
       pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-qt6-base git

   For Qt 5, install `mingw-w64-ucrt-x86_64-qt5-base mingw-w64-ucrt-x86_64-qt5-tools` instead and put `QT=5` in
   front of each `ci/build.sh` below. (The older MINGW64 shell works too, with `mingw-w64-x86_64-` packages.)

2. From the source folder:

       bash ci/build.sh windows                      # build, and copy Qt's DLLs and plugins into deploy/
       bash ci/collect-licences.sh windows deploy    # gather the licences of everything in deploy/
       bash ci/build.sh windows                      # build again, with those licences compiled in
       cp -r LICENSE THIRD-PARTY-NOTICES.md licences deploy/

   `deploy/` then runs on any 64-bit Windows 10 or later; zip it to share it. For a quick build to run from the
   UCRT64 shell, the first step's `build/<app>.exe` is enough. qmake's warning about a missing `_resource_res.o`
   is harmless.

## macOS
With Qt's official Qt 6 (the [online installer](https://www.qt.io/download-qt-installer-oss) or
[aqtinstall](https://github.com/miurahr/aqtinstall)) on your PATH:

    QT=6 bash ci/build.sh macos                            # build/<app>.app, built for Intel and Apple Silicon
    bash ci/collect-licences.sh macos build/<app>.app
    QT=6 bash ci/build.sh macos

For Qt 5, use Homebrew's `qt@5` (Apple Silicon only, and Homebrew drops it in May 2027) and `QT=5`.

## Release builds
`.github/workflows/build.yml` builds Windows, macOS and Linux packages on GitHub:

| Platform | Built with | Package |
|---|---|---|
| Windows 10+, x64 | MSYS2 UCRT64, Qt 6 | `.zip` of the program with its DLLs and plugins |
| macOS 12+, Intel and Apple Silicon | Qt's official Qt 6.8 LTS (universal) | `.dmg`, signed ad hoc only (right-click > Open the first time) |
| Linux x86_64 | Ubuntu 22.04, Qt 6, linuxdeploy | AppImage (carries its own Qt) |
| Ubuntu 22.04+ / Debian 12+, amd64 | the same build, `ci/make-deb.sh` | `.deb` using the system's Qt (about 300 KB) |

* **Every push to `main` and every pull request** builds all three; the packages are kept for 90 days as the
  run's artifacts (at the bottom of the run's summary page).
* **Pushing a tag starting with `v`** also publishes them as a GitHub release, with notes listing the pull
  requests merged since the last one. A tag with a suffix (`v1.0.0-beta.1`, `v1.0.0-rc.1`) makes a pre-release.

      git checkout main && git pull
      git tag v1.0.0-beta.2
      git push origin v1.0.0-beta.2

  To undo a tag before anyone has the release: `git tag -d <tag>`, `git push origin :refs/tags/<tag>`, and delete
  the release on GitHub.
* **Qt 5 builds**: Actions > Build > Run workflow, Qt major version 5. Their packages are named `...-qt5`; the
  macOS one comes from Homebrew's `qt@5`, for Apple Silicon only.

## Licences in the builds
Every build links to Qt's **shared** libraries, as the LGPL expects (people can swap in their own), and carries
the licence of everything it ships twice: compiled into the program (Help > Licences) and as files beside it
(`LICENSE`, `THIRD-PARTY-NOTICES.md`, `licences/`).

To know what ships, each platform is built twice. The first build is deployed (`windeployqt`, `macdeployqt`,
`linuxdeploy`); `ci/collect-licences.sh` then traces every library in it to the package it came from - MSYS2 on
Windows, Ubuntu for Linux, Homebrew for a Qt 5 Mac build - and copies that package's licence texts into
`licences/bundled/` with an index, writing `licences-bundled.qrc`. The second build compiles them in. If a library
can't be traced to a licence, the collector fails, so a release can't go out without one.

Qt's official Mac build has no package manager behind it, and it contains its own copies of libraries such as
libpng, HarfBuzz and PCRE2. Its licences come from Qt's source at the same version: Qt's LGPL and GPL texts, and
every third-party component Qt records in a `qt_attribution.json` (`ci/qt-attributions.py`).

## Shared files
These are identical in both repositories; change them in both: `ci/`, `.github/workflows/build.yml`,
`licences.cpp`, `licences.h`, `licences.qrc`, `licences/`, `version.pri`, `version.cmake`,
`docs/building.md` and `docs/retro-files-format.md`.
