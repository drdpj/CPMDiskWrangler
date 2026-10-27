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
// cpmfs.h - the CP/M filesystem on a Disk::Image laid out by a Cpm::Format: directory, files and their extents,
// CP/M 3 / P2DOS time stamps and disc labels; adding, deleting, renaming files and setting attributes.
//
// Every change goes through the image's staging (Disk::Image::write), so nothing reaches the file until the image
// is committed. Each mutating call is atomic: it takes a snapshot of the staged writes and restores it on failure.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include "cpmformat.h"

#include "diskimage.h"

namespace Cpm {

// File attributes: the high bits of the name and extension characters.
enum Attr : quint16 {
    AttrRO = 0x01,      // T1' read-only
    AttrSYS = 0x02,     // T2' system (hidden from DIR)
    AttrARC = 0x04,     // T3' archived
    AttrF1 = 0x10, AttrF2 = 0x20, AttrF3 = 0x40, AttrF4 = 0x80,   // F1'-F4' (interface attributes)
};
QString attrString(quint16 a);         // e.g. "R/O SYS" or "-"

struct File {
    int user = 0;
    QByteArray name11;                 // the 8+3 name bytes with attribute bits cleared
    QString name;                      // "NAME.EXT" ("NAME" with no extension): bytes as Latin-1 characters
    quint16 attrs = 0;
    QVector<int> entries;              // directory entries, by logical extent
    qint64 records = 0;                // 128-byte records
    qint64 size = 0;                   // bytes (records * 128, less the CP/M 3 last-record byte count)
    bool exactSize = false;            // size came from a CP/M 3 byte count
    int blocks = 0;
    QDateTime created, updated, accessed;   // CP/M 3 / P2DOS stamps (invalid if none)
    QString problem;                   // set if the extents don't fit together
};

struct Label {
    bool present = false;
    int index = -1;
    QString name;
    quint8 mode = 0;                   // bit 7 passwords, 6 access stamps, 5 update stamps, 4 create stamps
    QDateTime created, updated;
};

struct Report {
    QStringList errors, warnings;
    bool ok() const { return errors.isEmpty(); }
};

class Volume {
public:
    Volume() = default;
    // Reads the directory. A disc whose directory fails check() can still be browsed; the caller makes it read-only.
    bool attach(Disk::Image *img, const Format &f, QString *err);
    void detach() { m_img = nullptr; m_files.clear(); m_dir.clear(); }
    bool attached() const { return m_img != nullptr; }
    const Format &format() const { return m_fmt; }
    bool reload(QString *err = nullptr);          // after anything changed the staged image

    const QVector<File> &files() const { return m_files; }
    int indexOf(int user, const QString &name) const;   // into files(), -1 if none (name compared upper-case)
    bool readFile(const File &f, QByteArray *out, QString *err) const;
    QVector<int> usersInUse() const;

    int freeBlocks() const;
    int freeEntries() const;
    qint64 freeBytes() const { return qint64(freeBlocks()) * m_fmt.blocksize; }
    int usedEntries() const;
    bool hasStamps() const { return m_stamps; }        // SFCB (0x21) entries present
    const Label &label() const { return m_label; }
    bool labelAllowed() const;                          // CP/M 3 / ZSDOS, or a label already exists
    QString stampKinds() const;                         // e.g. "create, update"
    const QVector<QByteArray> &directory() const { return m_dir; }   // raw 32-byte entries

    Report check() const;

    // ----- changes (staged, atomic)
    // Adds (or with replace, replaces) a file. when: the date for the time stamps (current time if invalid).
    bool addFile(int user, const QString &name, const QByteArray &data, quint16 attrs, const QDateTime &when,
                 bool replace, QString *err);
    bool deleteFile(int user, const QString &name, QString *err);
    bool rename(int user, const QString &name, int newUser, const QString &newName, QString *err);
    bool setAttrs(int user, const QString &name, quint16 attrs, QString *err);
    bool setLabel(const QString &label, QString *err);
    // An empty directory (E5 everywhere); with stamps, CP/M 3 style SFCB entries in every fourth slot and a
    // label (name may be empty) that turns on create and update stamps.
    bool initDirectory(bool stamps, const QString &labelName, QString *err);

    // ----- names
    // Turns any host name into a CP/M 8.3 name (upper case, invalid characters replaced, truncated).
    static QString toCpmName(const QString &hostName);
    static bool validName(const QString &name, QString *why);
    static QByteArray name11(const QString &name);    // "NAME.EXT" -> 11 space-padded bytes

    // ----- raw access (offsets are bytes from the start of block 0)
    bool readBytes(qint64 off, int len, QByteArray *out, QString *err) const;
    bool writeBytes(qint64 off, const QByteArray &data, QString *err);

private:
    bool parse(QString *err);
    bool writeEntry(int index, const QByteArray &e, QString *err);
    QVector<int> entryBlocks(const QByteArray &e) const;
    int recordsIn(const QByteArray &e) const;
    int logicalExtent(const QByteArray &e) const;
    QVector<bool> allocation() const;
    bool stampSlot(int entry, QByteArray *sfcb, int *slotOffset) const;
    bool writeStamps(int entry, const QDateTime &first, const QDateTime &update, QString *err);
    bool freeSlot(int index) const;
    bool isFileEntry(quint8 st) const;

    Disk::Image *m_img = nullptr;
    Format m_fmt;
    QVector<QByteArray> m_dir;
    QVector<File> m_files;
    Label m_label;
    bool m_stamps = false;
};

// A new, empty disc in the given container: every sector E5 (the format's sector IDs and ImageDisk mode), then an
// empty directory (with CP/M 3 time stamp entries and a label when stamps is set).
bool makeBlank(Disk::Image *img, Disk::Kind kind, const Format &f, bool stamps, const QString &label, QString *err);
int defaultImdMode(const Format &f);

// CP/M 3 dates: days since 31 Dec 1977 (day 1 = 1 Jan 1978), hours and minutes in BCD.
QDateTime decodeStamp(const QByteArray &b, int off);
void encodeStamp(QByteArray *b, int off, const QDateTime &t);

} // namespace Cpm
