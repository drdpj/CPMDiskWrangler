# CP/M Disk Wrangler

Qt5 (C++) tool for CP/M disc images: it reads raw sector dumps, ImageDisk (`.IMD`), CPCEMU and extended `.DSK`
and TeleDisk (`.TD0`, read-only), **works out the disc's format from its contents**, and lets you add, extract,
view, rename and delete files, change attributes and user numbers, set CP/M 3 labels, and make new discs in any
of about 140 formats. Each disc opens in its own window; files can be dragged between windows, to and from the
desktop, and to and from [Acorn Disk Wrangler](https://github.com/drdpj/AcornDiskWrangler), using the same
`retro-files` drag format ([docs/retro-files-format.md](docs/retro-files-format.md)).

Build:  `qmake cpmdiskwrangler.pro && make`      (or: `cmake -S . -B build && cmake --build build`)
Needs:  Qt 5 widgets dev package (Ubuntu: `qtbase5-dev`)
Run:    `./cpmdiskwrangler [image ...]`          (each image gets its own window)

## Building on Windows (MSYS2 / MinGW64)
Exactly as for Acorn Disk Wrangler: in the **MSYS2 MINGW64** shell, install the tools, then from the source
folder build and deploy, collect the licences of everything it ships, and build again with them compiled in:

    pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-qt5-base mingw-w64-x86_64-qt5-tools
    bash ci/build.sh windows
    bash ci/collect-licences.sh windows deploy
    bash ci/build.sh windows
    cp -r LICENSE THIRD-PARTY-NOTICES.md licences deploy/

Zip the `deploy` folder to distribute it, and publish the matching source with it (the repository link or a
source zip): the GPL requires that for this program, and the LGPL lets recipients swap in their own Qt DLLs.

## Release builds (Windows, macOS, Linux)
`.github/workflows/build.yml` builds all three on GitHub on every push and pull request (the packages are kept
as the run's artifacts), and pushing a tag `v*` publishes them as a release:

| Platform | Built with | Package |
|---|---|---|
| Windows x64 | MSYS2 MINGW64, `windeployqt-qt5` | `.zip` of the program, its DLLs and plugins |
| macOS (Apple Silicon) | Homebrew `qt@5`, `macdeployqt` | `.dmg`; signed ad hoc only, so the first time it must be opened with right-click > Open |
| Linux x86_64 | Ubuntu 22.04, `linuxdeploy` | AppImage |

Every build links to Qt's **shared** libraries, as the LGPL expects (people can replace them with their own),
and carries the licence of everything it ships twice over: compiled into the program (**Help > Licences**) and
as files beside it (`LICENSE`, `THIRD-PARTY-NOTICES.md`, `licences/`). To make that work each platform is built
twice - the first build is deployed to find out which libraries ship, `ci/collect-licences.sh` gathers those
libraries' licences from the packages they came from (it fails if one can't be traced), and the second build
compiles them in. The same scripts work by hand, from the source root:

    bash ci/build.sh windows                       # or macos / linux: build and deploy
    bash ci/collect-licences.sh windows deploy     # macos: build/<app>.app   linux: build/AppDir
    bash ci/build.sh windows                       # again, with the licences compiled in

`ci/`, `licences.cpp/.h`, `licences.qrc`, `licences/` and the workflow are shared, unchanged, with [Acorn Disk Wrangler](https://github.com/drdpj/AcornDiskWrangler).

## Image containers
| Container | Read | Write | Notes |
|---|---|---|---|
| Raw sector image (`.img`, `.dsk`, `.h8d`, ...) | yes | yes | geometry from the format (detected or chosen); a short image is padded in memory and saved full size |
| ImageDisk `.IMD` | yes | yes | header, comment, sector numbering (interleave), cylinder/head maps, deleted/error marks and modes are kept; unchanged tracks are written back byte for byte |
| CPCEMU `.DSK` and extended `.DSK` | yes | yes | written back in place: only changed sectors' bytes change |
| TeleDisk `.TD0` (2.x, normal and "advanced" compression) | yes | Save As only | Save As ImageDisk/DSK/raw to edit |

Sectors are always found by their **sector ID**, never by their position in the file, so rotated or
interleaved tracks (common in ImageDisk and TeleDisk files) read correctly.

**Mixed density** (a single-density FM track 0 on an otherwise double-density MFM disc, as on Osborne 8-inch and
Research Machines 480Z discs) is handled: each track keeps its own shape and recording mode, the filesystem only
uses tracks of the main shape, and editing leaves track 0 exactly as it was. Formats can say what their track 0
looks like (`track0 26 128 fm` in a disc definition), so new discs are made with it - as ImageDisk (which records
FM/MFM per track) or DSK; a raw image can't hold tracks of different shapes, so it is refused there.

## How the format is worked out
A CP/M disc doesn't say what its layout is, so the program tries them and keeps the one that makes sense:

1. The **physical shape** comes from the container (sector size, sectors per track, sides, sector IDs). For a raw
   image, every known format with that file size and every common geometry that divides it are candidates.
2. For each combination of **reserved (boot) tracks, skew and side order**, it reads where the directory would be
   and checks every 32-byte entry: user number, name characters, extent and record counts.
3. For each **block size and directory size**, it checks the block pointers: two-byte or one-byte, in range, not
   in the directory, not shared between files, and the number of blocks in each extent matches its record count.
   Files on a well-used disc start right after the directory, which settles the directory size.
4. The best readings are checked on **content**: text files must stay text up to their `^Z`, `.COM` files
   should start with a plausible instruction, and no file should start in never-written (E5) space.
5. **Known formats** (built-in and cpmtools' diskdefs, about 140) score extra, as do physical clues: a format's
   sector-ID base (Amstrad's C1 and 41), and the Amstrad PCW/+3 boot-sector spec.

**Tools > Disc format** shows the ranked readings with their evidence and a preview of what each would list,
lets you try a format by hand, and copies any of them as a cpmtools `diskdef`. A format you confirm there is
remembered for that file.

### When changes are allowed
A disc can be changed only when the reading is safe: a known format that nothing else competes with, a format
you confirmed, or a disc made by this program. Otherwise it opens **read-only** and says why in the status bar:

* the best reading isn't a known format (it was found by searching), or another reading comes close;
* the directory has errors (Tools > Check disc);
* the image is a TeleDisk file (Save As first).

Everything is staged in memory until **Save**, which re-checks the directory first, refuses if the file was
changed by something else, keeps a `.bak` (option), and writes atomically.

## Using it
* **Files are grouped by user number.** Drop files on a *User N* row to put them in that user number; dropping on
  the disc row keeps a CP/M file's own user number (0 for other files). Host folders called `user3`, `user 3`,
  `u3` or `3` go to that user number; other folders are flattened.
* Names are converted to CP/M 8.3 (upper case; invalid characters become `-`). Exact file sizes are kept where the
  disc can record them (the last-record byte count that CP/M 3 and cpmtools use).
* **Drag files out** to the desktop or another program: plain files plus the `retro-files` JSON with the
  user number and attributes. From Acorn Disk Wrangler: locked → read-only, DOS R/S/H/A → R/O, SYS, ARC.
* F3 / double-click: view as text (up to the `^Z`, optionally ignoring WordStar's bit 7) or hex.
* Alt+Enter: attributes (R/O, SYS, ARC, F1'-F4') and user number, for one or many files. F2: rename.
* Edit > Disc label (CP/M 3). Tools > Check disc, Disc information (geometry, DPB values, the format as a diskdef).
* File > New disc image: any format in the library, as ImageDisk, extended DSK or raw, optionally with CP/M 3
  time stamps and a label.
* File > Save As converts between containers (e.g. TeleDisk → ImageDisk).
* Tools > Add a diskdefs file: extend the format library with your own cpmtools-style definitions.

## The format library
`resources/builtin-diskdefs` (this program's, including the **Acorn Z80 second processor** 400K format from
Acorn Application Note 007, the Amstrad formats with their sector IDs, and the Kaypros with their sides made
explicit, the Osborne 8-inch double-density format and Research Machines 480Z MD/MQ with their FM track 0) and
`resources/cpmtools-diskdefs` (cpmtools' file, GPL v3). Both use cpmtools' syntax; the built-in
one adds a few keys (`heads`, `secbase`, `imdmode`, `track0`, `desc`, `source`) that cpmtools ignores.

### A separate disc on each side
Some double-sided discs hold two filesystems, one per side (Research Machines 480Z, for example). Detection tries
a single-sided format on each side of a two-sided disc; when that reads best, the window shows **Side 0** and
**Side 1**, each with its own user numbers, label and free space. Drop files on a side's rows to put them there;
drag files from one side to the other to copy them. Raw images of these discs (side 0 then side 1, or cylinder by
cylinder, each side with its own FM track 0 - 732,160 bytes for an RM 480Z MQ disc) open directly.

## Not supported
Making a new disc with a separate filesystem on each side (open a blank one-sided format twice, or copy an
existing image), copy-protected tracks
(duplicate sector IDs; read but never written), TeleDisk 1.x "advanced compression", DateStamper's
`!!!TIME&.DAT` stamps (the file is kept, just not interpreted), CP/M 3 passwords (kept, not checked).

## Licence
Copyright (C) 2026 Daniel Jameson. Licensed under the GNU General Public License v3.0 or later - see LICENSE.
Includes the disc definitions from cpmtools 2.23 (Michael Haardt and contributors, GPL v3 or later), unchanged in
`resources/cpmtools-diskdefs`. The TeleDisk decoder follows LZHUF by Haruyasu Yoshizaki and Haruhiko Okumura.
Uses Qt 5 under the LGPL v3. Everything third-party, and its terms, is listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## For developers / agents
See [AGENTS.md](AGENTS.md) (design rules, format notes, detection, how to test, pitfalls).
