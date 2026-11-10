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
// cpmstore.h - CP/M files, whichever filesystem keeps them: the file, label and report types, and Cpm::Store, the file
// API the adapter (cpmvolume.h) and the main window use. Two filesystems implement it: the CP/M directory
// (Cpm::Volume, cpmfs.h) and Torch CP/N's (Torch::Fs, torchfs.h). Both hold CP/M files: users 0-31, 8.3 names, the same
// attributes; only how the directory and the space are kept differs.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include "cpmformat.h"

#include <diskwrangler/device/container.h>

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

// The CP/M files on one disc (or one side of a disc with a filesystem per side). Every change goes through the
// image's staging and is atomic: a snapshot of the staged writes is taken and put back on failure.
class Store {
public:
    virtual ~Store() = default;
    virtual const Format &format() const = 0;
    virtual bool reload(QString *err = nullptr) = 0;          // after anything changed the staged image

    virtual const QVector<File> &files() const = 0;
    int indexOf(int user, const QString &name) const;         // into files(), -1 if none (name compared upper-case)
    QVector<int> usersInUse() const;
    virtual bool readFile(const File &f, QByteArray *out, QString *err) const = 0;

    virtual int freeBlocks() const = 0;                       // the filesystem's units: blocks (CP/M) or sectors (Torch)
    virtual int freeEntries() const = 0;
    virtual int usedEntries() const = 0;
    virtual qint64 freeBytes() const = 0;
    virtual bool hasStamps() const = 0;                       // CP/M 3 / P2DOS time stamps
    virtual const Label &label() const = 0;
    virtual bool labelAllowed() const = 0;
    virtual bool setLabel(const QString &label, QString *err) = 0;
    virtual Report check() const = 0;

    // Adds (or with replace, replaces) a file. when: the date for the time stamps (current time if invalid; a
    // filesystem without stamps ignores it).
    virtual bool addFile(int user, const QString &name, const QByteArray &data, quint16 attrs, const QDateTime &when,
                         bool replace, QString *err) = 0;
    virtual bool deleteFile(int user, const QString &name, QString *err) = 0;
    virtual bool rename(int user, const QString &name, int newUser, const QString &newName, QString *err) = 0;
    virtual bool setAttrs(int user, const QString &name, quint16 attrs, QString *err) = 0;
    // extra: name characters beyond the usual ones that are allowed (a national character set's letters, natchars.h)
    virtual void setExtraNameChars(const QString &c) = 0;
};

// Raw access to a format's logical bytes (offsets from the start of block 0, after the reserved tracks), through the
// container's staging; an inverted format's bytes are inverted on the way.
bool readLogical(dw::device::Container *img, const Format &f, qint64 off, int len, QByteArray *out, QString *err);
bool writeLogical(dw::device::Container *img, const Format &f, qint64 off, const QByteArray &data, QString *err);

} // namespace Cpm
