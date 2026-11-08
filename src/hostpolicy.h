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
// hostpolicy.h - how CP/M items become host files (QtCore only: the command-line program uses it too): the Z padding
// trim and CpmPolicy, the policy Extract to host and drag-out write with.
#pragma once

#include <QByteArray>
#include <diskwrangler/xfer/retrofiles.h>

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

} // namespace RetroFiles
