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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>
#include <map>
#include <vector>

namespace RetroFiles {

const char *const MimeType = "application/x-retro-files+json";
const char *const Generator = "CP/M Disk Wrangler";
static const char *const UriList = "text/uri-list";

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

// ------------------------------------------------------------------------------ writing to the host

static QString unique(const QString &name, QSet<QString> &used) {
    QString n = name;
    int dot = name.lastIndexOf('.');
    for (int k = 2; used.contains(n.toLower()); ++k)
        n = dot > 0 ? name.left(dot) + "_" + QString::number(k) + name.mid(dot) : name + "_" + QString::number(k);
    used.insert(n.toLower());
    return n;
}

static QJsonObject cpmAttrs(const XItem &x) {
    QJsonObject a;
    a["user"] = qMax(0, x.user);
    a["readonly"] = x.readOnly;
    a["system"] = x.system;
    a["archive"] = x.archive;
    for (int k = 0; k < 4; ++k) if (x.interfaceAttrs & (1 << k)) a[QString("f%1").arg(k + 1)] = true;
    return a;
}

void writeHost(const QVector<XItem> &items, const QString &hostDir, bool folderPerUser, bool trimText,
               QStringList *topLevel, QJsonArray *jsonItems, QStringList *errors) {
    QMap<int, QSet<QString>> used;            // per folder (user, or -1 for the top)
    QSet<int> folders;
    for (const XItem &x : items) {
        int u = folderPerUser ? qMax(0, x.user) : -1;
        QString relDir;
        if (u >= 0) {
            relDir = QString("user%1").arg(u);
            if (!folders.contains(u)) {
                folders.insert(u);
                if (!QDir().mkpath(hostDir + "/" + relDir)) { errors->append("cannot create folder " + relDir); continue; }
                QJsonObject d;
                d["path"] = relDir;
                d["type"] = "directory";
                d["name"] = relDir;
                jsonItems->append(d);
                if (topLevel) topLevel->append(relDir);
            }
        }
        QString host = unique(hostSafeName(x.name.toLower()), used[u]);
        QString rel = relDir.isEmpty() ? host : relDir + "/" + host;
        QByteArray data = trimText ? trimCpmText(x.data) : x.data;
        QFile f(hostDir + "/" + rel);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size()) {
            errors->append(QString("%1:%2: %3").arg(x.user).arg(x.name, f.errorString()));
            continue;
        }
        f.close();
        if (x.when.isValid()) {
            QFile t(hostDir + "/" + rel);
            if (t.open(QIODevice::Append)) t.setFileTime(x.when, QFileDevice::FileModificationTime);
        }
        QJsonObject j;
        j["path"] = rel;
        j["type"] = "file";
        j["name"] = x.name;
        j["size"] = double(data.size());
        if (x.when.isValid()) j["modified"] = x.when.toString("yyyy-MM-ddTHH:mm:ss");
        QJsonObject attrs;
        attrs["cpm"] = cpmAttrs(x);
        j["attributes"] = attrs;
        jsonItems->append(j);
        if (topLevel && relDir.isEmpty()) topLevel->append(rel);
    }
}

QByteArray makeJson(const QJsonArray &items, const QString &hostDir, const QString &imageName) {
    QJsonObject src;
    src["filesystem"] = "cpm";
    if (!imageName.isEmpty()) src["image"] = imageName;
    QJsonObject o;
    o["format"] = "retro-files";
    o["version"] = 1;
    o["generator"] = Generator;
    o["source"] = src;
    o["base"] = QUrl::fromLocalFile(hostDir).toString();
    o["items"] = items;
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

// ------------------------------------------------------------------------------ reading a drop

bool readJson(const QByteArray &json, QVector<XItem> *out, QString *err) {
    out->clear();
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject()) { *err = "the dropped file list is not valid JSON: " + pe.errorString(); return false; }
    QJsonObject o = doc.object();
    if (o["format"].toString() != "retro-files") { *err = "the dropped data is not a retro-files list"; return false; }
    if (o["version"].toInt() != 1) { *err = QString("retro-files version %1 is not supported").arg(o["version"].toInt()); return false; }
    QString fs = o["source"].toObject()["filesystem"].toString();
    QString base = QUrl(o["base"].toString()).toLocalFile();
    if (base.isEmpty() || !QFileInfo(base).isDir()) { *err = "the dropped files' folder is missing"; return false; }
    QString canonBase = QFileInfo(base).canonicalFilePath();

    struct Node { XItem x; std::vector<int> kids; };
    std::vector<Node> nodes;
    std::vector<int> roots;
    std::map<QString, int> byPath;
    for (const QJsonValue &v : o["items"].toArray()) {
        QJsonObject it = v.toObject();
        QString path = it["path"].toString();
        QStringList parts = path.split('/');
        if (path.isEmpty() || path.startsWith('/') || parts.contains("..") || parts.contains(".") || parts.contains("")) {
            *err = QString("'%1': not a path inside the drop folder").arg(path);
            return false;
        }
        Node n;
        XItem &x = n.x;
        x.isDir = it["type"].toString() == "directory";
        x.name = it["name"].toString().isEmpty() ? parts.last() : it["name"].toString();
        x.srcFs = fs;
        x.when = QDateTime::fromString(it["modified"].toString(), "yyyy-MM-ddTHH:mm:ss");
        QJsonObject attrs = it["attributes"].toObject();
        QJsonObject cpm = attrs["cpm"].toObject(), fat = attrs["fat"].toObject(), acorn = attrs["acorn"].toObject();
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
        if (fs == "dfs" && x.name.size() >= 3 && x.name[1] == '.') x.name = x.name.mid(2);   // "D.NAME": drop D
        QString abs = QDir(base).filePath(path);
        QString canon = QFileInfo(abs).canonicalFilePath();
        if (canon.isEmpty() || !canon.startsWith(canonBase + "/")) {
            *err = QString("'%1': the dropped file is missing or outside the drop folder").arg(path);
            return false;
        }
        if (!x.isDir) {
            QFile f(abs);
            if (!f.open(QIODevice::ReadOnly)) { *err = QString("%1: %2").arg(path, f.errorString()); return false; }
            x.data = f.readAll();
            if (it.contains("size") && qint64(it["size"].toDouble()) != x.data.size()) {
                *err = QString("'%1': the file is not the size the list says").arg(path);
                return false;
            }
        }
        int idx = int(nodes.size());
        nodes.push_back(n);
        QString parent = parts.size() > 1 ? path.left(path.lastIndexOf('/')) : QString();
        auto p = byPath.find(parent);
        if (!parent.isEmpty() && p != byPath.end() && nodes[size_t(p->second)].x.isDir) nodes[size_t(p->second)].kids.push_back(idx);
        else roots.push_back(idx);
        byPath[path] = idx;
    }
    std::function<XItem(int)> build = [&](int i) {
        XItem x = nodes[size_t(i)].x;
        for (int k : nodes[size_t(i)].kids) x.kids.append(build(k));
        return x;
    };
    for (int r : roots) out->append(build(r));
    if (out->isEmpty()) { *err = "the dropped list has no files"; return false; }
    return true;
}

