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
// interop_adw: built against Acorn Disk Wrangler's own host naming (hostpolicy.cpp: qmake ADW=<path to its source>) and
// DiskWranglerCore's retro-files writer and reader, so the retro-files exchange is tested with Acorn's rules on one side
// and the core's reader on the other. Both programs' windows are built on the same core items (dw::TransferItem).
//   interop_adw make <dir>      ADW writes an ADFS file, two DFS files and a DOS file the way its drags do;
//                               prints the path of the JSON document it made
//   interop_adw read <json>     ADW reads a document (e.g. one this program made) and prints what it imports
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <cstdio>
#include <functional>
#include <diskwrangler/xfer/retrofiles.h>
#include <diskwrangler/xfer/transferitem.h>
#include "hostpolicy.h"

static QString hex8(quint32 v) { return QString("%1").arg(v, 8, 16, QLatin1Char('0')).toUpper(); }

static dw::TransferItem adfsItem(const QString &name, bool dir, const QByteArray &data, quint32 load, quint32 exec, const QString &access) {
    dw::TransferItem t;
    t.name = name; t.isDir = dir; t.data = data; t.sourceFs = "adfs";
    QJsonObject a;
    if (!dir) { a["load"] = hex8(load); a["exec"] = hex8(exec); }
    a["access"] = access;
    t.attrs["acorn"] = a;
    return t;
}

static dw::TransferItem dfsItem(QChar dir, const QString &name, const QByteArray &data, quint32 load, quint32 exec, bool locked) {
    dw::TransferItem t;
    t.name = QString(dir) + "." + name; t.data = data; t.sourceFs = "dfs";      // a DFS name always carries its directory
    QJsonObject a;
    a["load"] = hex8(load); a["exec"] = hex8(exec);
    a["access"] = locked ? "L" : "WR";
    t.attrs["acorn"] = a;
    return t;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QStringList a = app.arguments().mid(1);
    if (a.size() == 2 && a[0] == "make") {
        QString dir = a[1];
        QDir().mkpath(dir);
        auto run = [&](const QVector<dw::TransferItem> &items, const QString &fs, const QString &sub) {
            QString d = dir + "/" + sub;
            QDir().mkpath(d);
            QStringList top, errors;
            QJsonArray j;
            dw::xfer::writeHost(items, d, RetroFiles::AcornPolicy(false), &top, &j, &errors);
            QFile f(d + ".json");
            f.open(QIODevice::WriteOnly);
            f.write(dw::xfer::makeJson(j, d, "test.img", fs, "Acorn Disk Wrangler"));
            printf("%s\n", qPrintable(d + ".json"));
            for (const QString &e : errors) printf("error: %s\n", qPrintable(e));
        };
        dw::TransferItem adfs = adfsItem("Elite", false, QByteArray(3000, 'E'), 0xFFFF1900, 0xFFFF8023, "LR");
        dw::TransferItem adfsDir = adfsItem("Games", true, QByteArray(), 0, 0, "R");
        adfsDir.kids << adfsItem("ReadMe", false, "Hello from ADFS\r", 0, 0, "WR");
        run({adfs, adfsDir}, "adfs", "adfs");
        run({dfsItem('$', "BOOT", "*RUN PROG\r", 0x1900, 0x8023, false),
             dfsItem('A', "PROG", QByteArray(700, 'P'), 0, 0, true)}, "dfs", "dfs");
        dw::TransferItem dos;
        dos.name = "AUTOEXEC.BAT"; dos.sourceFs = "fat"; dos.data = "ECHO HI\r\n";
        QJsonObject f;
        f["readonly"] = true; f["hidden"] = false; f["system"] = true; f["archive"] = true;
        dos.attrs["fat"] = f;
        run({dos}, "fat", "fat");
        return 0;
    }
    if (a.size() == 2 && a[0] == "read") {
        QFile f(a[1]);
        if (!f.open(QIODevice::ReadOnly)) { printf("cannot open %s\n", qPrintable(a[1])); return 1; }
        QVector<dw::TransferItem> items;
        QString err;
        if (!dw::xfer::readJson(f.readAll(), &items, &err)) { printf("ADW refused it: %s\n", qPrintable(err)); return 1; }
        std::function<void(const dw::TransferItem &, int)> show = [&](const dw::TransferItem &x, int depth) {
            printf("%*s%s %s src=%s bytes=%d\n", depth * 2, "", x.isDir ? "dir " : "file", qPrintable(x.name),
                   qPrintable(x.sourceFs), int(x.data.size()));
            for (const dw::TransferItem &k : x.kids) show(k, depth + 1);
        };
        for (const dw::TransferItem &x : items) show(x, 0);
        return 0;
    }
    printf("usage: interop_adw make <dir> | read <json>\n");
    return 2;
}
