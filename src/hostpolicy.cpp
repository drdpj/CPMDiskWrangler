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
#include "hostpolicy.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <diskwrangler/xfer/hostname.h>

namespace RetroFiles {

QByteArray trimCpmText(const QByteArray &d) {
    if (d.isEmpty() || d.size() % 128) return d;
    int lastRec = d.size() - 128;
    int z = d.indexOf(char(0x1A), lastRec);
    if (z < 0) return d;
    for (int i = z; i < d.size(); ++i) if (d[i] != char(0x1A)) return d;      // not plain padding
    for (int i = 0; i < z; ++i) {                                               // text only (bit 7 allowed: WordStar)
        quint8 c = quint8(d[i]) & 0x7F;
        if (c < 0x20 && c != '\r' && c != '\n' && c != '\t' && c != 0x0C && c != 0x08) return d;
    }
    return d.left(z);
}

// Is the folder's file system case sensitive (Linux usually; Windows and macOS usually not)? Found by trying it.
static bool hostCaseSensitive(const QString &dir) {
    const QString base = QString("cpmdw-case-probe-%1").arg(QCoreApplication::applicationPid());
    QFile f(dir + "/" + base + "a");
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.close();
    const bool distinct = !QFileInfo::exists(dir + "/" + base + "A");
    f.remove();
    return distinct;
}

static bool hasLower(const QString &n) {
    for (QChar c : n) if (c >= 'a' && c <= 'z') return true;
    return false;
}

// The host names, worked out first so that names which keep their case (lower-case ones) get theirs before the
// upper-case ones, which are written in lower case ... unless that would clash: then they keep their case too, and
// only a host that can't tell the two apart (Windows, macOS) adds _2 (the importer takes it off again).
bool CpmPolicy::planNames(const QVector<dw::TransferItem> &items, const QString &hostDir, QVector<QString> *names) const {
    const bool sensitive = hostCaseSensitive(hostDir);
    QMap<int, dw::xfer::UsedNames> used;                     // per folder (user, or -1 for the top)
    names->resize(items.size());
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < items.size(); ++i) {
            const dw::TransferItem &x = items[i];
            const QString src = x.hostName.isEmpty() ? x.name : x.hostName;
            if (hasLower(src) != (pass == 0)) continue;
            const int key = m_folders ? qMax(0, userOf(x)) : -1;
            if (!used.contains(key)) used.insert(key, dw::xfer::UsedNames(sensitive));
            dw::xfer::UsedNames &u = used[key];
            if (pass == 0) { (*names)[i] = u.unique(dw::hostSafeName(src), true); continue; }
            const QString lowered = dw::hostSafeName(src.toLower()), exact = dw::hostSafeName(src);
            if (!u.has(lowered)) { u.add(lowered); (*names)[i] = lowered; }
            else if (!u.has(exact)) { u.add(exact); (*names)[i] = exact; }
            else (*names)[i] = u.unique(exact, true);
        }
    return true;
}

} // namespace RetroFiles
