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
// interop_adw: built against Acorn Disk Wrangler's own dragout.cpp (qmake ADW=<path to its source>), so the
// retro-files exchange is tested with the real implementation on both sides.
//   interop_adw make <dir>      ADW writes an ADFS file, two DFS files and a DOS file the way its drags do;
//                               prints the path of the JSON document it made
//   interop_adw read <json>     ADW reads a document (e.g. one this program made) and prints what it imports
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <cstdio>
#include "dragout.h"

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QStringList a = app.arguments().mid(1);
    if (a.size() == 2 && a[0] == "make") {
        QString dir = a[1];
        QDir().mkpath(dir);
        auto run = [&](const QVector<XItem> &items, int pane, const QString &sub) {
            QString d = dir + "/" + sub;
            QDir().mkpath(d);
            QStringList top, errors;
            QJsonArray j;
            RetroFiles::writeHost(items, d, false, &top, &j, &errors);
            QFile f(d + ".json");
            f.open(QIODevice::WriteOnly);
            f.write(RetroFiles::makeJson(j, d, "test.img", pane));
            printf("%s\n", qPrintable(d + ".json"));
            for (const QString &e : errors) printf("error: %s\n", qPrintable(e));
        };
        XItem adfs;
        adfs.name = "Elite"; adfs.src = PaneAdfs; adfs.data = QByteArray(3000, 'E');
        adfs.load = 0xFFFF1900; adfs.exec = 0xFFFF8023; adfs.hasAddr = true; adfs.adfsFlags = Adfs::FlagL | Adfs::FlagR;
        XItem adfsDir;
        adfsDir.name = "Games"; adfsDir.isDir = true; adfsDir.src = PaneAdfs; adfsDir.adfsFlags = Adfs::FlagR;
        XItem readme;
        readme.name = "ReadMe"; readme.src = PaneAdfs; readme.data = "Hello from ADFS\r"; readme.adfsFlags = Adfs::FlagR | Adfs::FlagW;
        adfsDir.kids << readme;
        run({adfs, adfsDir}, PaneAdfs, "adfs");
        XItem d1, d2;
        d1.name = "BOOT"; d1.dfsDir = '$'; d1.src = PaneDfs0; d1.data = "*RUN PROG\r"; d1.load = 0x1900; d1.exec = 0x8023;
        d2.name = "PROG"; d2.dfsDir = 'A'; d2.src = PaneDfs0; d2.data = QByteArray(700, 'P'); d2.locked = true;
        run({d1, d2}, PaneDfs0, "dfs");
        XItem dos;
        dos.name = "AUTOEXEC.BAT"; dos.src = PaneDos; dos.data = "ECHO HI\r\n"; dos.dosAttr = Dos::AttrR | Dos::AttrA | Dos::AttrS;
        run({dos}, PaneDos, "fat");
        return 0;
    }
    if (a.size() == 2 && a[0] == "read") {
        QFile f(a[1]);
        if (!f.open(QIODevice::ReadOnly)) { printf("cannot open %s\n", qPrintable(a[1])); return 1; }
        QVector<XItem> items;
        QString err;
        if (!RetroFiles::readJson(f.readAll(), &items, &err)) { printf("ADW refused it: %s\n", qPrintable(err)); return 1; }
        std::function<void(const XItem &, int)> show = [&](const XItem &x, int depth) {
            printf("%*s%s %s src=%d bytes=%d locked=%d dosAttr=%02x\n", depth * 2, "", x.isDir ? "dir " : "file",
                   qPrintable(x.name), x.src, x.data.size(), int(x.locked), x.dosAttr);
            for (const XItem &k : x.kids) show(k, depth + 1);
        };
        for (const XItem &x : items) show(x, 0);
        return 0;
    }
    printf("usage: interop_adw make <dir> | read <json>\n");
    return 2;
}
