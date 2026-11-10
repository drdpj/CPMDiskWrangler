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
#include "dragout.h"

#include "hostpolicy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QMap>
#include <diskwrangler/xfer/hostname.h>
#include <diskwrangler/xfer/retrofiles.h>

namespace RetroFiles {

const char *const MimeType = "application/x-retro-files+json";
const char *const Generator = "CP/M Disk Wrangler";

// ------------------------------------------------------------------------------ XItem <-> dw::TransferItem

dw::TransferItem toTransfer(const XItem &x) {
    dw::TransferItem t;
    t.name = x.name;
    t.hostName = x.hostName;
    t.isDir = x.isDir;
    t.data = x.data;
    t.when = x.when;
    t.sourceFs = x.srcFs;
    QJsonObject a;
    a["user"] = qMax(0, x.user);
    a["readonly"] = x.readOnly;
    a["system"] = x.system;
    a["archive"] = x.archive;
    for (int k = 0; k < 4; ++k) if (x.interfaceAttrs & (1 << k)) a[QString("f%1").arg(k + 1)] = true;
    t.attrs["cpm"] = a;
    for (const XItem &k : x.kids) t.kids.append(toTransfer(k));
    return t;
}

XItem fromTransfer(const dw::TransferItem &t) {
    XItem x;
    x.isDir = t.isDir;
    x.name = t.name;
    x.srcFs = t.sourceFs;
    x.when = t.when;
    x.data = t.data;
    const QJsonObject cpm = t.attrs.value("cpm"), fat = t.attrs.value("fat"), acorn = t.attrs.value("acorn");
    if (!cpm.isEmpty()) {
        x.user = cpm.contains("user") ? cpm["user"].toInt() : -1;
        x.readOnly = cpm["readonly"].toBool();
        x.system = cpm["system"].toBool();
        x.archive = cpm["archive"].toBool();
        for (int k = 0; k < 4; ++k) if (cpm[QString("f%1").arg(k + 1)].toBool()) x.interfaceAttrs |= quint8(1 << k);
    } else if (!fat.isEmpty()) {
        x.readOnly = fat["readonly"].toBool();
        x.system = fat["system"].toBool() || fat["hidden"].toBool();     // CP/M SYS files are hidden from DIR
        x.archive = fat["archive"].toBool();
    } else if (!acorn.isEmpty()) {
        x.readOnly = acorn["access"].toString().contains('L');         // case-sensitive: 'l' is public
    }
    if (t.sourceFs == "dfs" && x.name.size() >= 3 && x.name[1] == '.') x.name = x.name.mid(2);   // "D.NAME": drop D
    for (const dw::TransferItem &k : t.kids) x.kids.append(fromTransfer(k));
    return x;
}

// ------------------------------------------------------------------------------ writing to the host

void writeHost(const QVector<XItem> &items, const QString &hostDir, bool folderPerUser, bool trimText,
               QStringList *topLevel, QJsonArray *jsonItems, QStringList *errors) {
    QVector<dw::TransferItem> t;
    for (const XItem &x : items) t.append(toTransfer(x));
    dw::xfer::writeHost(t, hostDir, CpmPolicy(folderPerUser, trimText), topLevel, jsonItems, errors);
}

QByteArray makeJson(const QJsonArray &items, const QString &hostDir, const QString &imageName) {
    return dw::xfer::makeJson(items, hostDir, imageName, "cpm", Generator);
}

// ------------------------------------------------------------------------------ reading a drop

bool readJson(const QByteArray &json, QVector<XItem> *out, QString *err) {
    out->clear();
    QVector<dw::TransferItem> t;
    if (!dw::xfer::readJson(json, &t, err)) return false;
    for (const dw::TransferItem &x : t) out->append(fromTransfer(x));
    return true;
}

// ------------------------------------------------------------------------------ temporary folders

QString dragDirPrefix() { return QDir::temp().filePath("cpmdiskwrangler-drag-"); }

QString newDragDir(QString *err) { return dw::xfer::newDragDir(dragDirPrefix(), err); }

int cleanStale(qint64 maxAgeSecs) { return dw::xfer::cleanStale(dragDirPrefix(), maxAgeSecs); }

// ------------------------------------------------------------------------------ the drag data

dw::DragSpec dragSpec(bool trimText, const QString &imageName) {
    dw::DragSpec s;
    s.policy = std::make_shared<CpmPolicy>(false, trimText);
    s.filesystem = "cpm";
    s.generator = Generator;
    s.imageName = imageName;
    s.tempPrefix = dragDirPrefix();
    return s;
}

DragOutMime::DragOutMime(Exporter exporter, bool trimText, const QString &imageName)
    : dw::DragOutMime(
          [exporter](QVector<dw::TransferItem> *out, QString *err) {
              QVector<XItem> items;
              if (!exporter(&items, err)) return false;
              for (const XItem &x : items) out->append(toTransfer(x));
              return true;
          },
          dragSpec(trimText, imageName)) {}

} // namespace RetroFiles
