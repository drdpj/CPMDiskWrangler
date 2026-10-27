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
// diskimage.h - the physical disc: tracks of sectors, read from and written to the container formats
// (raw sector dump, ImageDisk .IMD, CPCEMU .DSK / extended .DSK, TeleDisk .TD0 read-only).
//
// Sectors are addressed by (cylinder, head, index), where index is the sector's position on its track in
// ascending order of sector ID - never its position in the file. (ImageDisk and TeleDisk store a track in the
// order the sectors passed the head, which is often rotated or interleaved; reading in file order puts sectors
// in the wrong place and still gives a plausible-looking but wrong directory.)
//
// Nothing is written to the file except by commit(): writes are staged in memory, every read overlays them.
#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVector>

namespace Disk {

enum class Kind { None, Raw, Imd, Dsk, Edsk, Td0 };
QString kindName(Kind k);           // "raw", "ImageDisk", ...
QString kindExtension(Kind k);      // "img", "imd", "dsk", "dsk", "td0"

struct Sector {
    quint8 c = 0, h = 0, r = 0, n = 0;   // the ID field as recorded
    int size = 0;                        // data length in bytes
    QByteArray data;                     // empty if the sector had no data ("unavailable")
    bool deleted = false;                // deleted-data address mark
    bool crcError = false;               // data read with an error
    bool missing = false;                // no data recorded
    // container-specific, to write the sector back the way it was
    quint8 imdType = 1;                  // IMD record type 0..8 as read
    quint8 st1 = 0, st2 = 0;             // DSK FDC status bytes
    int fileOffset = -1;                 // DSK/EDSK/raw: where the data lives in the original file
};

struct Track {
    int cyl = 0, head = 0;               // where the track is (from the container, not the sector IDs)
    int mode = -1;                       // ImageDisk mode 0-5 (data rate and FM/MFM), -1 if unknown
    QVector<Sector> secs;                // in recorded (rotational) order
    QVector<int> byId;                   // logical index -> position in secs, ascending sector ID
    bool duplicateIds = false;           // two sectors with the same ID: copy protection, never written
    QByteArray imdRaw;                   // IMD: the track's bytes exactly as in the file (re-emitted if unchanged)
    int imdHeadFlags = 0;                // IMD: bits 7/6 of the head byte (cylinder / head maps present)
    // DSK track header bytes kept for writing back
    quint8 dskGap = 0x4E, dskFill = 0xE5;
};

// How a raw image's bytes are cut into tracks: always cylinder by cylinder (cyl 0 head 0, cyl 0 head 1, ...).
// A raw image written in "logical" (cpmtools) order is read with the format's side order set to "alt".
struct RawLayout {
    int seclen = 0, sectrk = 0, cyls = 0, heads = 1;
    int firstId = 1;                     // sector IDs given to the sectors (only matters for Save As)
    int mode = -1;                       // IMD mode for Save As
    bool valid() const { return seclen > 0 && sectrk > 0 && cyls > 0 && heads > 0; }
    qint64 bytes() const { return qint64(seclen) * sectrk * cyls * heads; }
};

// A summary of the physical shape, for detection and the Disc info box.
struct Shape {
    int cyls = 0, heads = 0;             // heads that carry tracks of the common shape
    int allHeads = 0;                    // heads with any track at all
    int seclen = 0, sectrk = 0;          // the most common track shape (by bytes)
    int firstId = 0;                     // lowest sector ID on the common tracks
    int uniformTracks = 0;               // tracks with the common shape
    int oddTracks = 0;                   // tracks with another shape (e.g. an FM boot track)
    int badSectors = 0;                  // CRC errors, missing data, duplicate IDs
    QVector<int> idsOfFirstTrack;        // sector IDs of cyl 0 head 0, sorted
};

class Image {
public:
    Image() = default;

    // Opens any supported container. A raw image has no tracks until setRawLayout() is called (detection does
    // that); its bytes are available through rawBytes(). err is set on failure.
    bool open(const QString &path, QString *err);
    // An image made in memory (a new blank disc), saved with saveAs().
    void createBlank(Kind kind, const RawLayout &l, const QString &comment);
    void close();

