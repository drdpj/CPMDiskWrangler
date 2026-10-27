/*
 * CP/M Disk Wrangler - view and edit CP/M disc images (raw, ImageDisk, CPCEMU DSK, TeleDisk)
 * Copyright (C) 2026 Daniel Jameson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
// cpmformat.h - a CP/M disc format: the logical geometry (what cpmtools calls a diskdef) and the disc parameter
// block derived from it, plus the library of known formats read from diskdefs files.
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace Disk { class Image; }

namespace Cpm {

enum class SideOrder { Alt, OutBack, OutOut };
enum class Os { V22, V3, P2dos, Zsys };

QString sideOrderName(SideOrder s);   // "alt", "outback", "outout"
QString osName(Os o);                 // "2.2", "3", "p2dos", "zsys"

struct Format {
    // ----- as written in a diskdefs file
    QString name;                 // e.g. "ibm-3740"
    QString desc;                 // shown in menus; may be empty
    QString origin;               // "built-in", "cpmtools", a file path, or "detected"
    QString source;               // where the definition comes from / how sure we are (built-in file only)
    int seclen = 128;             // bytes per sector
    int sectrk = 26;              // sectors per track
    int tracks = 77;              // logical tracks, all sides together
    int heads = 0;                // physical sides; 0 = infer (2 if tracks > 86, else 1)
    SideOrder sides = SideOrder::Alt;
    int blocksize = 1024;
    int maxdir = 64;
    int dirblks = 0;              // 0 = enough blocks for maxdir entries
    int boottrk = 0;              // reserved tracks before the directory ...
    int bootsec = -1;             // ... or reserved sectors, when >= 0
    int offsetSectors = 0;        // cpmtools "offset": sectors to skip before logical track 0
    int skew = 0;                 // software skew (0/1 = none) ...
    QVector<int> skewtab;         // ... or an explicit table (logical sector -> physical, 0-based)
    Os os = Os::V22;
    int secbase = -1;             // first sector ID for a new disc (-1: 1, or what the image has)
    int imdMode = -1;             // ImageDisk mode for a new disc (-1: from fm/datarate or the geometry)
    bool fm = false;              // single density (cpmtools "fm yes")
    QString datarate;             // cpmtools "datarate" (SD, DD, HD, ED)
    int logicalExtents = 0;       // cpmtools "logicalextents" (rarely needed)
    // "track0 SECTRK SECLEN fm|mfm [FIRSTID]": track 0 has its own shape, e.g. a single-density boot track on a
    // double-density disc. It must lie in the boot area (the filesystem never reads it). 0 = like the others.
    int t0sectrk = 0, t0seclen = 0, t0firstId = -1;
    bool t0fm = false;

    // ----- derived by derive()
    int physHeads = 1;            // heads the format uses
    int cylinders = 0;            // tracks / physHeads (rounded up)
    int dataStart = 0;            // logical sector number (absolute, from the image start) of block 0
    int dsm = 0;                  // blocks - 1
    int drm = 0;                  // directory entries - 1
    int exm = 0;                  // extent mask
    bool ptr16 = false;           // two-byte block pointers
    int ptrsPerEntry = 16;        // 16 or 8
    int dirBlocks = 0;
    int secsPerBlock = 0;
    QVector<int> xlat;            // logical sector -> physical index on the track (skew applied)
    bool derived = false;

    bool derive(QString *err);    // fills the derived fields; false if the parameters are inconsistent
    // Where logical sector lsec (absolute) is on a disc with imageHeads heads: false if out of range.
    bool locate(int lsec, int *cyl, int *head, int *idx) const;
    qint64 capacityBytes() const { return qint64(dsm + 1) * blocksize; }
    QString summary() const;      // one line for the status bar / info box
    QString shortSummary() const; // "10x256, 40 trk, skew 2, boot 3, 2K, 64 dir" for lists
    QString toDiskdef() const;    // the definition in diskdefs syntax (so it can be used with cpmtools)
    QString displayName() const { return desc.isEmpty() ? name : name + " - " + desc; }
    // Same logical layout (everything that decides where data lives and what the directory means).
    bool sameLayout(const Format &o) const;
};

// Parses diskdefs text (cpmtools syntax, case-insensitive keys, '#' comments, plus this program's extras:
// heads, secbase, desc, source, imdmode). Bad definitions are skipped with a message in *warnings.
QVector<Format> parseDiskdefs(const QString &text, const QString &origin, QStringList *warnings);

// The library: built-in definitions first, then cpmtools' diskdefs (bundled), then any files the user added.
// A name defined earlier wins.
const QVector<Format> &library();
void setExtraDiskdefFiles(const QStringList &paths, QStringList *warnings);
QStringList extraDiskdefFiles();
const Format *findFormat(const QString &name);

} // namespace Cpm