// ------------------------------------------------------------------------------ temporary folders

QString dragDirPrefix() { return QDir::temp().filePath("cpmdiskwrangler-drag-"); }

static std::vector<std::unique_ptr<QTemporaryDir>> &dragDirs() {
    static std::vector<std::unique_ptr<QTemporaryDir>> dirs;
    return dirs;
}

QString newDragDir(QString *err) {
    std::unique_ptr<QTemporaryDir> d(new QTemporaryDir(dragDirPrefix() + "XXXXXX"));
    if (!d->isValid()) { *err = "cannot create a temporary folder: " + d->errorString(); return QString(); }
    QString p = d->path();
    dragDirs().push_back(std::move(d));
    return p;
}

int cleanStale(qint64 maxAgeSecs) {
    int n = 0;
    QFileInfo pre(dragDirPrefix());
    QDir tmp(pre.absolutePath());
    for (const QFileInfo &fi : tmp.entryInfoList({pre.fileName() + "*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (fi.lastModified().secsTo(QDateTime::currentDateTime()) < maxAgeSecs) continue;
        if (QDir(fi.absoluteFilePath()).removeRecursively()) ++n;
    }
    return n;
}

// ------------------------------------------------------------------------------ the drag data

DragOutMime::DragOutMime(Exporter exporter, bool trimText, const QString &imageName)
    : m_exporter(exporter), m_trim(trimText), m_image(imageName), m_state(new DragState) {}

QStringList DragOutMime::formats() const {
    QStringList f = QMimeData::formats();
    for (const char *m : {UriList, MimeType})
        if (!f.contains(m)) f << m;
    return f;
}

bool DragOutMime::hasFormat(const QString &mimeType) const { return formats().contains(mimeType); }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
QVariant DragOutMime::retrieveData(const QString &mimeType, QMetaType type) const {
    const int typeId = type.id();
#else
QVariant DragOutMime::retrieveData(const QString &mimeType, QVariant::Type type) const {
    const int typeId = int(type);                  // QVariant::Type values are QMetaType ids
#endif
    if (mimeType == UriList) {
        extract();
        if (typeId == QMetaType::QByteArray || typeId == QMetaType::QString) {
            QByteArray b;
            for (const QUrl &u : m_urls) b += u.toEncoded() + "\r\n";
            return typeId == QMetaType::QString ? QVariant(QString::fromUtf8(b)) : QVariant(b);
        }
        QVariantList l;
        for (const QUrl &u : m_urls) l << u;
        return l;
    }
    if (mimeType == MimeType) {
        extract();
        return m_json;
    }
    return QMimeData::retrieveData(mimeType, type);
}

void DragOutMime::extract() const {
    if (m_state->extracted) return;
    m_state->extracted = true;
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QVector<XItem> items;
    QString err;
    QString dir = m_exporter(&items, &err) ? newDragDir(&err) : QString();
    if (dir.isEmpty()) {
        m_state->errors << err;
    } else {
        QStringList top;
        QJsonArray j;
        writeHost(items, dir, false, m_trim, &top, &j, &m_state->errors);
        for (const QString &t : top) m_urls << QUrl::fromLocalFile(dir + "/" + t);
        m_json = makeJson(j, dir, m_image);
        m_state->dir = dir;
        m_state->files = j.size();
    }
    QGuiApplication::restoreOverrideCursor();
}

} // namespace RetroFiles
