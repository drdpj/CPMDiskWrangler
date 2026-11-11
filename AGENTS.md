# AGENTS.md - working on CP/M Disk Wrangler

Read this before changing code: what the program is, where things are, the rules that must not be broken, how
the window works and how to build and test. Three companion documents hold the detail you only need for some work:

* **[docs/disc-formats.md](docs/disc-formats.md)** - the facts about each format and container, how sure we are of
  each, and the real discs they were checked on. Read it before touching the containers (`core/diskwrangler/device/`), `cpmformat`, `cpmfs`,
  `torchfs` or the diskdefs.
* **[docs/detection.md](docs/detection.md)** - how `detect.cpp` works out a disc's format, and why. Read it before
  changing a score, a bonus or a tie-break.
* **[docs/testing.md](docs/testing.md)** - every test, the independent checks and the real images used.

## What this is
A Qt 6 / Qt 5.15 (C++17) desktop tool for CP/M disc images: raw, ImageDisk, CPCEMU DSK / extended DSK, TeleDisk
(read). It also reads and writes Torch CP/N discs, whose files are CP/M files kept in Torch's own filesystem
(`torchfs.cpp`). It works out each disc's logical format, then lists, adds, extracts, renames and deletes files,
sets attributes, user numbers and CP/M 3 labels, and makes new discs. One disc per window; drag and drop between
windows, the desktop and other programs via the `retro-files` format shared with Acorn Disk Wrangler
(github.com/drdpj/AcornDiskWrangler), its sibling, which follows the same rules and style.

* **Licence and headers.** GPLv3-or-later. Every source file starts with the header naming
  `Copyright (C) 2026 Daniel Jameson` (`#` comments for scripts and build files); put it on any new file. The notice
  also appears in Help > About, `QMAKE_TARGET_COPYRIGHT` and the `.rc`.
* **Other people's work.** Anything taken from or closely modelled on someone else's work goes in
  `THIRD-PARTY-NOTICES.md` with its terms (and in About if it ships in the program); material only consulted for facts
  goes in its "Consulted" table.
* **Never commit third-party disc images**: the tests read them from outside the repo.
* **`core/` is the git submodule DiskWranglerCore**, shared with Acorn Disk Wrangler (clone with `--recurse-submodules`, or
  `git submodule update --init`). Never edit files under `core/` from here: change DiskWranglerCore, tag it, and move the
  submodule to the tag. The retro-files specification is `core/docs/retro-files-format.md`.