    Kind kind() const { return m_kind; }
    QString path() const { return m_path; }
    bool isOpen() const { return m_kind != Kind::None; }
    bool readOnlyContainer() const { return m_kind == Kind::Td0; }      // TeleDisk: Save As only
    QString comment() const { return m_comment; }                     // IMD / TD0 comment, DSK creator
    QString headerLine() const { return m_imdHeader; }

    // ----- raw images
    const QByteArray &rawBytes() const { return m_raw; }
    qint64 rawFileSize() const { return m_rawFileSize; }
    bool setRawLayout(const RawLayout &l, QString *err);     // pads a short image in memory (filled with E5)
    RawLayout rawLayout() const { return m_layout; }
    bool needsPadding() const { return m_kind == Kind::Raw && m_layout.valid() && m_rawFileSize < m_layout.bytes(); }
    // A raw image longer than its layout keeps its tail untouched (it is never truncated by a save).

    // ----- the physical disc
    int cylinders() const { return m_cyls; }
    int heads() const { return m_heads; }
    const Track *track(int cyl, int head) const;
    int sectorCount(int cyl, int head) const;
    const Sector *sector(int cyl, int head, int idx) const;      // idx in ascending-ID order
    bool sectorUsable(int cyl, int head, int idx) const;        // has good data and can be written
    Shape shape() const;

    // Reads overlay staged writes. A sector that is missing reads as nothing (returns false).
    bool read(int cyl, int head, int idx, QByteArray *out) const;
    // Staged; data is padded/truncated to the sector size. Fails on a missing/bad/duplicate sector.
    bool write(int cyl, int head, int idx, const QByteArray &data, QString *err);

    bool hasPending() const { return !m_pending.isEmpty() || m_forceSave; }
    void discardPending() { m_pending.clear(); m_forceSave = false; }
    QMap<quint32, QByteArray> snapshot() const { return m_pending; }
    void restore(const QMap<quint32, QByteArray> &s) { m_pending = s; }
    void markForSave() { m_forceSave = true; }      // e.g. a truncated raw image that must be padded on disc

    // Builds the file contents with the staged writes applied, in this image's own container (or another, for
    // Save As). Unchanged tracks of an IMD file and every byte outside changed sectors of a DSK/raw file are
    // reproduced exactly.
    bool serialize(Kind as, QByteArray *out, QString *err) const;
    // Writes the file atomically (temporary file + rename); keeps a .bak copy first when backup is set.
    // On success the staged writes become the image's contents and the path becomes the target.
    bool commit(QString *err, bool backup);
    bool saveAs(const QString &path, Kind as, QString *err);

private:
    static quint32 key(int c, int h, int i) { return quint32(c) << 16 | quint32(h & 0xFF) << 8 | quint32(i & 0xFF); }
    bool parseImd(const QByteArray &b, QString *err);
    bool parseDsk(const QByteArray &b, QString *err);
    bool parseTd0(const QByteArray &b, QString *err);
    void index();                                        // fills m_cyls/m_heads, byId, the track lookup
    void applyPending();                                 // after a successful commit
    bool writeImd(QByteArray *out, QString *err) const;
    bool writeDsk(QByteArray *out, bool extended, QString *err) const;
    bool writeRaw(QByteArray *out, QString *err) const;
    QByteArray sectorData(int ti, int si) const;         // data with pending writes applied

    Kind m_kind = Kind::None;
    QString m_path;
    QString m_comment, m_imdHeader;
    QByteArray m_file;                                   // the file as opened (DSK/EDSK: patched on save)
    QByteArray m_raw;                                    // raw images: the bytes, padded to the layout
    qint64 m_rawFileSize = 0;
    RawLayout m_layout;
    QVector<Track> m_tracks;
    QMap<quint32, int> m_trackIndex;                     // (cyl,head) -> m_tracks index
    int m_cyls = 0, m_heads = 0;
    QMap<quint32, QByteArray> m_pending;                 // key(c,h,idx) -> new data
    bool m_forceSave = false;
    int m_td0Rate = -1;                                  // TeleDisk data rate / FM flag, for converting to IMD
    bool m_td0Fm = false;
};

// TeleDisk "advanced compression" (LZSS + adaptive Huffman, as in Okumura's LZHUF). Exposed for tests.
QByteArray lzhufDecode(const QByteArray &in, int maxOut);

} // namespace Disk
