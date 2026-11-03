[![Latest release](https://img.shields.io/github/v/release/drdpj/CPMDiskWrangler?label=release)](https://github.com/drdpj/CPMDiskWrangler/releases/latest)

# CP/M Disk Wrangler

View and edit CP/M disc images. It reads raw sector dumps, ImageDisk, DSK (standard and extended) and TeleDisk
images, **works out each disc's format from its contents**, and lets you add, extract, view, rename and delete files,
change user numbers and attributes, set CP/M 3 labels, convert between image types, and make new discs in about 140
formats. Files drag between discs, to and from your desktop, and to and from
[Acorn Disk Wrangler](https://github.com/drdpj/AcornDiskWrangler) and other tools that speak
[retro-files](docs/retro-files-format.md).

## Download
Get the latest build from the [Releases page](https://github.com/drdpj/CPMDiskWrangler/releases/latest).
None of the builds are signed, so your system may warn you the first time you run it.

* **Windows** (10 or later, 64-bit): run the `...-setup.exe` installer (it installs into Program Files; without
  administrator rights, choose to install it just for you), or unzip the `.zip` anywhere and run
  `cpmdiskwrangler.exe`. If Windows warns you, choose **More info**, then **Run anyway**.
* **macOS** (12 or later, Intel or Apple Silicon): open the `.dmg` and drag the app to Applications. The first time,
  right-click it and choose **Open**.
* **Linux** (x86_64):
  * **Ubuntu 22.04+ or Debian 12+:** `sudo apt install ./cpmdiskwrangler-<version>-linux-amd64.deb` (the `./`
    matters). It uses the system's Qt and adds the program to the applications menu. Remove it with
    `sudo apt remove cpmdiskwrangler`.
  * **Any other distribution:** make the `.AppImage` executable (`chmod +x`) and run it. It doesn't add itself to
    the applications menu, so docks show a generic icon unless a tool such as Gear Lever or AppImageLauncher
    integrates it.

Help > About shows the version; Help > Licences shows the licences of the program and everything shipped with it.

## What it opens
| Image type | Read | Write |
|---|---|---|
| Raw sector image (`.img`, `.dsk`, `.h8d`, ...) | yes | yes |
| ImageDisk (`.imd`) | yes | yes - everything it doesn't change is kept exactly |
| DSK, standard and extended (`.dsk`; the format CPCEMU introduced, now used for many machines) | yes | yes - only the changed sectors are rewritten |
| TeleDisk (`.td0`) | yes | no - use Save As to make an ImageDisk, DSK or raw copy, then edit that |

The image type comes from the file's contents, never its name: many emulators use `.dsk` for a plain sector dump,
so a `.dsk` file is read as a DSK image only if it has a DSK header.

It copes with interleaved and rotated tracks, with discs whose track 0 is single density (FM) and the rest double
density (Osborne, Research Machines 480Z, Vector Graphic), and with double-sided discs that hold a separate
filesystem on each side (shown as **Side 0** and **Side 1**). It also reads and writes **Torch CP/N** discs (the
Torch Z80 second processor for the BBC Micro), whose CP/M files are kept in Torch's own filesystem rather than a
CP/M directory; they have no labels or time stamps, and sizes are kept in whole 128-byte records.

## How it finds the format
A CP/M disc doesn't record its own layout, so the program tries the possibilities - block size, directory size,
reserved tracks, skew, side order - and keeps the reading that makes sense of the directory and the files. Known
formats count for more: about 140 are built in, including cpmtools' library, the Amstrad CPC, PCW and +3, Kaypro,
Osborne, Research Machines 480Z, Vector Graphic, Sharp, Acorn's Z80 second processor and Torch CP/N.

**Tools > Disc format** shows the ranked readings and the evidence for each, lets you choose one yourself (it's
remembered for that file), and copies any of them as a cpmtools `diskdef`. **Tools > Add a diskdefs file** adds
your own definitions.

So that a wrong guess can't damage a disc, a disc can only be changed when its reading is certain: a known format
with no close rival, one you've confirmed, or a disc made by this program. Otherwise it opens **read-only** and the
status bar says why.

## Using it
* **One disc per window.** File > Open uses a new window if this one already has a disc. Disc images dropped on an
  empty window open; dropped on a window with a disc, they're copied onto it as files.
* **Files are grouped by user number.** Drop files on a *User N* row to give them that user number. Host folders
  named `user3`, `user 3`, `u3` or `3` go to that user number; other folders are flattened.
* **Add files** by dropping them from your file manager, or by pasting files copied there. Names become CP/M 8.3
  (upper case unless lower case is allowed - see below; characters CP/M can't use become `-`).
* **Extract files** by dragging them out or by copying and pasting them into another program. A tool that supports
  retro-files receives their user number and attributes too.
* **Copy and Paste** (Ctrl+C / Ctrl+V, Cmd+C / Cmd+V on a Mac) work between windows, between the two sides of a
  disc, with Acorn Disk Wrangler and with your file manager. A paste goes to the selected user number; with none
  selected, each file keeps its own.
* **View** (double-click or F3) shows text up to the end-of-file mark (optionally ignoring WordStar's high bit), or
  hex.
* **Rename** (F2). **Properties** (Alt+Enter) sets attributes (R/O, SYS, ARC, F1'-F4') and the user number, for one
  file or many. **Edit > Disc label** for CP/M 3 discs.
* **Tools > Check disc** checks the directory; **Tools > Disc information** shows the geometry and format.
* **File > New disc image** makes a disc in any format in the library, as ImageDisk, extended or standard DSK, or
  raw, optionally with CP/M 3 time stamps and a label. Tick *Double-sided* for a Research Machines disc with a
  separate filesystem on each side. **File > Save As** converts between image types (TeleDisk to ImageDisk, say).
* **Save** (Ctrl+S). Nothing is written until you save. The directory is checked first, and it won't save if
  another program changed the file meanwhile. A `.bak` copy of the previous file is kept unless you turn that off
  (Tools).

### Lower-case names and national character sets
The **Names:** button at the bottom right of the window shows how names on this disc are treated, and its menu
holds the two options below (also in the Tools menu). Both are remembered for each disc.

* **Allow lower-case file names.** CP/M stores whatever bytes a program gives it, so `readme.txt` and `README.TXT`
  are different files; names are upper case only by convention, because the command prompt upper-cases what you
  type. This is ticked for a disc that already has a lower-case name. Ticked, names you add or rename keep their
  case; unticked, they're made upper case as CP/M's own tools would. When extracting, upper-case names are written
  in lower case unless that would clash with another name; a host that can't tell the two apart (Windows, macOS)
  gets `_2` added, which is taken off again when the files are dropped back.
* **Character set for names and text.** Danish/Norwegian, Swedish/Finnish, German, French, Italian, Spanish and
  Portuguese machines used `[ \ ] { | }` for letters such as Æ Ø Å æ ø å, so a file called `br|kregning` is really
  *brøkregning*. With a set chosen, names and text are shown, extracted and added in those letters; the disc itself
  is unchanged. The program suggests a set when names or text use those characters between letters. `#` and `$` are
  never mapped. Dragged between programs, a name travels as the disc holds it, so files moved between CP/M discs
  keep their names exactly.

### Settings, and carrying the program with you
Settings (the Tools options, the last folders used and the formats you confirmed for each image) live in the usual
place for your system: the registry on Windows (`HKEY_CURRENT_USER\Software\cpmdiskwrangler\cpmdiskwrangler`),
`~/Library/Preferences/com.cpmdiskwrangler.cpmdiskwrangler.plist` on macOS and
`~/.config/cpmdiskwrangler/cpmdiskwrangler.conf` on Linux.

To keep them with the program instead - on a USB stick, say - choose **File > Create local config file**. It writes
`cpmdiskwrangler.ini` beside the program (beside the `.exe`, the `.app` or the `.AppImage`) with a copy of the
current settings, and uses it from then on; an empty file of that name does the same. Delete it to go back to the
system's settings. Help > About says which is in use. Copies installed by the Windows installer or the `.deb` always
use the system's settings, so they don't offer this.

## Drag and drop between disc tools
An ordinary drag between programs carries only a file's name and contents, but a file on a retro disc has more that
matters: an Acorn file's load and execution addresses (without them a program may not run), its access bits and DFS
directory letter, DOS attributes, a CP/M user number. Both Wranglers implement
**[retro-files](docs/retro-files-format.md)** (`application/x-retro-files+json`), an open format that carries these
too:

* **Nothing is lost between disc tools.** Drag a file between Acorn Disk Wrangler and CP/M Disk Wrangler, between two
  copies of either, or to any other tool that adopts the format, and its attributes go with it. The receiving
  program converts what the destination can hold (an Acorn file's lock becomes CP/M's read-only flag, for example)
  instead of guessing from a bare file.
* **Nothing else breaks.** The same drag carries ordinary files, so dropping on your desktop, a file manager or any
  other program works as usual.
* **Any tool can join in.** It is a small, versioned JSON document with attributes grouped by filesystem (`acorn`,
  `fat`, `cpm`): a tool for another kind of disc adds its own group and reads the ones it understands. The
  [specification](docs/retro-files-format.md) explains the format and why it exists.

## Limitations
* TeleDisk images can't be saved as TeleDisk, and TeleDisk 1.x "advanced compression" isn't supported.
* A raw image can't hold a disc whose track 0 differs from the rest; use ImageDisk or DSK for those.
* Copy-protected tracks (duplicate sector numbers) are read but never written.
* DateStamper's `!!!TIME&.DAT` time stamps are kept but not shown; CP/M 3 passwords are kept but not checked.
* Deleted Torch CP/N files can't be recovered yet, and discs written here haven't yet been checked by a real Torch.

## Building from source
You need Qt 6 (or Qt 5.15) with its widgets module, and a C++17 compiler.

    qmake cpmdiskwrangler.pro && make              # or: cmake -S . -B build && cmake --build build
    ./cpmdiskwrangler [image ...]                  # each image opens in its own window

[docs/building.md](docs/building.md) covers Linux installs, Windows (MSYS2), macOS, Qt 5, and how the release
builds are made and carry their licences.

Working on the code? Start with [AGENTS.md](AGENTS.md): the design rules, the source map, the window and the tests.
The formats are described in [docs/disc-formats.md](docs/disc-formats.md) and format detection in
[docs/detection.md](docs/detection.md).

## Licence
Copyright (C) 2026 Daniel Jameson. Licensed under the GNU General Public License v3.0 or later - see
[LICENSE](LICENSE). Includes the disc definitions from cpmtools 2.23 (Michael Haardt and contributors, GPL v3 or
later); the TeleDisk decoder follows LZHUF by Haruyasu Yoshizaki and Haruhiko Okumura. Uses Qt under the LGPL v3.
Everything third-party, and its terms, is listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
