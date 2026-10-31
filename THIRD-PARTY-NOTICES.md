# Third-party notices

CP/M Disk Wrangler is Copyright (C) 2026 Daniel Jameson and is licensed under the GNU General Public License,
version 3 or (at your option) any later version (see `LICENSE`). This file lists everything in it, or used to make
or test it, that comes from someone else, and on what terms.

## Included in the source

### cpmtools disc definitions - `resources/cpmtools-diskdefs`
The `diskdefs` file from **cpmtools 2.23** (Ubuntu package 2.23-5build1), included unchanged and compiled into the
program as a resource.

* Copyright 1997-2007 Michael Haardt <michael@moria.de> and the contributors of the individual definitions.
* Licence: GNU General Public License, version 3 or (at your option) any later version - the same licence as this
  program. Upstream: <http://www.moria.de/~michael/cpmtools/>.

`resources/builtin-diskdefs` is this program's own file. It uses cpmtools' syntax, and several of its definitions
copy the parameters of cpmtools definitions of the same name (`cpcdata`, `cpcsys`, `pcw`, `kpii`, `kpiv`, `rm-dd`,
`rm-qd`), as each one's `source` line says.

The Microbee definitions `microbee-ds80` and `microbee-ds84` in that file take their parameters (block size,
directory size, reserved tracks, skew, and the data-track sector numbering) from the `ds80` and `ds84` entries of the
`diskdefs` in **uBee's patched cpmtools 2.10j** (uBee, 2016, for the Microbee Software Preservation Project,
<http://www.microbee-mspp.org.au/repository>), as collected in snakebyte69's
<https://github.com/snakebyte69/CPMTools-Microbee-Patch>. That patch is GPL, like cpmtools. The `datasect` key
(first sector ID on the data tracks) is that patch's key of the same name, implemented here independently in
`Format::dataId` and `makeBlank`; no code was taken from the patch. Both definitions were checked against real discs.

### LZHUF - `lzhuf.cpp`
The TeleDisk "advanced compression" decoder implements the LZHUF algorithm and follows the structure of
**LZHUF.C**, written by **Haruyasu Yoshizaki** (1988, with English comments by **Haruhiko Okumura**, 1989),
which builds on Okumura's LZSS. Okumura released his LZSS code into the public domain. LZHUF.C was published
without any licence statement and has been redistributed and adapted freely for over thirty years (TeleDisk itself,
LHA, MAME's and libdsk's TeleDisk readers); no terms are known that restrict this use. The code in `lzhuf.cpp`
was written for this program (the position-code tables are generated, not copied) and is distributed under the GPL
with the rest; the credit above is given because its structure follows LZHUF.C.

### Acorn Disk Wrangler
Parts of this program are adapted from **Acorn Disk Wrangler** (<https://github.com/drdpj/AcornDiskWrangler>),
by the same author and under the same licence: the tree-layout helpers in `mainwindow_p.h`, the drag-out code in
`dragout.cpp`, the viewer, the build files, and `docs/retro-files-format.md` (the shared interchange
specification). `tests/interop_adw` is built from Acorn Disk Wrangler's own `dragout.cpp` at test time; that code
is not copied into this repository.

## Consulted, not copied
These were read for facts about formats. No code or text was copied from them.

| Source | Used for | Terms |
|---|---|---|
| MAME `src/lib/formats/td0_dsk.cpp` (Miodrag Milanovic) | checking the TeleDisk record encodings | BSD-3-Clause |
| MAME `src/lib/formats/imd_dsk.cpp` | checking the ImageDisk layout | BSD-3-Clause |
| cpmtools' `cpm(5)` manual page | directory entries, CP/M 3 labels and time stamps, byte counts | GPL v3+ (documentation) |
| Acorn Application Note 007 (Acorn Computers, via chrisacorns.computinghistory.org.uk) | the Acorn Z80 second processor disc format | facts only |
| Adam Sampson, "Archiving CP/M floppies" notes (offog.org) | Research Machines 480Z track 0 and side layout | facts only |
| Notes on the Torch CP/N directory, entries, address word and allocation map (a source comment the owner supplied) | the Torch CP/N filesystem (`torchfs.cpp`, `tests/torch_check.py`) | facts only |
| Myrcurial/o1next research notes (GitHub) | confirming that IMD tracks must be placed by sector ID | facts only |
| uBee's patched cpmtools 2.10j diskdefs (snakebyte69/CPMTools-Microbee-Patch) | the Microbee ds80 and ds84 geometry and the `datasect` idea (see the cpmtools section above for the credit) | GPL (facts only; nothing copied) |

## Used only for testing (not included, not distributed)
* **cpmtools** (`cpmls`, `cpmcp`, `fsck.cpm`; GPL v3+) and **libdsk** (`dsktrans`, `dskid`; Copyright John Elliott,
  LGPL v2+) are run by the test scripts as independent checks.
* **Real disc images** fetched by `tests/fetch_real_images.sh` from public GitHub repositories (Myrcurial/o1next,
  udo-munk/z80pack, lesbird/H89, FujiNetWIFI/fujinet-go-h89-desktop) and the owner's own Research Machines images.
  They belong to their owners and are not part of this repository; the tests read them where they are.

## Linked at run time / bundled in binary releases
* **Qt 6** (or Qt 5.15 in a Qt 5 build; The Qt Company and contributors), used under the **GNU LGPL v3** (also
  available under GPL v2/v3).
  The program links to Qt's shared libraries; it does not modify Qt.
* A binary release also ships the libraries Qt needs on that platform. Which ones depends on the platform and
  the packages it was built from, so they are not listed here: `ci/collect-licences.sh` traces each one to its
  package (MSYS2 on Windows, Ubuntu for the Linux AppImage, Homebrew for a Qt 5 Mac build) and gathers that
  package's licence
  texts. A release build compiles them into the program - **Help > Licences**, with an index of which library
  came from which package and its stated licence - and also ships them as files in `licences/bundled`. On
  Windows they include the GCC runtime (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`; GPL v3 with the GCC Runtime
  Library Exception) and `libwinpthread-1.dll` (MIT and BSD-style); on every platform, things like ICU (Unicode
  licence), HarfBuzz (MIT), FreeType (FreeType Licence), zlib, libpng, PCRE2 and zstd (BSD-style).
  The Mac build uses Qt's official binaries, which contain their own copies of such libraries; their licences
  come from Qt's source at the same version, where Qt records each one (`ci/qt-attributions.py`).
  Under the LGPL, Qt's own licence text accompanies it (compiled in, and as `licences/LGPL-3.0.txt`), and
  recipients must be able to replace the Qt libraries with their own builds (they can: every build links to
  Qt's ordinary shared libraries) and to obtain this program's source (each GitHub release carries it).
