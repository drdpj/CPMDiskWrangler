# CP/M Disk Wrangler

View and edit CP/M disc images. It reads raw sector dumps, ImageDisk, CPCEMU and extended DSK, and TeleDisk images,
**works out the disc's format from its contents**, and lets you add, extract, view, rename and delete files, change
user numbers and attributes, set CP/M 3 labels, convert between image types, and make new discs in about 140
formats. Files can be dragged between discs, to and from your desktop, and to and from
[Acorn Disk Wrangler](https://github.com/drdpj/AcornDiskWrangler) and other tools that support the
[retro-files](docs/retro-files-format.md) drag-and-drop format.

## Download
Get the latest build for Windows, macOS or Linux from the
[Releases page](https://github.com/drdpj/CPMDiskWrangler/releases).

* **Windows** (10 or later, 64-bit): unzip anywhere and run `cpmdiskwrangler.exe`.
* **macOS** (12 or later, Intel or Apple Silicon): open the `.dmg` and drag the app to Applications. It isn't
  signed by Apple, so the first time, right-click it and choose **Open**.
* **Linux** (x86_64): the `.AppImage` runs as it is - make it executable (`chmod +x`) and start it.

Help > About shows the version, and Help > Licences the licences of the program and everything distributed with it.

## What it opens
| Image type | Read | Write |
|---|---|---|
| Raw sector image (`.img`, `.dsk`, `.h8d`, ...) | yes | yes |
| ImageDisk (`.imd`) | yes | yes - everything it doesn't change is kept exactly |
| CPCEMU and extended DSK (`.dsk`) | yes | yes - only the changed sectors are rewritten |
| TeleDisk (`.td0`) | yes | no - use Save As to make an ImageDisk, DSK or raw copy, then edit that |

It copes with interleaved and rotated tracks, with discs whose track 0 is single density (FM) and the rest
double density (Osborne, Research Machines 480Z), and with double-sided discs that hold a separate filesystem on
each side (shown as **Side 0** and **Side 1**).

## How it finds the format
A CP/M disc doesn't record its own layout, so the program tries the possibilities - block size, directory size,
reserved tracks, skew, side order - and keeps the reading that makes sense of the directory and the files. Known
formats count for more: about 140 are built in, including cpmtools' library, the Amstrad CPC, PCW and +3, Kaypro,
Osborne, Research Machines 480Z and Acorn's Z80 second processor.

**Tools > Disc format** shows the ranked readings and their evidence, lets you pick one yourself (it's remembered
for that file), and copies any of them as a cpmtools `diskdef`. Tools > Add a diskdefs file adds your own
definitions.

To keep a wrong guess from damaging a disc, a disc can only be changed when the reading is certain: a known
format with no close rival, one you've confirmed, or a disc made by this program. Otherwise it opens
**read-only** and the status bar says why.

## Using it
* **One disc per window.** File > Open opens a new window if this one already has a disc in it. Dropping disc
  images on an empty window opens them; dropped on a window with a disc, they're copied onto it as files.
* **Files are grouped by user number.** Drop files on a *User N* row to put them in that user number. Host
  folders named `user3`, `user 3`, `u3` or `3` go to that user number; other folders are flattened.
* **Add files** by dropping them from your file manager. Names become CP/M 8.3 (upper case; characters CP/M can't
  use become `-`).
* **Extract files** by dragging them out to your desktop or another program. Dragged to a tool that supports
  retro-files, they keep their user number and attributes (see below).
* **View** (double-click, F3): text up to the end-of-file mark (optionally ignoring WordStar's high bit), or hex.
* **Rename** (F2), **Properties** (Alt+Enter) for attributes (R/O, SYS, ARC, F1'-F4') and user number, for one
  file or many. Edit > Disc label for CP/M 3 discs.
* **Tools > Check disc** checks the directory; **Disc information** shows the geometry and format.
* **File > New disc image**: any format in the library, as ImageDisk, extended DSK or raw, optionally with CP/M 3
  time stamps and a label. **File > Save As** converts between image types (e.g. TeleDisk to ImageDisk).
* **Save** (Ctrl+S). Nothing is written until you save; the directory is checked first, it won't save if the
  file was changed by something else meanwhile, and a `.bak` copy of the previous file is kept (Tools; on unless
  you turn it off).

### Settings, and carrying the program with you
Options (Tools menu), the last folders used and the formats you confirmed for each image are saved in
the usual place for your system: the registry (`HKEY_CURRENT_USER\Software\cpmdiskwrangler\cpmdiskwrangler`) on Windows,
`~/Library/Preferences/com.cpmdiskwrangler.cpmdiskwrangler.plist` on macOS, `~/.config/cpmdiskwrangler/cpmdiskwrangler.conf` on Linux.
To keep them with the program instead - on a USB stick, say - choose **File > Create local config file**: it
makes **`cpmdiskwrangler.ini`** in the program's folder (beside the `.exe` on Windows, beside the `.app` on macOS) with
a copy of the current settings, and uses it from then on. (Putting an empty file of that name there by hand does
the same.) Delete the file to go back to the system's settings. Help > About says which is in use.

## Drag and drop between disc tools
Both Wranglers implement **[retro-files](docs/retro-files-format.md)** (`application/x-retro-files+json`), an open
format for dragging files out of disc images and into other programs. An ordinary drag between programs carries
only a file's name and contents, but a file on a retro disc has more than that, and it matters: an Acorn file's
load and execution addresses (without them a program may not run, and they also hold its type and date), access
bits, a DFS directory letter, DOS attributes, a CP/M user number. retro-files carries those too. That means:

* **Files move between disc tools without losing anything.** Drag a file from a disc in one program that
  supports the format to a disc in another - between Acorn Disk Wrangler and CP/M Disk Wrangler, between two
  copies of either, or to any other tool that adopts it - and its attributes go with it. The receiving program
  converts what the destination disc can hold (for example, an Acorn file's lock becomes CP/M's read-only flag)
  instead of guessing from a bare file.
* **Nothing else breaks.** The same drag also carries ordinary files, so dropping on your desktop, a file
  manager or any other program works as usual.
* **Any tool can join in.** It is a small, versioned JSON document, with attributes grouped by filesystem
  (`acorn`, `fat`, `cpm`): a tool for another kind of disc adds its own group and reads the groups it
  understands. The [specification](docs/retro-files-format.md) explains the format and why it exists.

## Limitations
* TeleDisk images can't be saved back as TeleDisk, and TeleDisk 1.x "advanced compression" isn't supported.
* A raw image can't hold a disc whose track 0 differs from the rest; use ImageDisk or DSK for those.
* New discs with a separate filesystem on each side can't be made (copy an existing image instead).
* Copy-protected tracks (duplicate sector numbers) are read but never written.
* DateStamper's `!!!TIME&.DAT` time stamps are kept but not shown; CP/M 3 passwords are kept but not checked.

## Building from source
Needs Qt 6 (or Qt 5.15) with its widgets module, and a C++17 compiler.

    qmake cpmdiskwrangler.pro && make              # or: cmake -S . -B build && cmake --build build
    ./cpmdiskwrangler [image ...]                  # each image opens in its own window

See [docs/building.md](docs/building.md) for Windows, macOS, Qt 5, the release builds and how they carry their
licences.

## Licence
Copyright (C) 2026 Daniel Jameson. Licensed under the GNU General Public License v3.0 or later - see
[LICENSE](LICENSE). Includes the disc definitions from cpmtools 2.23 (Michael Haardt and contributors, GPL v3 or
later); the TeleDisk decoder follows LZHUF by Haruyasu Yoshizaki and Haruhiko Okumura. Uses Qt under the LGPL
v3. Everything third-party, and its terms, is listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

Working on the code? See [AGENTS.md](AGENTS.md): design rules, format notes, detection, tests and pitfalls.
