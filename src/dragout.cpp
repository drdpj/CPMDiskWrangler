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

// How CP/M items become host files: optionally a "userN" folder per user number, the ^Z padding optionally trimmed,
// and names worked out together (below).
class CpmPolicy : public dw::xfer::HostPolicy {
public:
    CpmPolicy(bool folderPerUser, bool trimText) : m_folders(folderPerUser), m_trim(trimText) {}
    static int userOf(const dw::TransferItem &it) { return it.attrs.value("cpm")["user"].toInt(); }
    QString topFolder(const dw::TransferItem &it) const override { return m_folders ? QString("user%1").arg(qMax(0, userOf(it))) : QString(); }
    QByteArray content(const dw::TransferItem &it) const override { return m_trim ? trimCpmText(it.data) : it.data; }
    QString errorLabel(const dw::TransferItem &it) const override { return QString("%1:%2").arg(userOf(it)).arg(it.name); }
    // The host names, worked out first so that names which keep their case (lower-case ones) get theirs before the
    // upper-case ones, which are written in lower case ... unless that would clash: then they keep their case too, and
    // only a host that can't tell the two apart (Windows, macOS) adds _2 (the importer takes it off again).
    bool planNames(const QVector<dw::TransferItem> &items, const QString &hostDir, QVector<QString> *names) const override {
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
private:
    bool m_folders, m_trim;
};

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

DragOutMime::DragOutMime(Exporter exporter, bool trimText, const QString &imageName)
    : dw::DragOutMime(
          [exporter](QVector<dw::TransferItem> *out, QString *err) {
              QVector<XItem> items;
              if (!exporter(&items, err)) return false;
              for (const XItem &x : items) out->append(toTransfer(x));
              return true;
          },
          [&] {
              dw::DragSpec s;
              s.policy = std::make_shared<CpmPolicy>(false, trimText);
              s.filesystem = "cpm";
              s.generator = Generator;
              s.imageName = imageName;
              s.tempPrefix = dragDirPrefix();
              return s;
          }()) {}

} // namespace RetroFiles
