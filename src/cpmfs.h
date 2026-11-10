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
// cpmfs.h - the CP/M filesystem on a dw::device::Container laid out by a Cpm::Format: directory, files and their extents,
// CP/M 3 / P2DOS time stamps and disc labels; adding, deleting, renaming files and setting attributes.
//
// Every change goes through the image's staging (dw::device::Container::write), so nothing reaches the file until the image
// is committed. Each mutating call is atomic: it takes a snapshot of the staged writes and restores it on failure.
// The file types and the Store interface are in cpmstore.h; Torch CP/N's filesystem is Torch::Fs (torchfs.h).
#pragma once

#include <memory>

#include "cpmstore.h"
#include "torchfs.h"

namespace Cpm {

// The CP/M directory filesystem. Given a Torch CP/N format (Format::torch) it is also a facade over a Torch::Fs, every
// call forwarded, so code written for one class keeps working: kept for the tests and detection; removed after the
// tests are ported (step 6). New code opens a disc with openStore (below), which gives a Torch::Fs for a Torch disc.
class Volume : public Store {
public:
    Volume() = default;
    // Reads the directory. A disc whose directory fails check() can still be browsed; the caller makes it read-only.
    bool attach(dw::device::Container *img, const Format &f, QString *err);
    void detach() { m_img = nullptr; m_files.clear(); m_dir.clear(); m_torch.reset(); }
    bool attached() const { return m_img != nullptr; }
    const Format &format() const override { return m_fmt; }
    bool reload(QString *err = nullptr) override;             // after anything changed the staged image

    const QVector<File> &files() const override { return m_torch ? m_torch->files() : m_files; }
    bool readFile(const File &f, QByteArray *out, QString *err) const override;

    int freeBlocks() const override;
    int freeEntries() const override;
    qint64 freeBytes() const override { return m_torch ? m_torch->freeBytes() : qint64(freeBlocks()) * m_fmt.blocksize; }
    int usedEntries() const override;
    bool hasStamps() const override { return m_stamps; }        // SFCB (0x21) entries present
    const Label &label() const override { return m_label; }
    void setExtraNameChars(const QString &c) override { m_extraNameChars = c; if (m_torch) m_torch->setExtraNameChars(c); }   // see validName
    bool labelAllowed() const override;                          // CP/M 3 / ZSDOS, or a label already exists
    QString stampKinds() const;                         // e.g. "create, update"
    // raw 32-byte entries (Torch: its 16-byte ones)
    const QVector<QByteArray> &directory() const { return m_torch ? m_torch->directory() : m_dir; }

    Report check() const override;

    // ----- changes (staged, atomic)
    // Adds (or with replace, replaces) a file. when: the date for the time stamps (current time if invalid).
    bool addFile(int user, const QString &name, const QByteArray &data, quint16 attrs, const QDateTime &when,
                 bool replace, QString *err) override;
    bool deleteFile(int user, const QString &name, QString *err) override;
    bool rename(int user, const QString &name, int newUser, const QString &newName, QString *err) override;
    bool setAttrs(int user, const QString &name, quint16 attrs, QString *err) override;
    bool setLabel(const QString &label, QString *err) override;
    // An empty directory (E5 everywhere); with stamps, CP/M 3 style SFCB entries in every fourth slot and a
    // label (name may be empty) that turns on create and update stamps.
    bool initDirectory(bool stamps, const QString &labelName, QString *err);

    // ----- names
    // Turns any host name into a CP/M 8.3 name (upper case, invalid characters replaced, truncated).
    static QString toCpmName(const QString &hostName, const QString &extra = QString());
    // extra: name characters beyond the usual ones that are allowed (a national character set's letters, natchars.h)
    static bool validName(const QString &name, QString *why, const QString &extra = QString());
    static QByteArray name11(const QString &name);    // "NAME.EXT" -> 11 space-padded bytes
    static QString nameFrom11(const QByteArray &n11); // 11 bytes -> "NAME.EXT" ("NAME" with no extension)

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
    bool inertEntry(const QByteArray &e) const;     // a user entry that holds nothing and isn't a file name

    dw::device::Container *m_img = nullptr;
    Format m_fmt;
    QVector<QByteArray> m_dir;
    QVector<File> m_files;
    Label m_label;
    QString m_extraNameChars;          // national letters allowed in names (natchars.h)
    bool m_stamps = false;
    std::unique_ptr<Torch::Fs> m_torch;   // the facade's Torch CP/N filesystem (Format::torch), else none
};

// The filesystem a format says the disc holds, attached to the container: a Torch::Fs for a Torch CP/N format, else
// the CP/M directory (Volume). Null, with *err set, if its directory can't be read.
std::unique_ptr<Store> openStore(dw::device::Container *img, const Format &f, QString *err);

// A new, empty disc in the given container: every sector E5 (the format's sector IDs and ImageDisk mode), then an
// empty directory (with CP/M 3 time stamp entries and a label when stamps is set).
bool makeBlank(dw::device::Container *img, dw::device::Kind kind, const Format &f, bool stamps, const QString &label, QString *err);
int defaultImdMode(const Format &f);

// CP/M 3 dates: days since 31 Dec 1977 (day 1 = 1 Jan 1978), hours and minutes in BCD.
QDateTime decodeStamp(const QByteArray &b, int off);
void encodeStamp(QByteArray *b, int off, const QDateTime &t);

} // namespace Cpm
