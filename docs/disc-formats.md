# Disc formats - CP/M Disk Wrangler

The facts the code relies on for each format and container, how sure we are of each, and the real discs they were
checked on. Read this before changing `src/diskimage`, `src/cpmformat`, `src/cpmfs`, `src/torchfs` or the diskdefs in
`src/resources/`. The design rules are in [AGENTS.md](../AGENTS.md), how a format is chosen in
[detection.md](detection.md), and the tests named here in [testing.md](testing.md).

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
  - Microbee: `microbee-ds80` data tracks (after the reserved ones) are numbered from 21 (`datasect 21`), the reserved ones from 1. `makeBlank` reshapes the data tracks to `dataId` (DSK/IMD only; a raw file has no IDs) - without it a new DS80 disc was "Bad Sector" in ubee512. `microbee-ss80` is `microbee40`'s layout on 80 cylinders, one side (IDs 1-10 everywhere); `microbee-ds40` is `microbee40`'s layout on 40 cylinders x 2 sides (`heads 2`; cpmtools' `microbee40` has no sides key, so on its own it is read as one side of 80 cylinders, which is what a DS40 made from it used to be. No Microbee disc is 40 track on one side of 80 cylinders, so `microbee40` is withdrawn from the library by a `withdraw` line in the built-in diskdefs) and, as a raw file, the same bytes as SS80, so a raw file is detected as either. Checked on `dtoolkit_disk_3_ds40.dsk` (35 files, detected as `microbee-ds40` with high confidence).
  - DSK containers: `Kind::Edsk` ("EXTENDED CPC DSK File", per-track size table) and `Kind::Dsk` (the original "MV - CPCEMU Disk-File", one track size, which the Microbee repository's discs use and some older emulators read (ubee512's own DSK code doesn't check the header at all: it reads both the same, as long as every track is the same size)). New discs and Save As offer both; `writeStandardDsk` refuses a disc whose tracks differ in size (a boot track of its own shape, mixed sector sizes) and says to use an extended DSK. Opening and saving a file keeps its kind (the data is patched in place).
  - Sharp (only *some* Sharp formats are inverted; the MZ-800 P-CP/M disc below is ordinary): `sharp-sds400` (40 cylinders x 2 x 10 x 512, 2K blocks, 128 entries): the sectors are stored inverted (`complement yes`, libdsk's "complement"; empty space reads 1A, not E5) and a cylinder is read side 1 first (`sides altrev`; IDs 1-10 on side 1, 11-20 on side 0), cylinder 0 (both sides, one track 16 x 256) reserved. The inversion is applied in `readLogical` (detect.cpp) and `Volume::readBytes/writeBytes`; detection tries inverted readings when the image is mostly 1A with hardly any E5 (`looksInverted`). New discs of it can't be made yet (per-side sector IDs). A raw file of one (408576 bytes: the odd 16 x 256 track makes it not a uniform layout) isn't read. The MZ-800 P-CP/M disc (`PCPM.SYS`, 40 cylinders x 2 x 8 x 512, boot track 16 x 256 on cylinder 0 side 1) is outback: `oddTrackPenalty` marks down readings that put a track of another shape in the middle of the data (outout), so outback ranks first. S2 bit 7 (BDOS's "written to" flag) is tolerated in directory entries.
  - `vector-26x256`: Vector Graphic CP/M 2.2 56K (`CPM22-56K-DSDD.DSK`, 376064 bytes): track 0 FM 26 x 128, then 56 tracks of
    26 x 256, skew 5 (sectors 5 apart: the directory is logical sectors 0-5 of the third track), 4K blocks, 48 directory
    entries (the rest of the first block holds unrelated system data, which is why 128 would read junk), 2 boot tracks.
    cpmtools reads it as `seclen 256 tracks 56 sectrk 26 blocksize 4096 maxdir 48 skew 5 boottrk 1 offset 3328` (fsck clean).
    The generic search finds it too, read-only, with 128 entries: `detect()` also tries raw layouts of one track 0 of
    26 x 128 or 16 x 128 FM and the rest in 256/512/1024-byte sectors (one side).
  - `osborne-8dd`: worked out by detection from 7 real Osborne 8-inch discs (track 0 FM 26 x 128, then MFM
    8 x 1024, skew 3, 2 boot tracks, 2K blocks); directory size 128 is the whole-block reading (96 would read
    the same discs). New discs made here have the same track structure as the real ones (checked with
    `tests/decode_container.py` and the IMD dumper).
  - `rm-dd`, `rm-qd`: cpmtools' layouts plus track 0 FM 16 x 128 from Adam Sampson's RM archiving notes
    (offog.org); **not checked against a real RM disc**.
  - On double-sided discs `makeBlank` gives only cylinder 0 head 0 the track 0 shape. Raw layouts
    (`RawLayout::t0sectrk/t0seclen`) give cylinder 0 of **each** side the track 0 shape, as on the RM discs.
- **Microbee DS80** (`microbee-ds80`, `microbee-ds84`; parameters from uBee's patched cpmtools, credited in
  `THIRD-PARTY-NOTICES.md` and the diskdefs comments): 80 cylinders x 2 sides, 10 x 512, skew table `1,4,7,0,3,6,9,2,5,8`.
  DS80 numbers the sectors of its reserved tracks (cylinders 0-1) 1-10 and those of every data track 21-30; the
  diskdef key `datasect 21` (`Format::dataId`, same name as uBee's) makes `makeBlank` do that for new IMD / DSK
  discs (a raw image records no IDs). Reading needs nothing: sectors are addressed by ID order. Checked on
  `games_demo_ds80.dsk` (DS80) and `peter_broughton_v2.2p_2of2_ds84.dsk` (DS84).
- **Tatung Einstein** (`einstein`): 40 tracks x 10 x 512, one side, sector IDs 0-9, 2 reserved tracks, 2K blocks, 64
  entries, no skew. Its DPB says DSM 257 although the disc holds about 95 blocks, so the directory has **two-byte block
  pointers** (8 an entry); the diskdef key `ptr16 yes` (`Format::ptr16Forced`) says so, and only the pointer width
  changes (the real block count still limits the disc). cpmtools has no such key (Damien Guard's workaround is to fake
  `tracks 103`), so `fstest` / `verify_independent.sh` leave `einstein` out. Before the definition existed, `amp1`
  (same geometry, one-byte pointers) matched and read files as blocks 1,0,2,0,... - high confidence, wrong data.
  Now such a reading is flagged (`DirScore::widePtr`: two or more small block numbers in a row then zeros, as many
  as the records need; a real one-byte directory has a nonzero byte beside each pointer), is never "clean", and says
  so in its explanation. Tools > Disc format has a "Two-byte block pointers" box for trying it by hand (`ptr16`);
  the summary line says "two-byte block pointers" for a forced reading. No real image trips the signal.
  Checked on `JSWSHOW_SHOWJUMPER.dsk` and `JSWSHOW_JETSETWILLY.dsk` (the latter has a CRC-error sector on the last
  track, so it opens read-only, and a file whose name is eleven spaces: `blankNamedFile` in `detect.cpp` accepts one
  with blocks instead of calling it nonsense).
- **National character sets**: names on Danish / Norwegian / Swedish discs use `[ \ ] { | }` for ÆØÅ æøå (German: ÄÖÜ äöü),
  so detection's name check allows `[`, `]` and `|` (`<>,;:=?*` are still refused). The RC Piccolo COMAL discs have
  nine such names (`brøkregning`, `årsløn_1`, ...) that used to count as nonsense entries. Names are shown and
  extracted as they are on the disc (`br|kregning`); `|` is not a legal Windows file name, so extracting to Windows has
  not been checked.
- **Names with no base name**: `.HEX` / `.PRN` (the assembler leaves them, empty, when run without a file name) are
  files, not nonsense: `blankNamedFile` in `detect.cpp` accepts a blank base name with a valid extension, with or
  without blocks, and a name of eleven spaces only when it holds blocks (the Einstein Jet Set Willy disc).
- **A separate disc on each side** (`separatesides yes`; `Format::fixedHead` picks the side; cpmtools ignores the
  key): `tracks` etc. describe one side. Detection tries every single-sided library format this way on a
  two-sided disc (scoring side 0, plus side 1 if it reads sensibly, plus each side's track 0 shape); the window
  then attaches one `Cpm::Volume` per side (`m_vols[2]`), and every row, `FileRef`, drag payload and import
  carries the side. Drags between the two sides of one window are allowed; drags back onto the same side are not.
  Not yet: generic search for separate sides (only library formats).
  **New discs of this kind**: a single-sided definition with `twosided yes` (`rm-dd`, `rm-qd`) gets a "Double-sided: a
  separate disc on each side" box in File > New disc image; ticking it gives `separateSides` + 2 heads, and `makeBlank`
  formats both sides (each side its own directory, stamps and label if asked) and, as IMD/DSK, gives cylinder 0 of
  **each** side the track 0 shape (FM 16 x 128). As a raw image the file is side 0 then side 1 (`RawLayout::sideBySide`)
  with a track 0 on each, 732,160 bytes for `rm-qd` - the size of the real discs (`BASIC-disk1.img`, `basiczasm.img`),
  detected back as `rm-qd`, a separate disc on each side. A single-sided format with its own track 0 is still refused as
  raw. The boot track is made blank (E5), not with RM's boot code. Tests: `fstest rm-qd+ds raw|imd|edsk` (both sides
  written, committed, reopened, detected), `tests/verify_rm_sides.py` (cpmtools reads each side of the raw file: first
  track padded to 9 x 512, `fsck.cpm` clean, every file exact; also run on the real images), and guitest.
- **Research Machines 480Z** (checked on three of the owner's real discs: `BASIC-disk1.img`, `BASIC-disk2.img`,
  `basiczasm.img`, raw, 732,160 bytes): side 0 then side 1 in the file; each side = FM track 0 of 16 x 128 (RM
  boot code, "Quad density disc"), then 79 tracks of 9 x 512; each side is an `rm-qd` disc (skew 5, 3 boot
  tracks, 2K blocks, 128 entries). All 20 files match cpmtools (run on each side cut out of the file); an edit of
  side 1 left side 0 and both track 0s byte-identical and passes `fsck.cpm`. The images are the owner's, not in
  the repo: `guitest --rm <image>` and `run_all.sh` (from `<real>/rm/*.img`) use them when given.
- **Torch CP/N** (`torch-cpn`, key `torchcpn yes`; Torch Z80 second processor; checked on five of the owner's real
  discs, `SIG0.dsd`, `AdventureB01.dsd`, `TurboPascal30A.dsd`, `WordStar.dsd`, `StandardUtilities2.dsd`: raw, interleaved, 409,600 bytes, not in the repo).
  **Not a CP/M directory**, so no CP/M reading or block size can describe it: the program's CP/M search finds
  nothing and rightly called these discs "not CP/M" before. 80 cylinders x 2 sides x 10 x 256 (FM), file cylinder
  by cylinder. A sector address word is cylinder << 5 | side << 4 | sector, so logical sector = (word >> 4) * 10 +
  (word & 15), which is exactly the `alt` order the format uses. From the notes the owner supplied (a source comment
  on the directory and the address word) plus what the discs showed (each fact held on all five unless noted):
  - directory: track 0 and track 1 sectors 0-5, 256 entries of 16 bytes; **never-used entries are all zero** (not E5); deleted ones are below.
    Entry: word 0 = the first index sector (bit 15 = L2, else L3), word 1 = highest record number, byte 4 user,
    bytes 5-12 name, 13-15 extension, bit 7: 5-8 F1'-F4', 13 R/O, 14 SYS, 15 archived (the same positions as CP/M's
    attribute bits in the 8.3 name). Entries with index address **FFFF** are **deleted files**: CP/N keeps the name, user and record
    count and frees the sectors in the map. Evidence: `WordStar.dsd`'s six (`CREF80.COM`, `L80.COM`, `LIB.COM`,
    `LIB80.COM`, `M80.COM`, `TEN.MAC`, user 20, the last six slots) each have their old L3 index sector intact in
    free space, in the same order, with exactly the entry's record count, and the data still reads as those
    programs; the map marks none of it. `SHOW.C` on `SIG0.dsd` is another. Torch MCP 1.22 writes FFFF into a word
    in its map-handling code (around $9649), not traced further. So, as with CP/M's E5: deleted entries are not
    listed, a new file takes the first all-zero or deleted slot, and Delete here writes FFFF over word 0 and keeps
    the rest, as CP/N does (`fstest` checks both; `run_all.sh` checks the slot on the real discs). An undelete
    would be possible (find a free L3 whose sectors are free and match the record count) but isn't written. An
    empty file gets one record of ^Z, since an entry can't be without data.
  - **index**: an L3 sector lists up to 128 data sectors; files of up to 128 sectors use one L3 (TURBO.COM, 121),
    larger ones an L2 listing L3s (ADV.COM, 141). Lists end at the first zero word. Data pointers carry **4000**
    (first record in use) and **8000** (second): every sector C000 except the last of a file with an odd number of
    records (4000); L2 pointers have no flags.
  - **allocation map** (track 1 sectors 6-7): word t = cylinder * 2 + side, bit s = sector s in use; bits 10-15 always
    set; words 160-255 FC00 (three discs) or FFFF (two; new discs get FC00); tracks 0 and 1 all marked. On every disc it marks exactly the sectors the files
    reach. Track 1 sectors 8-9 are the same bytes on every disc (a D6 D7 ... ramp, then zeros) but sector 9's byte 254
    (85 on three discs, 80 and 47 on the others; detection ignores it, new discs get 85):
    written by the formatter, so new discs get them and detection takes them as evidence (+6; the map's fixed bits
    are required at all, +10). Free sectors are E5.
  - sizes are whole records (no byte count): a file added comes back padded with ^Z to its last record, as on CP/M
    2.2 (`fstest` expects that for Torch). User numbers 0-31 (`WordStar.dsd` uses 0, 10 and 20). No labels, no time stamps.
  - writing: index sectors first, then data, lowest free sector first (a sector is free when no file reaches it and
    the map doesn't mark it). Real CP/N's own order is not known and doesn't matter to a reader. Adding a 70,000-byte
    file to a copy of each real disc writes only sectors that were free, the directory and the map, and
    `tests/torch_check.py` (an independent reader from the notes alone) reads every file, old and new, and finds
    the map exact. **Not yet checked by Torch's own CP/N** reading a disc this program wrote.
- **TeleDisk**: 12-byte header (`TD` normal, `td` advanced), optional comment block, tracks (count, cylinder,
  head, crc), sectors (C H R N flags crc; data block with length and encoding 0 raw / 1 repeated pair /
  2 RLE). Checked against MAME's reader and on real Kaypro and Zorba images. 1.x advanced compression (LZW) is
  refused.
- **DSK / EDSK**: standard (fixed track size) and extended (track size table); sector data in recorded order;
  weak or short sectors (stored length != nominal) are read but never written.

## Behaviour that follows from the formats
* Exact file sizes are kept where the disc can record them: the last-record byte count used by CP/M 3 and
  cpmtools.
* A short raw image is padded in memory and saved full size.
