# AGENTS.md - working on CP/M Disk Wrangler

Read this before changing code. It records the design rules, the facts about the formats (and how sure we are of
each), how detection works and why, how to build and test, and the mistakes already made once. It is the sibling
of Acorn Disk Wrangler (github.com/drdpj/AcornDiskWrangler) and follows the same rules and style.

## What this is
A Qt 6 / Qt 5.15 (C++17) desktop tool for CP/M disc images: raw, ImageDisk, CPCEMU DSK / extended DSK, TeleDisk (read).
It works out each disc's logical format, then lists, adds, extracts, renames and deletes files, sets attributes,
user numbers and CP/M 3 labels, and makes new discs. One disc per window; drag and drop between windows, the
desktop and other programs via the `retro-files` format shared with Acorn Disk Wrangler. GPLv3-or-later; the
copyright line in every header is `Copyright (C) 2026 Daniel Jameson`; put the same header on any new source file
(`#` comments for scripts and build files). The notice also appears in Help > About, `QMAKE_TARGET_COPYRIGHT` and
the `.rc`. **Anything taken from or closely modelled on someone else's work goes in `THIRD-PARTY-NOTICES.md`**
with its terms (and in About if it ships in the program); material only consulted for facts goes in its
"Consulted" table. Never commit third-party disc images: the tests read them from outside the repo.

## Source map
| File | Role |
|---|---|
| `diskimage.h/.cpp` | `Disk::Image`: the physical disc (tracks of sectors keyed by sector ID), the containers (raw, IMD, DSK, EDSK, TD0), staged writes, serialising, atomic commit |
| `lzhuf.cpp` | TeleDisk 2.x "advanced compression" decoder (Okumura's LZHUF; tables generated, not copied) |
| `cpmformat.h/.cpp` | `Cpm::Format`: a diskdef plus the derived DPB (DSM, EXM, pointer width, skew table, `locate()`); the diskdefs parser; the library |
| `cpmfs.h/.cpp` | `Cpm::Volume`: directory, files, extents, CP/M 3 / P2DOS stamps and labels, add/delete/rename/attrs, `check()`, `makeBlank()` |
| `detect.h/.cpp` | working out the format: candidates, scoring, ranking, confidence, `safeToWrite` |
| `resources/builtin-diskdefs` | this program's definitions (Acorn Z80, Amstrad, Kaypro) and descriptions for cpmtools' ones |
| `resources/cpmtools-diskdefs` | cpmtools 2.23's diskdefs file, unchanged (Michael Haardt and contributors, GPL v3+; see THIRD-PARTY-NOTICES.md) |
| `mainwindow.*`, `mainwindow_p.h` | window, `DropTree`, importing/exporting, Copy / Paste, menus; `mainwindow_p.h` has the shared tree helpers (same as Acorn Disk Wrangler's) and `XItem` |
| `formatdialog.*` | Tools > Disc format (ranked readings, evidence, preview, try a format by hand) |
| `newdisc.*` | File > New disc image |
| `dragout.*` | `retro-files` JSON writer/reader, drag-out data, temporary drag folders |
| `viewer.*`, `viewtext.*` | the text/hex viewer (CP/M `^Z`, WordStar bit 7) |
| `appsettings.h` | where settings live: the system's place, or `<program>.ini` beside the program (portable; for an AppImage, beside the `.AppImage` file - `$APPIMAGE` - not in its temporary mount; a program installed on Linux has no portable mode, so File > Create local config file isn't shown). Use `AppSettings` instead of `QSettings` in program code (tests may use `QSettings` to clear the system store). Header only; shared with Acorn Disk Wrangler, keep identical |
| `buttonbar.h` | the main window's button bar: real QPushButtons kept in step with their QActions (a QToolBar's own buttons draw as flat text on macOS). Header only; shared with Acorn Disk Wrangler, keep identical |
| `icon.*`, `icons/` | the 8-inch floppy icon (QPainter); PNGs from `tests/mkicon`, the `.ico` made from them with Pillow, and a hand-written SVG that renders pixel-identical (check with cairosvg if you change either) |

