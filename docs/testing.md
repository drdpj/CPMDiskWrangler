# Testing - CP/M Disk Wrangler

    tests/run_all.sh <build dir> [<real image dir from tests/fetch_real_images.sh>] [<Acorn Disk Wrangler source>]

builds every test and runs them all; it exits 0 only if everything passed. The independent checks need
**cpmtools** and **libdsk-utils** (Ubuntu packages) and Python 3. Each test is its own `.pro` in `tests/`, built
against the sources in `src/`. Run GUI tests with `QT_QPA_PLATFORM=offscreen`.

## The tests
| Test | What |
|---|---|
| `detecttest [-v] [-x dir] images...` | prints the ranked readings, the verdict of detection by tiers (open, ask, handoff) and the listing; `-x` extracts every file (compare with cpmtools: `tests/cmp_cpmtools.sh`) |
| `voltest <dir>` | DiskWranglerCore's filesystem interface (`src/cpmvolume.h`): a CP/M and a Torch CP/N disc as `dw::fs::Disc` (user areas as namespace levels, attributes, renaming into another user, a failed transaction put back, the checked save); detection by tiers (CP/M discs open here, DFS and FAT discs go to Acorn Disk Wrangler); Acorn Z80's catalogue from the core's DFS module; and (step 6b) on CP/M 2.2, CP/M 3 (`pcw`) and Torch CP/N, `reload()` giving back exactly what every kind of change leaves in memory, and the transaction alone undoing a replace that doesn't fit and a failure among several changes |
| `fstest <out> <format> raw\|imd\|edsk` | new disc, awkward file sizes, replace/delete/rename/attrs, commit, reopen **by detection**, verify; leaves material for `verify_independent.sh` |
| `fstest roundtrip images...` | unchanged images serialise byte-identically |
| `fstest atomic <out>` | failed adds leave nothing behind |
| `fstest library` | built-in definitions that replace cpmtools ones have the same layout and `dirblks` |
| `fstest addto <image> NAME size` | detect, add a file, save (for checking edits of real images with cpmtools) |
| `tests/verify_independent.sh <fstest outdirs>` | **independent**: Python IMD/DSK decoder + libdsk (where its geometry probe is right) must see the same sectors; cpmtools must read every file exactly and `fsck.cpm` must be clean |
| `guitest <dir> [osborne.imd [teledisk.td0]] [--adw dir]` | offscreen GUI: host drops (userN folders, flattening, .inf skipped), real drag events between windows, drag-out JSON, drops made by Acorn Disk Wrangler's code, read-only rules, format dialog, TeleDisk Save As, the menu and button order, Copy / Paste with the real keys (`QTest`: the `.pro` has `QT += testlib`); screenshots in `<dir>/shots`. `run_all.sh` fails unless it ends "all ok" |
| `xdragtest <dir>` (run as `xvfb-run -a -s "-screen 0 1280x800x24" xdragtest <dir>`) | Linux/X11 only, needs Xvfb, xdotool and libxcb (built with `-lxcb`); series 6c-2. Real drags out of a disc window with the real pointer (xdotool) to targets in other processes: a Qt window that asks for the files as the drag enters; a target written on xcb (`core/tests/xdndtarget.h`) that asks as GTK 4 (Nautilus) and GNOME's XWayland bridge do - TARGETS, a portal format nobody offers, then text/uri-list, taking only an answer for the target asked; and one that never asks. Checks the files each gets, the refusal, and the status line ("Copied 1 file out of the disc." or "No files were copied out of the disc: ..."). Before 6c-2 the xcb target got no answer to TARGETS or the portal format and the window said nothing about the last two drops |
| `screenshot` | `<outdir> <image> [<image> ...]` | saves `<image>.png` (main window, 1300 px wide) and `<image>_narrow.png` (default size) for each image, and for the first image `dialog_<slot>.png` for each dialog the main window opens (newImageDialog, formatDialog, natDialog, setLabelDialog, viewSelected, editProperties, about); exits 1 if one opens none. `run_all.sh` runs it on copies of the ibm-3740, pcw and torch-cpn ImageDisk discs |
| `tests/interop_adw` (qmake `ADW=<Acorn Disk Wrangler source>`) | built from **Acorn Disk Wrangler's own host naming (`hostpolicy.cpp`) and the core's retro-files writer and reader**: `make <dir>` writes its drags (ADFS, DFS, FAT) for guitest; `read <json>` reads ours |
| `tests/torch_check.py <image> <outdir>` | **independent** Torch CP/N reader and checker (from the format notes only): extracts every file, checks every chain, the record flags and that the map marks exactly the sectors in use. `verify_independent.sh` uses it for `fstest`'s Torch discs (cpmtools can't read them); `run_all.sh` compares its files with ours for every `<real>/torch/*.dsd` (the owner's discs, not in the repo) and adds a file to a copy of each |
| `tests/acornz80_check.py <dsd> <outdir>` | **independent** Acorn Z80 reader (from Application Note 007 only); works out for itself whether the file is interleaved or side after side (where the directory is) and prints it first. `run_all.sh` compares its files with ours for every `<real>/acorn/*.dsd` (the owner's discs `cpm1.dsd`-`cpm7.dsd`, not in the repo), checks we read the file in the same order, and for a side-after-side disc adds a file to a copy and reads it back with it. Without real discs, `run_all.sh` rewrites `fstest`'s raw acorn-z80 disc side after side and checks it detects the same, with the same files |
| `tests/make_foreign.py <dir>` | Acorn DFS discs (and copies with the catalogue wiped) that must all detect as not CP/M, the DFS ones handed to Acorn Disk Wrangler (`run_all.sh` checks) |
| `devicetest` (`core/tests/devicetest.pro`) | DiskWranglerCore's device layer (the containers this program used to have as `src/diskimage.*`): raw files read as needed and staged in blocks, the physical view, ImageDisk / DSK Save As, `Linear`, and the checked save (temporary copy, read-back, verify veto, `.bak` as a hard link, permissions). `run_all.sh` runs it |
| `volumetest`, `moduletest` (`core/tests/`) | DiskWranglerCore's filesystem layer and its DFS and FAT modules; built and run by Acorn Disk Wrangler's `acorn_all.sh` and the core's `ctest` |
| `settingstest` (`core/tests/settingstest.pro`) | where settings live on Linux: an installed program has no portable mode; an AppImage keeps `<program>.ini` beside the `.AppImage` file (in DiskWranglerCore, shared with Acorn Disk Wrangler) |
| `mkicon <dir>` | the PNG icons |
| `clitest` (`core/tests/clitest.pro`) | DiskWranglerCore's command-line layer on a small FAT backend: usage errors, the JSON envelope, exit codes, `script`, `--dry-run`, saving (`run_all.sh` runs it) |
| `tests/cli/run_cli.sh [--update] <cpmdw> <work dir> [<real images dir>]` | `cpmdw`'s golden-image cases in `tests/cli/cases/<NN-name>/`: `setup` makes the image, `cmds` is one `$TOOL ...` per line (every command on a disc that is not yet confirmed needs `--format=NAME`), `host/` holds the files to put. Standard output, exit code, a hash of the images and of the files written to the host are compared with `expected/`, run with `SOURCE_DATE_EPOCH` fixed and `TZ=UTC`. Exit 10 runs as `nobody` (root ignores `chmod`). Cases marked `# needs-real` run only when the real images folder is given. `--update` rewrites `expected/`: **review the diff by hand**. Exit 8 (ambiguous) has no case: no disc has been found that two CP/M layouts or CP/M and another filesystem tie on at the same tier; Acorn Disk Wrangler's cases cover the code path |
| `tests/cli/cross_tool.sh <cpmdw> <acorndw> <work dir>` | the two console programs hand files to each other through `get --manifest` and `put --retro`; run by `run_all.sh` when given the Acorn Disk Wrangler source |
| `tests/track0_raw.sh <cpmdw> <work dir> [RM images...]` | a track 0 of its own shape in raw images (core v1.2.0): each such library format (`rm-dd`, `rm-qd`, `osborne-8dd`, `vector-26x256`) made new as raw (and checked), and made as ImageDisk with two files and converted to raw, read back as that format (certain) with the same listing and files; each real Research Machines disc (`<real>/rm/*.img`) raw -> ImageDisk -> raw and raw -> extended DSK -> raw byte for byte. Raw -> ImageDisk (`saveDiscAs`) gives each such format and `microbee-ds80` the tracks a new ImageDisk of it has (mode, sector size, IDs, read by its own decoder), the real discs' track 0 FM on both sides; `put --format` keeps an own-size file's size. `--track0=padded` (core v1.3.0): new and converted padded files are every track full with zeros after track 0's bytes, detected (certain; `rm-qd` likely, as it is the size of a padded two-sided `rm-dd`), list the same files, go to ImageDisk with the same tracks and back to own size byte for byte; the real discs padded are detected certain with both sides' files and convert back byte for byte; `--track0` with ImageDisk is a usage error; a disc of a format not in the library (its own diskdef, track 0 16 x 128 FM) opened without it is read with track 0 16 x 128 FM, goes raw -> ImageDisk (track 0 FM) -> raw byte for byte, and its ImageDisk copy to padded raw |
| `ldd cpmdw` | must show neither QtGui nor QtWidgets (`run_all.sh` checks) |

## Verify independently, not with our own code
Our reader agreeing with our writer proves little. This
caught a real bug (a built-in `kpiv` without `dirblks 2`, so new files were put in a block the Kaypro reserves
for the directory) that every self-test had passed.

## Pitfalls with the independent tools
- **libdsk's geometry probe is unreliable** for many layouts (it guessed a Kaypro disc as 9 sectors from 1); its
  `dsktrans` output is only trusted when `dskid`'s geometry matches. It also fails ("Missing address mark") on
  some discs whose geometry it does get right. Hence the Python decoder.
- **cpmtools can't read the last blocks of some z80pack images** ("Bad parameter"): the files we read there were
  checked by eye (PROFILE.SUB is a SUBMIT script; WM.COM continues as Z80 code with its own messages).
- cpmtools reads `./diskdefs` before `/etc/cpmtools/diskdefs`: the scripts write one into a temporary folder.
- The cpmtools 4MB `z80pack-hd` listing crashes cpmtools on hd-tools.dsk (malloc error); our reading checks clean.

## Real images used for testing (not in the repo)
`tests/fetch_real_images.sh <dir>` clones them: Myrcurial/o1next (about 40 real Osborne IMD/TD0 discs including
8-inch source discs whose files that project extracted independently - ours match byte for byte), udo-munk/z80pack
(8" CP/M 1.3-3, MP/M, 4MB hard discs, UCSD and FLP-80DOS negatives), lesbird/H89 and FujiNetWIFI (Heathkit H8D).