## Source map (program in `src/`, tests in `tests/`)
| File | Role |
|---|---|
| `core/diskwrangler/device/container.*` | `dw::device::Container`: the physical disc (tracks of sectors keyed by sector ID), the containers (raw, IMD, DSK, EDSK, TD0; `container_formats.cpp`), staged writes, serialising, the checked save (`commit`, `saveAs`, with `safesave.*`). A raw file is read from the file as needed. In DiskWranglerCore (this was `src/diskimage.*`) |
| `diskimage.h` | forwarding names (`Disk::Image` = `dw::device::Container`, `Disk::Kind`, ...) for the tests only; program code uses the core's names |
| `natchars.h/.cpp` | `Nat`: ISO 646 national character sets (map `@ [ \ ] ^ ` { | } ~`, never `#` `$`), host<->disc name/text conversion, evidence scan and language guess, the chooser dialog. Per disc, remembered in settings (`charset/<sha1 of path>`); Volume accepts `[ \ ] |` in names only while a national set is chosen (`setExtraNameChars`). Not done: SE-C/DEC variants, UK (`#`->£) |
| `core/diskwrangler/device/lzhuf.cpp` | TeleDisk 2.x "advanced compression" decoder (Okumura's LZHUF; tables generated, not copied). In DiskWranglerCore |
| `cpmformat.h/.cpp` | `Cpm::Format`: a diskdef plus the derived DPB (DSM, EXM, pointer width, skew table, `locate()`); the diskdefs parser; the library. Acorn Z80's DFS catalogue comes from DiskWranglerCore's DFS module |
| `cpmfs.h/.cpp` | `Cpm::Volume`: directory, files, extents, CP/M 3 / P2DOS stamps and labels, add/delete/rename/attrs, `check()`, `makeBlank()` |
| `detect.h/.cpp` | working out the format: candidates, scoring, ranking, confidence, `safeToWrite` |
| `cpmvolume.h/.cpp` | DiskWranglerCore's filesystem interface: `Cpm::verdict` (detection by tiers: open, ask, or hand to Acorn Disk Wrangler, used by `openImage`), `CpmVolume` (a `Cpm::Volume` as a `dw::fs::Volume`, user areas as namespace levels) and `OpenedDisc` (the disc as a `dw::fs::Disc`; `voltest` and the command-line program of step 4b) |
| `resources/builtin-diskdefs` (in `src/`) | this program's definitions (Acorn Z80, Amstrad, Kaypro) and descriptions for cpmtools' ones |
| `resources/cpmtools-diskdefs` (in `src/`) | cpmtools 2.23's diskdefs file, unchanged (Michael Haardt and contributors, GPL v3+; see THIRD-PARTY-NOTICES.md) |
| `mainwindow.*`, `mainwindow_p.h` | window, on DiskWranglerCore's window kit since step 5c (`MainWindow` derives from `dw::ui::DiscWindow`, its document is a `Cpm::OpenedDisc`; the kit does the rows, selection, title, Save / Save As / Revert, Delete, Extract and, since step 5e, every import through its import session (`dw::ui::ImportSession`), the window fills its hooks), `DropTree` (the kit's `VolumePane` plus drag and drop), importing/exporting, Copy / Paste, menus; `mainwindow_p.h` has the shared tree helpers (same as Acorn Disk Wrangler's) and `XItem` |
| `formatdialog.*` | Tools > Disc format (ranked readings, evidence, preview, try a format by hand) |
| `newdisc.*` | File > New disc image |
| `dragout.*` | `retro-files` JSON writer/reader, drag-out data, temporary drag folders |
| `viewer.*`, `viewtext.*` | the text/hex viewer (CP/M `^Z`, WordStar bit 7) |
| `core/diskwrangler/ui/licences.*` | Help > Licences: the texts compiled in from `src/licences.qrc` (this program's own, kept here) and `licences-bundled.qrc` in a release build. In DiskWranglerCore |
| `core/diskwrangler/app/appsettings.h` | where settings live: the system's place, or `<program>.ini` beside the program (portable; for an AppImage, beside the `.AppImage` file - `$APPIMAGE` - not in its temporary mount; a program installed on Linux, or by the Windows installer - `installedHere()`: Inno Setup's `unins000.dat` beside it -, has no portable mode, so File > Create local config file isn't shown). Use `AppSettings` instead of `QSettings` in program code (tests may use `QSettings` to clear the system store). Header only; in DiskWranglerCore |
| `core/diskwrangler/ui/buttonbar.h` | the main window's button bar: real QPushButtons kept in step with their QActions (a QToolBar's own buttons draw as flat text on macOS). Header only; in DiskWranglerCore |
| `core/diskwrangler/app/fileopen.h` | macOS opens files from the Finder (double-click, Open With, the Dock icon) by sending the running program a `QFileOpenEvent`, never a command-line argument: `FileOpen::FileOpenFilter<MainWindow>` (installed on the application in `main.cpp`) opens the image like File > Open - in an empty window, the active one first, else a new window; one already open just comes forward. Windows and Linux never send it. `guitest2` / CP/M Disk Wrangler's `guitest` send it. Header only; in DiskWranglerCore |
| `torchfs.h/.cpp` | Torch CP/N's filesystem (format key `torchcpn`): `Torch::walk`/`scan` (shared with detection) and the `Cpm::Volume` methods `torch...` that every public `Volume` method hands over to when `Format::torch` is set, so the window, drags, Copy/Paste and the tests see one `Volume` |
| `icon.*`, `icons/` (top level) | the 8-inch floppy icon (QPainter); PNGs from `tests/mkicon`, the `.ico` made from them with Pillow, and a hand-written SVG that renders pixel-identical (check with cairosvg if you change either) |

**Container by content, not by name.** `Container::open` recognises ImageDisk, DSK / extended DSK and TeleDisk by their
headers and treats anything else as raw, whatever the suffix (`.dsk` is used for raw dumps by many emulators). Save As
starts on the filter for the kind the image already is, and the kind saved is the filter chosen, never the typed name.

## The rules that must not be broken
1. **Nothing is written to the file except by `Container::commit()` / `saveAs()`.** Filesystem code writes through
   `Container::write`, which stages; every read overlays the staging.
2. **Sectors are addressed by sector ID order, never file order.** `Track::byId` maps a logical index to the
   recorded order. ImageDisk/TeleDisk tracks are often rotated (Osborne single density: IDs 2,3,...,10,1);
   reading in file order gives a plausible but wrong directory. (Others have made this mistake: see the
   o1next repository's notes.)
3. **Validate before writing.** `Cpm::OpenedDisc::save` (the window's and `cpmdw`'s save) runs `check()` on every side and refuses on any error, and refuses
   if the file changed on disk since it was opened. Commit writes a complete new file beside the original, reads it
   back (every staged sector, then `OpenedDisc::checkWritten`: each side's directory attached again, checked and its files counted),
   then makes the optional `.bak` (a hard link where possible) and atomically replaces the original, then reloads
   it (DiskWranglerCore's `Container::SaveOptions`). Any failure before the replace leaves the original untouched.
4. **A save with nothing changed changes nothing**, and unchanged parts stay byte-identical: IMD tracks without
   staged sectors are re-emitted from their original bytes (header and comment too); DSK/EDSK and raw files are
   patched in place. `fstest roundtrip` checks this on every image it is given.
5. **Every mutation is atomic**: `TXN_BEGIN` in `cpmfs.cpp` snapshots the staging and restores it on failure. A
   failed add (disc full, directory full) leaves no trace (`fstest atomic`).
6. **Changes need a safe reading** (`MainWindow::readOnlyReason`, from `OpenedDisc::readOnlyWhy`): a known format with no competing reading,
   a format the user confirmed (remembered per file in QSettings), or a disc made here. A reading found only by
   searching is shown but read-only until confirmed. A directory that fails `check()` is read-only. TeleDisk
   files are read-only (Save As). Don't weaken this: a wrong directory size or block size means new files are
   written over data, or written where the real machine won't see them.
7. **The physical shape of a new disc must match the real thing.** A built-in definition that replaces a
   cpmtools one must describe exactly the same layout, including `dirblks` (`fstest library` checks this). A cpmtools
   definition whose layout no real disc has is left out with a `withdraw NAME "why"` line in the built-in file (e.g.
   `microbee40`, a "40 track" disc of 80 cylinders on one side); `fstest library` checks it is gone.

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
- **Copy and Paste** (the same design in Acorn Disk Wrangler): Copy exports the selection at once
  (`exportForCopy`, the kit's `exportItems` with a Torch file sent as `cpm`; View, drags and drops on another window
  use it too) and keeps it as `dw::TransferItem`s, so a paste gets what was
  selected then even if the disc has changed or the window has closed. The clipboard gets a `dw::DragOutMime`
  (`RetroFiles::dragSpec` says how) whose exporter returns that copy (files are written to a
  drag folder only when another program asks: text/uri-list, the retro-files JSON and, `setForClipboard`, GNOME's
  `x-special/gnome-copied-files`), plus `CPMDW_MIME_CLIP` = `pid:n`. Paste (`pasteItems`): our own token ->
  `importInto` from the kept copy (nothing written); else retro-files JSON -> `importRetroFiles` (another copy of
  this program, Acorn Disk Wrangler); else local files from a file manager (URLs or GNOME's list) ->
  `importPaths`. Target: the selection's user number and side, as Add files (none selected: each file keeps its
  user). A clipboard we still own is cleared at exit (its files go with the drag folders). Checked across real
  processes on X11 (Xvfb + xclip); not tested with Windows Explorer or macOS Finder.

## Names and character sets
* Imports (step 5e): every one - Add files, drops, Paste, retro-files, drops from another window - ends in `importInto` (a user area or -1, a side), which runs the kit's `importItems` (an `ImportSession` on the core's `Importer`: the conflict question, errors collected and the rest imported, one `Disc::transact` per file). CP/M's rules are the session's hooks in `MainWindow::importHooks`: the clash suffixes taken off (`RetroFiles::withoutClashSuffixes`, shared with `cpmdw`), the character-set question, `userN` folders as user areas (`RetroFiles::userFromFolderName`), `placeIncoming` (the drop target's user wins over the item's; names as below; each name made valid is listed in the summary), the attributes the item brings (none from the host, so a replaced file's SYS / ARC aren't kept, unlike `cpmdw`), a file existing only under exactly its name (`exactNames`: the directory's bytes, as `Store::indexOf` compares them), and a name made twice in one import asked about as a file that exists (`renameRepeats` off). The question (`askReplace`) and the summary (`importSummary`) keep their texts. Host files are read by the window's own `hostItem` (`cpmdw`'s reader stops at the first file too large and skips `.inf` sidecars inside folders; the window reports and goes on, and skips only `.inf` files dropped by name).
* National character sets and retro-files: `XItem::name` is always the raw disc name (the spec says bytes unchanged); `XItem::hostName` is the national-set name used only for the file written to the host. `placeIncoming` keeps a `cpm` source name exactly (allowing `[ \ ] |` for it), converts only host file names (`srcFs` empty) through the disc's set, and treats other filesystems' names as bytes.
* The status bar (right of the line): one button, `Names: <character set>, upper case|any case` (+ "(national letters?)" when `Nat::scan` suspects one), whose menu holds the lower-case option and the Character set dialog (the same QActions as in Tools; `showLower` / `refreshNamesButton` keep it current). The status line itself is a `StatusLabel` (mainwindow_p.h) given several versions of its text, longest first (`setVariants`): it shows the longest that fits, ends the last with "...", and keeps the longest in its tooltip - never put a long message in a plain QLabel there. Settings keys per disc: `charset/<sha1>`, `lowercase/<sha1>`.
* Adding host files with letters ASCII can't hold (`askNatForNames`, once per import: the session's `beforeRun`): a question, then the Character set dialog constructed with the incoming names - it says which letters, which sets have a place for them (`Nat::candidatesFor`, in list order; ä ö are both se-fi and de), preselects the first and previews each name as it would be written. `autoAnswer` skips the question (tests).
* Lower-case names: `Volume` upper-cases names unless its extra name characters include `a`-`z` (`Nat::lowerLetters()`); MainWindow sets that from `m_lowerOk` (Tools > Allow lower-case file names + status bar tick box; auto from `Nat::usesLowerCase` = any lower-case name; a new disc starts off, else the per-disc `lowercase/<sha1>` setting). Drag-out keeps a name with lower-case letters as is, else lower-cases it. A name from a `cpm` source is never case-changed.
* Host names on extraction (`writeHost`): names with lower-case letters keep them; upper-case names are lower-cased unless that clashes, then they keep their case; only a case-insensitive host (found by probing the folder) adds `_2`. An import strips such a `_N` again when what is left matches another dropped name (case-insensitively), so a real `foo_1.txt` stays.
* National character sets are display/transfer only: the directory always holds the raw 7-bit bytes. Tests: `tests/chartest` (needs COMAL discs in `real/nat` for the guess), and a section in guitest.

## Build
```
qmake cpmdiskwrangler.pro && make          # or: cmake -S . -B build && cmake --build build
```
The `.pro`, `CMakeLists.txt`, `.rc`, `.desktop` and `icons/` stay at the top level (the CI scripts look for `./*.pro`);
the program's sources and its compiled-in diskdefs are in `src/`. A new source file goes in the `.pro`, in
`CMakeLists.txt`, and in any `tests/*.pro` that builds `mainwindow.cpp` (they also `include(../core/diskwranglercore.pri)`).

* **Qt 6 and Qt 5.15 must both keep building** (see Acorn Disk Wrangler's AGENTS.md for the `#if QT_VERSION`
  places; the same here). qmake uses whichever `qmake` you run; CMake prefers Qt 6 and `-DUSE_QT5=ON` forces 5.
* **Version.** `version.pri` (qmake) and `version.cmake` (CMake) compile in `git describe --tags --always --dirty` as
  APP_VERSION, shown in Help > About; "unknown" without git, or set it with `qmake APP_VERSION=...` /
  `-DAPP_VERSION=...`. It is read when qmake/CMake runs, so re-run them after a new commit or tag.
* **Releases.** `.github/workflows/build.yml` builds Windows, macOS and Linux packages linked to Qt's *shared*
  libraries (keep it that way: the LGPL lets users replace them). Each is built twice so that the licences of every
  shipped library (`core/ci/collect-licences.sh` -> `licences-bundled.qrc`) are compiled in (Help > Licences,
  `core/diskwrangler/ui/licences.cpp`). Linux also gets a `.deb` against the system's Qt (`core/ci/make-deb.sh`; built on Ubuntu 22.04, it
  installs on 22.04+ and Debian 12+); Windows, for a tag or Run workflow only, an Inno Setup installer
  (`core/ci/installer.iss`; each program's AppId in it is fixed for good). A tag `vX.Y.Z` publishes a GitHub release
  whose notes are `release-notes/<tag>.md` when that file exists. The details for people are in `core/docs/building.md`;
  a 32-bit Windows 7 build (not tried yet) in `core/docs/building-windows-32bit.md`.
* **Still identical copies in both programs - change both:** `.github/workflows/build.yml`, `src/licences.qrc`,
  `licences/`, `version.pri` and `version.cmake`. (The CI scripts, the building and retro-files documents, the settings
  helper, the button bar, the Finder-open filter, the licences viewer and `settingstest` are in the core.)

## Test
```
tests/run_all.sh <build dir> [<real image dir from tests/fetch_real_images.sh>] [<Acorn Disk Wrangler source>]
```
builds every test and runs them all, with the independent checks; it needs **cpmtools**, **libdsk-utils** (Ubuntu
packages) and Python 3, and ends "ALL PASSED". [docs/testing.md](docs/testing.md) lists each test. For a UI change,
`guitest` is the one that matters (menus, buttons, drops, drags, the format dialog, Copy / Paste; screenshots in
`<dir>/shots`).

## Pitfalls (things that bit us)
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

## Known gaps / ideas
Other BBC Micro CP/M systems; undeleting Torch CP/N files (FFFF entries); a check of Torch CP/N discs written here on a real Torch (or an emulator running CP/N);
unsqueezing .?Q? files in the viewer; per-head sector bases
(Kaypro 4 side 1); new discs with a filesystem per side; TeleDisk 1.x LZW; DateStamper stamps; CP/M 3 passwords (kept, not
checked); an MBASIC detokeniser for the viewer; writing TeleDisk.
