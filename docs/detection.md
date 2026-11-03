# How detection works - CP/M Disk Wrangler

A CP/M disc doesn't record its own layout, so `src/detect.cpp` tries the possibilities and scores them. This is how,
and why. The "How scoring works" help in `src/formatdialog.cpp` must match these rules: when a score, bonus or
tie-break changes here, change the help too (`guitest` checks it mentions the main sections). The format facts it
relies on are in [disc-formats.md](disc-formats.md).

## The steps
1. Physical layouts: the container's own; for raw images every library format whose size fits (exactly, or
   a truncated image at least 60% of it) and every factorisation into 128-1024-byte sectors, common sectors per
   track, 30-86 cylinders, 1-2 heads.
2. Library candidates that fit physically are evaluated directly (+3 for being known; +4 / -6 if the format
   has a sector-ID base and the disc's IDs match / don't). The PCW spec (+15) and a remembered format (+20) too.
   A Torch CP/N format (`torchcpn`) is scored by `evaluateTorch` instead: its map's fixed bits are required (else
   no reading), then +10 for the map, +6 for the formatter's sectors, +4 a file, +2 a sound index chain, -15 an
   entry that isn't a file, -8 a broken chain or a sector used twice. It is never "empty but used", and another
   filesystem's look-alike structures don't veto it (its own are stronger evidence).
3. The search: for each side order x boot tracks (0-6) x skew (1..sectrk-1; none beyond 32 sectors/track),
   read a window where the directory would be and classify the entries once; then for each block size and
   directory size (whole blocks only) score: +4 per file entry, +2 per entry whose block count matches its
   records, -3 mismatches, -15 nonsense entries, -8 bad/shared pointers or duplicate extents, +6 when the
   lowest file block is the first after the directory.
4. The best 16 search results get the content checks (+3 text files that stay text to their ^Z, -8 that
   don't, +-1 plausible .COM starts, -4 files starting in E5 space), then are named after an identical library
   format if there is one.
5. Readings whose "directory" is mostly nonsense, or score below -5, are dropped. One per layout is kept.
5a. Content checks also rank rivals by blocks landing on nothing: -1 per never-written (all E5) sector inside the first
   8K of a file, -4 per file of only zeros. Not a verdict: real files have a few (Osborne DIAG/OS1 discs), so it only
   separates readings of the same disc. It put the RC Piccolo COMAL discs on side order `alt`, 6 reserved tracks (0
   never-written sectors; out-back and out-out gave 3-12) and moved nothing among the 58 real images. The idea is from
   AutoArchaeologist's `fs_abc.py` (see `THIRD-PARTY-NOTICES.md`).
5b. Confidence: a reading is "clean" (and so can be high confidence and writable) only with no nonsense entries, bad or
   shared pointers, text files that stay text, and no more than a quarter of the file entries whose pointer count
   disagrees with their record count ("block counts match record counts in N of M"). A wrong pointer width or block
   size shows up that way first: a two-byte-pointer directory (the Tatung Einstein's) read with one-byte pointers
   gave 1 of 2, and files with half their blocks missing. All 58 real images we test on have 0 such entries.
5c. The known-format points (+3 known, track 0 shape, DFS catalogue, sector-ID base) are one function,
   `Detect::scoreKnownFormat`: detection uses it for library formats and the format dialog for a format typed by hand
   that equals one (`Format::sameLayout`), so a score means the same whichever way the reading was made. The dialog's
   "How scoring works" button (`showScoringHelp` in `formatdialog.cpp`) lists every weight; change the numbers in
   `detect.cpp` and that text together.
6. Not CP/M at all: an empty directory fits every layout of the right shape, so a disc whose directory area is
   just E5 (an Acorn DFS disc formatted by Acorn's FORM, for one) used to open as an empty CP/M disc. Now (a) an
   empty reading whose directory is one repeated byte is dropped if the rest of the disc (outside that format's
   boot tracks; data before an `offset` counts) has 4 or more non-blank 128-byte records - formatting writes every
   track, so a fresh disc has none; and (b) `foreignFilesystem()` recognises Acorn DFS catalogues, ADFS old map
   (Hugo/Nick at 0x201 and 0x6FB) and D/E, and FAT (BPB, or a media byte + FF FF at 0 or 512, as on DOS 1.x and
   Acorn DOS Plus 800K): with no CP/M files that means "not CP/M" (pointing at Acorn Disk Wrangler); with files,
   the reading is shown but read-only until confirmed. A remembered (confirmed) format is exempt from both.
7. Confidence: `high` if nothing with a different file listing comes within 6 points; `medium` if a known
   format with a different layout lists the same files; `low` for empty directories. `safeToWrite` per rule 6 in [AGENTS.md](../AGENTS.md).

## Raw images: one row for readings that are the same
Readings of a raw image that reach the same bytes in the same order - 32 x 128 on one side, 16 x 256 on two,
8 x 512 - score the same, so `detect()` breaks the tie by `plausibleGeometry` (35/40/77/80 cylinders a side, common
sector layouts; worth under a point) and shows one row, listing the others in its evidence (`readingSignature`).

## Real-disc results
Found correctly on real discs: Osborne SD (osb1sssd, skew 2, including rotated tracks) and DD, Osborne 8"
mixed-density (FM boot track + 8x1024 MFM, skew 3, found by search), Kaypro 4 TeleDisk (kpiv), Zorba CP/M-86
TeleDisk (search), Heathkit H8D (search), z80pack 8" and 4MB hard discs; MS-DOS and UCSD p-System discs are
correctly not read as CP/M (an MS-DOS boot sector gets a hint pointing at Acorn Disk Wrangler).
The tests and the real images are in [testing.md](testing.md).
