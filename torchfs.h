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
// torchfs.h - Torch CP/N's filesystem (format key "torchcpn"): CP/M files, kept another way.
//
// A Torch disc is 80 cylinders x 2 sides x 10 x 256 bytes. Logical sector n = (cylinder * 2 + side) * 10 + sector,
// so a disc address word, cylinder << 5 | side << 4 | sector, gives n = (word >> 4) * 10 + (word & 15).
//   sectors 0-15   directory: 256 entries of 16 bytes (all zero = never used; address FFFF = deleted)
//   sectors 16-17  allocation map: 256 words, word t (t = cylinder * 2 + side) bit s = sector s in use; bits 10-15
//                  always set; words 160-255 FC00 (FFFF on some discs)
//   sectors 18-19  the same bytes on every disc seen but one (written when the disc is formatted)
// Directory entry: word 0 = the file's first index sector (bit 15: an L2 sector, else L3; FFFF: a deleted file,
// whose name, user and record count stay and whose sectors are freed in the map; the slot can be reused), word 1 =
// highest record number (records - 1), byte 4 user (0-31), bytes 5-15 the 8+3 name with attributes in bit 7 (5-8 F1'-F4',
// 13 read-only, 14 system, 15 archived). An L3 sector lists up to 128 data sectors, each word flagged 4000 (first
// 128-byte record in use) and 8000 (second); an L2 sector lists up to 128 L3 sectors (no flags). Files of up to
// 128 sectors have one L3, larger ones an L2. See AGENTS.md for how each fact was checked.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace Torch {

constexpr int Tracks = 160, SecPerTrack = 10, SecLen = 256, Sectors = Tracks * SecPerTrack;
constexpr int DirSectors = 16, MapSector = 16, FormatterSector = 18, Reserved = 20;   // logical sectors 0-19
constexpr int Entries = 256, EntrySize = 16, PtrsPerSector = 128, MaxUser = 31;
constexpr quint16 L2Flag = 0x8000, NoData = 0xFFFF, AddrMask = 0x3FFF;
constexpr quint16 FirstRecord = 0x4000, SecondRecord = 0x8000, MapFixed = 0xFC00;

// Map words beyond the disc (160-255): FC00 on some discs, FFFF on others
inline bool beyondOk(quint16 v) { return v == MapFixed || v == 0xFFFF; }
inline bool validAddr(quint16 w) { w &= AddrMask; return (w >> 4) < Tracks && (w & 15) < SecPerTrack; }
inline int lsecOf(quint16 w) { w &= AddrMask; return (w >> 4) * SecPerTrack + (w & 15); }
inline quint16 addrOf(int lsec) { return quint16((lsec / SecPerTrack) << 4 | (lsec % SecPerTrack)); }
inline quint16 word(const QByteArray &b, int i) { return quint16(quint8(b[2 * i]) | quint8(b[2 * i + 1]) << 8); }
inline void setWord(QByteArray *b, int i, quint16 v) { (*b)[2 * i] = char(v & 0xFF); (*b)[2 * i + 1] = char(v >> 8); }

QByteArray formatterSectors();      // sectors 18-19 (512 bytes) as Torch discs have them (sector 19 byte 254 varies: 85 here)
QByteArray emptyMap();              // the map (512 bytes) of a disc with no files

// Reads logical sector n (256 bytes); false if it can't.
typedef std::function<bool(int, QByteArray *)> SectorReader;

// A file's sectors, found by following its index from the directory entry.
struct Walk {
    QVector<int> index, data;       // logical sectors: the L2/L3 sectors, then the data in file order
    QVector<quint16> flags;         // each data sector's record flags (FirstRecord | SecondRecord)
    QStringList errors;             // pointers off the disc or into the first two tracks, unreadable sectors
    QStringList warnings;           // odd but harmless (a record flag not as the record count says)
};
Walk walk(const SectorReader &read, quint16 first, qint64 records);

// An entry's name and user are what a file would have (printable name, user 0-31).
bool entryIsFile(const QByteArray &e);
bool entryIsDeleted(const QByteArray &e);      // index address FFFF
bool entryIsFree(const QByteArray &e);         // all zero (never used) or deleted: a new file may take it
qint64 entryRecords(const QByteArray &e);      // 0 for a deleted entry

// What the directory makes of a disc, for detection: no Volume needed.
struct Scan {
    bool readable = false;
    bool mapOk = false;             // the map has the bits every Torch map has (fixed bits; tracks 0-1 in use)
    bool signature = false;         // sectors 18-19 hold the formatter's bytes
    int files = 0, bad = 0, badPtr = 0, cross = 0, exact = 0;
    QStringList names;              // "USER:NAME.EXT", in directory order
};
Scan scan(const SectorReader &read);

} // namespace Torch
