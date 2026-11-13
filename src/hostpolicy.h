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
// hostpolicy.h - how CP/M items become host files and host files come back (QtCore only: the command-line program uses
// it too): the Z padding trim and CpmPolicy, the policy Extract to host and drag-out write with; and, for files coming
// in, the host's clash suffixes taken off and the folders that stand for user numbers.
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <diskwrangler/xfer/hostread.h>
#include <diskwrangler/xfer/retrofiles.h>
#include <diskwrangler/xfer/transferitem.h>

namespace RetroFiles {

// The CP/M ^Z (0x1A) padding after the end of a text file: everything from the first ^Z in the last record on,
// if the rest of the record is ^Z too. Returns the data unchanged if it doesn't look like padded text.
QByteArray trimCpmText(const QByteArray &d);

// How CP/M items become host files: optionally a "userN" folder per user number, the ^Z padding optionally trimmed,
// and names worked out together (hostpolicy.cpp).
class CpmPolicy : public dw::xfer::HostPolicy {
public:
    CpmPolicy(bool folderPerUser, bool trimText) : m_folders(folderPerUser), m_trim(trimText) {}
    static int userOf(const dw::TransferItem &it) { return it.attrs.value("cpm")["user"].toInt(); }
    QString topFolder(const dw::TransferItem &it) const override { return m_folders ? QString("user%1").arg(qMax(0, userOf(it))) : QString(); }
    QByteArray content(const dw::TransferItem &it) const override { return m_trim ? trimCpmText(it.data) : it.data; }
    QString errorLabel(const dw::TransferItem &it) const override { return QString("%1:%2").arg(userOf(it)).arg(it.name); }
    bool planNames(const QVector<dw::TransferItem> &items, const QString &hostDir, QVector<QString> *names) const override;
private:
    bool m_folders, m_trim;
};

// ----- files coming in from the host (the window's imports and cpmdw put alike)

// Extracting to a host that can't tell LONSEDDE.L from lonsedde.l adds _2 (or _1...) to one of them. Dropped back, that
// suffix is taken off again - but only when what is left matches another host file name of the same list, so a real
// foo_1.txt stays. One level of a folder at a time (callers apply it to each folder's own list).
void withoutClashSuffixes(QVector<dw::TransferItem> *items);

// "user3", "User 3", "u3" or "3": a host folder that stands for a CP/M user number (as extraction writes them); -1 if not.
int userFromFolderName(const QString &name);

// How both of this program's readers of host files (the window's Add files, drops and pastes, and cpmdw put) use the
// core's one host reader (dw::xfer::readHost, step 5l): a file over 16 MB is too large for a CP/M disc (refused before
// it is read), and Acorn's .inf sidecars aren't CP/M files: one is left out when it was named, or when the file it
// describes is beside it, at every level.
const qint64 MaxHostFile = 16 * 1024 * 1024;
dw::xfer::HostReadOptions cpmHostRead();

} // namespace RetroFiles