## The rules that must not be broken
1. **Nothing is written to the file except by `Image::commit()` / `saveAs()`.** Filesystem code writes through
   `Image::write`, which stages; every read overlays the staging.
2. **Sectors are addressed by sector ID order, never file order.** `Track::byId` maps a logical index to the
   recorded order. ImageDisk/TeleDisk tracks are often rotated (Osborne single density: IDs 2,3,...,10,1);
   reading in file order gives a plausible but wrong directory. (Others have made this mistake: see the
   o1next repository's notes.)
3. **Validate before writing.** `MainWindow::save()` runs `Volume::check()` and refuses on any error, and refuses
   if the file changed on disk since it was opened. Commit writes a complete new file atomically (QSaveFile),
   with an optional `.bak`, then reloads it.
4. **A save with nothing changed changes nothing**, and unchanged parts stay byte-identical: IMD tracks without
   staged sectors are re-emitted from their original bytes (header and comment too); DSK/EDSK and raw files are
   patched in place. `fstest roundtrip` checks this on every image it is given.
5. **Every mutation is atomic**: `TXN_BEGIN` in `cpmfs.cpp` snapshots the staging and restores it on failure. A
   failed add (disc full, directory full) leaves no trace (`fstest atomic`).
6. **Changes need a safe reading** (`MainWindow::readOnlyReason`): a known format with no competing reading,
   a format the user confirmed (remembered per file in QSettings), or a disc made here. A reading found only by
   searching is shown but read-only until confirmed. A directory that fails `check()` is read-only. TeleDisk
   files are read-only (Save As). Don't weaken this: a wrong directory size or block size means new files are
   written over data, or written where the real machine won't see them.
7. **The physical shape of a new disc must match the real thing.** A built-in definition that replaces a
   cpmtools one must describe exactly the same layout, including `dirblks` (`fstest library` checks this).

## Format facts (confidence noted)
- **Directory entries** (cpm(5), verified with cpmtools on generated and real discs): status 0-15 user (16-31:
  P2DOS users, or CP/M 3 password entries), 0x20 label, 0x21 time stamps (every fourth entry), 0xE5 free.
  Name bytes 1-11 with attributes in bit 7 (F1'-F4' on name bytes 1-4, T1' R/O, T2' SYS, T3' ARC). EX (byte 12,
  low 5 bits) and S2 (byte 14, 6 bits) make the logical extent; RC (byte 15) <= 128; 16 one-byte or 8 two-byte
  block pointers (two-byte when DSM > 255). An entry holds EXM+1 logical extents of 16K.
- **Byte 13 (S1/Bc)**: the bytes used in the last record (1-127; 0 = 128). CP/M 3 and **cpmtools on every
  format** write it; CP/M 2.2 leaves it 0. We honour it whenever it is 1-127 and always write it (this is what
  made our sizes match cpmtools on the z80pack discs). ISX's "unused bytes" meaning is not supported.
- **CP/M 3 stamps**: SFCB in every fourth entry: 3 x (4-byte create/access stamp, 4-byte update stamp, password
  mode, reserved) for the 3 entries before it; date = days since 31 Dec 1977 (LE16), hour and minute BCD. Stamps
  are written only when the label's mode byte enables them (P2DOS/ZSDOS: always). Checked with `cpmls -F`.
- **Skew** (cpmtools' algorithm, `Format::derive`): logical sector i -> physical index `xlat[i]` within the track;
  applied to every track. IBM 3740 skew 6, Osborne single density skew 2, Osborne 8" double density skew 3,
  Heathkit H8D skew 4 (found by detection on two real images; not in cpmtools).
- **Side order**: `alt` (cylinder by cylinder), `outback` (side 1 runs back from the last cylinder: Acorn Z80),
  `outout`. Raw images are cut into tracks cylinder by cylinder; a raw image in cpmtools' logical order is read
  with `alt`.
- **cpmtools definitions don't state the number of sides.** We infer 2 when tracks > 86, and detection tries a
  definition both ways when the inferred count doesn't fit the image (that is how a 40-cylinder double-sided
  Kaypro 4 disc is matched to `kpiv`, "tracks 80"). For new discs the built-in definitions say it explicitly.
- **Editing a real mixed-density IMD** (`fstest addto --force` on an Osborne 8" disc): track 0 stays
  byte-identical, every track keeps its mode, only the directory and data tracks change.
- **Kaypro** (`kpii`, `kpiv`): sector IDs from 0; `dirblks 4` / `dirblks 2` (the Kaypro reserves 4K for a 2K
  directory). Real Kaypro 4 discs number side 1's sectors 10-19; a new disc made here uses 0-9 on both sides
  (**unverified on a real Kaypro**; per-head sector bases are not modelled).
- **Amstrad**: CPC data IDs C1-C9 (no boot tracks), system 41-49 (2), IBM 01-08 (1); PCW 180K IDs 1-9. The PCW/+3
  boot sector spec (byte 0 = 0 or 3) describes the disc; libdsk writes it for pcw720 (decoded to 2K blocks, 256
  entries, 1 reserved track - matching cpmtools' `cf2dd`).
- **Acorn Z80 second processor**: from Acorn Application Note 007: 80 track double-sided, 10 x 256-byte sectors
  (FM), side 1 runs back from track 79, 3 reserved tracks, 128 entries, logical 512-byte sectors from physical
  pairs 0/1 4/5 8/9 2/3 6/7 (skew table `0,1,4,5,8,9,2,3,6,7`), sector IDs 0-9. The 2K block size is inferred from
  the 388K the note states. **Checked on three real discs** (the owner's, raw, interleaved, 409,600 bytes, not in
  the repo: Colossal Cave `cave.dsd`, CP/M installation discs 1 and 2 `cpm1.dsd`, `cpm2.dsd`): each detected as
  acorn-z80 with no rival and a clean Check, every file (12, 48, 47) identical to `tests/acornz80_check.py`'s (an
  independent reader written from the note alone); an added file reads back with it, and tracks 0-2 of side 0
  and the odd entry below are left untouched. Disc 1 holds the CP/M system in tracks 0-2 and is full (0 blocks
  free). The real discs also have:
  - **an Acorn DFS catalogue on track 0** (sectors 0-1, in the boot area): title "Acorn CP/M", one locked file
    `$.CPMDISC` from sector 2 to 800, so the BBC's DFS sees a full disc; byte-identical on all three. On `cpm2.dsd`
    the same catalogue is also on side 1's cylinder 0 (DFS drive 2) - but that is CP/M's last data track, which a
    full disc overwrites (`cpm1.dsd`) and `cave.dsd` has blank, so it is formatter leftover, not reserved, and new
    discs don't get it. Tracks 0-2 are otherwise E5 unless the CP/M system is on them. Format key
    `acorndfs yes`: new discs get it (track 0 byte-identical to the real one), detection gives +6 for it and does
    not take it as a foreign filesystem.
  - **directory entry 0 that is not a file**: user 0, name bytes `fe bd ee 29 00 30 db 29 3f 1f 1d`, S2 0x39, RC 0,
    no blocks - only on `cave.dsd` (entry 0 is an ordinary file on the installation discs), so it is not part of
    the format: probably leftover junk. Handled generally as an *inert* entry
    (`Volume::inertEntry`: a user entry with control characters in its name, no records, no blocks): not listed,
    its slot never reused, a warning (not an error) in Check; detection tolerates one (`KInert`), and an all-zero
    entry is never inert. More real discs would show whether every Acorn disc has it and what it holds.
- **ImageDisk**: header line + comment up to 0x1A; per track mode, cylinder, head (bit 7 cylinder map, bit 6 head
  map), count, size code (0xFF: a table of sizes), sector numbering map, records 0-8 (0 missing, even =
  compressed, 3/4 deleted, 5-8 with data error). Checked against an independent Python parser
  (`tests/decode_container.py`) and libdsk.
- **Mixed density / track 0 of its own shape**: each `Track` keeps its sectors and IMD mode; `Image::shape()` takes
  the most common shape **by bytes** (not by track count) and counts heads only where that shape occurs. A format
  may declare `track0 SECTRK SECLEN fm|mfm [FIRSTID]` (cpmtools ignores the key); it must be inside the boot area,
  so the filesystem never reads it. `makeBlank` reshapes cylinder 0 head 0 (FM at the same data rate as the
  other tracks: IMD mode 0/1/2 for 3/4/5) and refuses raw containers. Detection: +3 when the disc's track 0
  matches a format's `track0`, -3 when it doesn't. `sameLayout` ignores it (it doesn't affect the filesystem).
  - `osborne-8dd`: worked out by detection from 7 real Osborne 8-inch discs (track 0 FM 26 x 128, then MFM
    8 x 1024, skew 3, 2 boot tracks, 2K blocks); directory size 128 is the whole-block reading (96 would read
    the same discs). New discs made here have the same track structure as the real ones (checked with
    `tests/decode_container.py` and the IMD dumper).
  - `rm-dd`, `rm-qd`: cpmtools' layouts plus track 0 FM 16 x 128 from Adam Sampson's RM archiving notes
    (offog.org); **not checked against a real RM disc**.
  - On double-sided discs `makeBlank` gives only cylinder 0 head 0 the track 0 shape. Raw layouts
    (`RawLayout::t0sectrk/t0seclen`) give cylinder 0 of **each** side the track 0 shape, as on the RM discs.
- **A separate disc on each side** (`separatesides yes`; `Format::fixedHead` picks the side; cpmtools ignores the
  key): `tracks` etc. describe one side. Detection tries every single-sided library format this way on a
  two-sided disc (scoring side 0, plus side 1 if it reads sensibly, plus each side's track 0 shape); the window
  then attaches one `Cpm::Volume` per side (`m_vols[2]`), and every row, `FileRef`, drag payload and import
  carries the side. Drags between the two sides of one window are allowed; drags back onto the same side are not.
  Not yet: generic search for separate sides (only library formats), new discs of this kind.
- **Research Machines 480Z** (checked on three of the owner's real discs: `BASIC-disk1.img`, `BASIC-disk2.img`,
  `basiczasm.img`, raw, 732,160 bytes): side 0 then side 1 in the file; each side = FM track 0 of 16 x 128 (RM
  boot code, "Quad density disc"), then 79 tracks of 9 x 512; each side is an `rm-qd` disc (skew 5, 3 boot
  tracks, 2K blocks, 128 entries). All 20 files match cpmtools (run on each side cut out of the file); an edit of
  side 1 left side 0 and both track 0s byte-identical and passes `fsck.cpm`. The images are the owner's, not in
  the repo: `guitest --rm <image>` and `run_all.sh` (from `<real>/rm/*.img`) use them when given.
- **TeleDisk**: 12-byte header (`TD` normal, `td` advanced), optional comment block, tracks (count, cylinder,
  head, crc), sectors (C H R N flags crc; data block with length and encoding 0 raw / 1 repeated pair /
  2 RLE). Checked against MAME's reader and on real Kaypro and Zorba images. 1.x advanced compression (LZW) is
  refused.
- **DSK / EDSK**: standard (fixed track size) and extended (track size table); sector data in recorded order;
  weak or short sectors (stored length != nominal) are read but never written.

## Behaviour details (moved from the README)
* Exact file sizes are kept where the disc can record them: the last-record byte count used by CP/M 3 and
  cpmtools.
* A short raw image is padded in memory and saved full size.

## How detection works (detect.cpp)
1. Physical layouts: the container's own; for raw images every library format whose size fits (exactly, or
   a truncated image at least 60% of it) and every factorisation into 128-1024-byte sectors, common sectors per
   track, 30-86 cylinders, 1-2 heads.
2. Library candidates that fit physically are evaluated directly (+3 for being known; +4 / -6 if the format
   has a sector-ID base and the disc's IDs match / don't). The PCW spec (+15) and a remembered format (+20) too.
3. The search: for each side order x boot tracks (0-6) x skew (1..sectrk-1; none beyond 32 sectors/track),
   read a window where the directory would be and classify the entries once; then for each block size and
   directory size (whole blocks only) score: +4 per file entry, +2 per entry whose block count matches its
   records, -3 mismatches, -15 nonsense entries, -8 bad/shared pointers or duplicate extents, +6 when the
   lowest file block is the first after the directory.
4. The best 16 search results get the content checks (+3 text files that stay text to their ^Z, -8 that
   don't, +-1 plausible .COM starts, -4 files starting in E5 space), then are named after an identical library
   format if there is one.
5. Readings whose "directory" is mostly nonsense, or score below -5, are dropped. One per layout is kept.
6. Not CP/M at all: an empty directory fits every layout of the right shape, so a disc whose directory area is
   just E5 (an Acorn DFS disc formatted by Acorn's FORM, for one) used to open as an empty CP/M disc. Now (a) an
   empty reading whose directory is one repeated byte is dropped if the rest of the disc (outside that format's
   boot tracks; data before an `offset` counts) has 4 or more non-blank 128-byte records - formatting writes every
   track, so a fresh disc has none; and (b) `foreignFilesystem()` recognises Acorn DFS catalogues, ADFS old map
   (Hugo/Nick at 0x201 and 0x6FB) and D/E, and FAT (BPB, or a media byte + FF FF at 0 or 512, as on DOS 1.x and
   Acorn DOS Plus 800K): with no CP/M files that means "not CP/M" (pointing at Acorn Disk Wrangler); with files,
   the reading is shown but read-only until confirmed. A remembered (confirmed) format is exempt from both.
7. Confidence: `high` if nothing with a different file listing comes within 6 points; `medium` if a known
   format with a different layout lists the same files; `low` for empty directories. `safeToWrite` per rule 6.

Real-disc results (tests below): Osborne SD (osb1sssd, skew 2, including rotated tracks) and DD, Osborne 8"
mixed-density (FM boot track + 8x1024 MFM, skew 3, found by search), Kaypro 4 TeleDisk (kpiv), Zorba CP/M-86
TeleDisk (search), Heathkit H8D (search), z80pack 8" and 4MB hard discs; MS-DOS and UCSD p-System discs are
correctly not read as CP/M (an MS-DOS boot sector gets a hint pointing at Acorn Disk Wrangler).

## Build and test

**Version.** `version.pri` (qmake) and `version.cmake` (CMake) compile in `git describe --tags --always --dirty`
as APP_VERSION, shown in Help > About; "unknown" without git, or set it with `qmake APP_VERSION=...` /
`-DAPP_VERSION=...`. It is read when qmake/CMake runs, so re-run them after a new commit or tag. Both files are
shared with the other wrangler.

How to build on each platform and how releases work, for people: `docs/building.md` (shared); a 32-bit Windows 7
build (Qt 5.15, MinGW 8.1 32-bit; not tried yet): `docs/building-windows-32bit.md` (shared).

**Release builds and licences.** `.github/workflows/build.yml` builds Windows, macOS and Linux packages, linked to
Qt's shared libraries (keep it that way: the LGPL lets users replace them). Each is built twice: `ci/build.sh`
builds and deploys, `ci/collect-licences.sh` gathers the licences of every shipped library into
`licences-bundled.qrc`, and `ci/build.sh` again compiles them in (Help > Licences, `licences.cpp`). On Linux, `ci/make-deb.sh` then packages the same build as a `.deb` that depends on the system's Qt
(dpkg-shlibdeps; built on 22.04 against Qt 6.2, it installs on Ubuntu 22.04+ and Debian 12+ - the t64-renamed
libraries of 24.04 provide the old names; checked in clean 22.04-built / 24.04-installed chroots, Qt 6 and Qt 5),
and the workflow installs it and starts it. A new source
file in the .pro also goes in CMakeLists.txt and any `tests/*.pro` that build `mainwindow.cpp` (they need
`../licences.cpp`). `ci/`, `licences.*`, `licences/`, `buttonbar.h`, `appsettings.h` and the workflow are shared with Acorn Disk Wrangler: keep them
identical.
```
qmake cpmdiskwrangler.pro && make          # or CMake
tests/run_all.sh <build dir> [<real image dir from tests/fetch_real_images.sh>] [<Acorn Disk Wrangler source>]
```
Needs Qt 6 or Qt 5.15 widgets, and both must keep building (see Acorn Disk Wrangler's AGENTS.md for the
`#if QT_VERSION` places, the same here); the independent checks need **cpmtools** and **libdsk-utils** (Ubuntu packages) and Python 3.
Each test is its own `.pro` in `tests/`:

| Test | What |
|---|---|
| `detecttest [-v] [-x dir] images...` | prints the ranked readings and listing; `-x` extracts every file (compare with cpmtools: `tests/cmp_cpmtools.sh`) |
| `fstest <out> <format> raw\|imd\|edsk` | new disc, awkward file sizes, replace/delete/rename/attrs, commit, reopen **by detection**, verify; leaves material for `verify_independent.sh` |
| `fstest roundtrip images...` | unchanged images serialise byte-identically |
| `fstest atomic <out>` | failed adds leave nothing behind |
| `fstest library` | built-in definitions that replace cpmtools ones have the same layout and `dirblks` |
| `fstest addto <image> NAME size` | detect, add a file, save (for checking edits of real images with cpmtools) |
| `tests/verify_independent.sh <fstest outdirs>` | **independent**: Python IMD/DSK decoder + libdsk (where its geometry probe is right) must see the same sectors; cpmtools must read every file exactly and `fsck.cpm` must be clean |
| `guitest <dir> [osborne.imd [teledisk.td0]] [--adw dir]` | offscreen GUI: host drops (userN folders, flattening, .inf skipped), real drag events between windows, drag-out JSON, drops made by Acorn Disk Wrangler's code, read-only rules, format dialog, TeleDisk Save As, the menu and button order, Copy / Paste with the real keys (`QTest`: the `.pro` has `QT += testlib`); screenshots in `<dir>/shots`. `run_all.sh` fails unless it ends "all ok" |
| `tests/interop_adw` (qmake `ADW=<Acorn Disk Wrangler source>`) | built from **Acorn Disk Wrangler's own dragout.cpp**: `make <dir>` writes its drags (ADFS, DFS, FAT) for guitest; `read <json>` reads ours |
| `tests/acornz80_check.py <dsd> <outdir>` | **independent** Acorn Z80 reader (from Application Note 007 only); `run_all.sh` compares its files with ours for every `<real>/acorn/*.dsd` (the owner's discs, not in the repo) |
| `tests/make_foreign.py <dir>` | Acorn DFS discs (and copies with the catalogue wiped) that must all detect as not CP/M (`run_all.sh` checks) |
| `settingstest` | where settings live on Linux: an installed program has no portable mode; an AppImage keeps `<program>.ini` beside the `.AppImage` file (shared with Acorn Disk Wrangler) |
| `mkicon <dir>` | the PNG icons |

**Verify independently, not with our own code.** Our reader agreeing with our writer proves little. This
caught a real bug (a built-in `kpiv` without `dirblks 2`, so new files were put in a block the Kaypro reserves
for the directory) that every self-test had passed.

## The window (kept in step with Acorn Disk Wrangler)
- **Menus, button bar and context menu have the same order in both programs** (owner's rule; one program's extra
  items may sit in between). File: New window (Ctrl+Shift+N), New disc image (Ctrl+N), Open | Save, Save as,
  Revert | Create local config file | Close, Quit. Edit: View (F3), Extract to folder (Ctrl+E) | Add files
  (Ctrl+Shift+F) | Copy, Paste (`QKeySequence::Copy/Paste`: the platform's keys) | Rename (F2), Properties
  (Alt+Return), Delete (`deleteKeys()`: Delete, plus Cmd+Backspace on macOS) | Disc label. Tools: Check disc
  (Ctrl+K) first, then this program's tools, then the options with *Keep a .bak copy* last. Buttons: View, Extract,
  Add files, Rename, Properties, Delete | Format. Acorn Disk Wrangler adds Add folder and New directory after Add
  files, and disc-level items at the end of Edit. `guitest` here and `guitest2` there assert the order: change
  both programs together. Window title `<file> * - CP/M Disk Wrangler`; the status bar's label says what the disc
  is, then `  -  `-separated extras such as `read-only: <reason>`.
- **Copy and Paste** (the same design in Acorn Disk Wrangler): Copy exports the selection at once (`exportFiles`)
  and keeps the `XItem`s, so a paste gets what was selected then even if the disc has changed or the window has
  closed. The clipboard gets a `RetroFiles::DragOutMime` whose exporter returns that copy (files are written to a
  drag folder only when another program asks: text/uri-list, the retro-files JSON and, `setForClipboard`, GNOME's
  `x-special/gnome-copied-files`), plus `CPMDW_MIME_CLIP` = `pid:n`. Paste (`pasteItems`): our own token ->
  `importItems` from the kept copy (nothing written); else retro-files JSON -> `importRetroFiles` (another copy of
  this program, Acorn Disk Wrangler); else local files from a file manager (URLs or GNOME's list) ->
  `importPaths`. Target: the selection's user number and side, as Add files (none selected: each file keeps its
  user). A clipboard we still own is cleared at exit (its files go with the drag folders). Checked across real
  processes on X11 (Xvfb + xclip); not tested with Windows Explorer or macOS Finder.

## Testing pitfalls (things that bit us)
- **libdsk's geometry probe is unreliable** for many layouts (it guessed a Kaypro disc as 9 sectors from 1); its
  `dsktrans` output is only trusted when `dskid`'s geometry matches. It also fails ("Missing address mark") on
  some discs whose geometry it does get right. Hence the Python decoder.
- **cpmtools can't read the last blocks of some z80pack images** ("Bad parameter"): the files we read there were
  checked by eye (PROFILE.SUB is a SUBMIT script; WM.COM continues as Z80 code with its own messages).
- cpmtools reads `./diskdefs` before `/etc/cpmtools/diskdefs`: the scripts write one into a temporary folder.
- The cpmtools 4MB `z80pack-hd` listing crashes cpmtools on hd-tools.dsk (malloc error); our reading checks clean.
- **The format dialog's form must keep what was typed.** Deriving a format normalises it (one side forces side
  order `alt`), so refilling the form from a tried reading changed the next try: 1 side then 2 sides read nothing
  on an `outback` disc. A reading tried by hand is selected without refilling the form (`m_keepForm`), and one-sided
  formats leave the order shown alone (`guitest` checks this on a new Acorn Z80 disc).
- **Readings go stale.** `m_det` is scored when the disc is opened or made (a new disc: empty). The format dialog
  scores them again first (`MainWindow::rescoreReadings`, `Detect::reevaluate` keeps detection's bonuses).
- `slots` is a Qt macro: don't use it as a variable name.
- GUI tests: synthetic drops need DragEnter, DragMove and Drop sent to the tree's **viewport**; dialogs inside a
  drop handler are deferred with `QTimer::singleShot`; `MainWindow::autoAnswer` answers the questions in tests.
- A drag's temporary folder is deleted when the program exits (as the retro-files spec says): copy it if a test
  needs it afterwards.

## Real images used for testing (not in the repo)
`tests/fetch_real_images.sh <dir>` clones them: Myrcurial/o1next (about 40 real Osborne IMD/TD0 discs including
8-inch source discs whose files that project extracted independently - ours match byte for byte), udo-munk/z80pack
(8" CP/M 1.3-3, MP/M, 4MB hard discs, UCSD and FLP-80DOS negatives), lesbird/H89 and FujiNetWIFI (Heathkit H8D).

## Known gaps / ideas
Torch CP/N and other BBC Micro CP/M systems; per-head sector bases
(Kaypro 4 side 1); new discs with a filesystem per side; TeleDisk 1.x LZW; DateStamper stamps; CP/M 3 passwords (kept, not
checked); an MBASIC detokeniser for the viewer; writing TeleDisk.
