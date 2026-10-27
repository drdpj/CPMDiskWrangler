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
// detecttest: prints what detection makes of each image (ranked formats, confidence, the listing).
//   detecttest [-v] [-x outdir] image...      -x extracts every file of the best reading into outdir/<image>/
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <cstdio>
#include "../cpmfs.h"
#include "../detect.h"
#include "../diskimage.h"

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    bool verbose = args.removeAll("-v") > 0;
    QString xdir;
    int xi = args.indexOf("-x");
    if (xi >= 0 && xi + 1 < args.size()) { xdir = args[xi + 1]; args.removeAt(xi); args.removeAt(xi); }
    QStringList warn;
    Cpm::setExtraDiskdefFiles({}, &warn);
    for (const QString &w : warn) printf("diskdefs: %s\n", qPrintable(w));
    printf("library: %d formats\n", Cpm::library().size());
    int failures = 0;
    for (const QString &p : args) {
        Disk::Image img;
        QString err;
        printf("\n=== %s\n", qPrintable(p));
        if (!img.open(p, &err)) { printf("  open failed: %s\n", qPrintable(err)); ++failures; continue; }
        QElapsedTimer t; t.start();
        Detect::Result r = Detect::detect(img);
        printf("  %s\n  detection took %lld ms; confidence %s; %s\n  %s\n", qPrintable(r.physical), t.elapsed(),
               qPrintable(r.confidence), r.safeToWrite ? "writable" : "read-only", qPrintable(r.why));
        for (int i = 0; i < r.ranked.size() && i < (verbose ? 12 : 4); ++i) {
            const Detect::Candidate &c = r.ranked[i];
            printf("  %2d. %7.1f  %-14s %s\n", i + 1, c.score, qPrintable(c.fmt.name), qPrintable(c.fmt.summary()));
            if (verbose || i == 0) printf("        %s\n", qPrintable(c.evidence.mid(1).join("; ")));
            if (verbose) printf("        key %s\n", qPrintable(c.key()));
        }
        if (r.ranked.isEmpty()) { ++failures; continue; }
        Cpm::Volume v;
        if (!v.attach(&img, r.ranked.first().fmt, &err)) { printf("  attach failed: %s\n", qPrintable(err)); ++failures; continue; }
        Cpm::Report rep = v.check();
        printf("  %d files, %d free blocks, %lldK free, label '%s', stamps %s; check: %d errors, %d warnings\n",
               v.files().size(), v.freeBlocks(), v.freeBytes() / 1024, qPrintable(v.label().name),
               v.hasStamps() ? qPrintable(v.stampKinds()) : "none", rep.errors.size(), rep.warnings.size());
        for (const QString &e : rep.errors.mid(0, 5)) printf("    error: %s\n", qPrintable(e));
        if (verbose) for (const QString &e : rep.warnings.mid(0, 5)) printf("    warning: %s\n", qPrintable(e));
        int shown = 0;
        for (const Cpm::File &f : v.files()) {
            if (verbose || shown < 8)
                printf("    %2d:%-12s %7lld %s%s\n", f.user, qPrintable(f.name), f.size, qPrintable(Cpm::attrString(f.attrs)),
                       f.problem.isEmpty() ? "" : qPrintable("  [" + f.problem + "]"));
            ++shown;
        }
        if (!xdir.isEmpty()) {
            QString d = xdir + "/" + QFileInfo(p).fileName();
            QDir().mkpath(d);
            for (const Cpm::File &f : v.files()) {
                QByteArray data;
                if (!v.readFile(f, &data, &err)) { printf("    read %s failed: %s\n", qPrintable(f.name), qPrintable(err)); continue; }
                QString sub = d + QString("/%1").arg(f.user);
                QDir().mkpath(sub);
                QFile o(sub + "/" + f.name.toLower());
                if (o.open(QIODevice::WriteOnly)) o.write(data);
            }
        }
    }
    return failures ? 1 : 0;
}
