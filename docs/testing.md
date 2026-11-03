# Testing - CP/M Disk Wrangler

    tests/run_all.sh <build dir> [<real image dir from tests/fetch_real_images.sh>] [<Acorn Disk Wrangler source>]

builds every test and runs them all; it exits 0 only if everything passed. The independent checks need
**cpmtools** and **libdsk-utils** (Ubuntu packages) and Python 3. Each test is its own `.pro` in `tests/`, built
against the sources in `src/`. Run GUI tests with `QT_QPA_PLATFORM=offscreen`.

## The tests
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
| `tests/torch_check.py <image> <outdir>` | **independent** Torch CP/N reader and checker (from the format notes only): extracts every file, checks every chain, the record flags and that the map marks exactly the sectors in use. `verify_independent.sh` uses it for `fstest`'s Torch discs (cpmtools can't read them); `run_all.sh` compares its files with ours for every `<real>/torch/*.dsd` (the owner's discs, not in the repo) and adds a file to a copy of each |
| `tests/acornz80_check.py <dsd> <outdir>` | **independent** Acorn Z80 reader (from Application Note 007 only); `run_all.sh` compares its files with ours for every `<real>/acorn/*.dsd` (the owner's discs, not in the repo) |
| `tests/make_foreign.py <dir>` | Acorn DFS discs (and copies with the catalogue wiped) that must all detect as not CP/M (`run_all.sh` checks) |
| `settingstest` | where settings live on Linux: an installed program has no portable mode; an AppImage keeps `<program>.ini` beside the `.AppImage` file (shared with Acorn Disk Wrangler) |
| `mkicon <dir>` | the PNG icons |

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
