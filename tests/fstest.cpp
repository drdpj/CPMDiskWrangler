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
// fstest: exercises the filesystem and the containers, and leaves images for independent checking.
//
//   fstest <outdir> <format> <raw|imd|edsk>
//     Makes a blank disc of <format>, adds files of awkward sizes in several user numbers, deletes, renames, sets
//     attributes, saves, reopens it (by detection and by the named format) and checks every file. Leaves:
//       <outdir>/<format>.<ext>          the image
//       <outdir>/<format>.logical        the same disc as a raw image in logical track order (what cpmtools reads)
//       <outdir>/<format>.diskdef        the format in diskdefs syntax
//       <outdir>/<format>.expect/<u>/<name>   what every file should contain
//   fstest roundtrip <image>     opens a container and serialises it with no changes: must be byte-identical
//   fstest atomic <outdir>       a failed add (disc full / directory full) must leave the image exactly as it was
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <cstdio>
#include "../cpmfs.h"
#include "../detect.h"
#include "../diskimage.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", qPrintable(QString(msg))); ++failures; } } while (0)

static QByteArray content(int size, quint32 seed, bool text) {
    QRandomGenerator g(seed);
    QByteArray d(size, '\0');
    for (int i = 0; i < size; ++i) d[i] = text ? char("ABCDEFGHIJ klmnop\r\n"[g.bounded(19)]) : char(g.bounded(256));
    return d;
}

struct Expect { int user; QString name; QByteArray data; quint16 attrs; };

static bool verify(Cpm::Volume &v, const QVector<Expect> &ex, const QString &what) {
    int before = failures;
    CHECK(v.files().size() == ex.size(), QString("%1: %2 files, expected %3").arg(what).arg(v.files().size()).arg(ex.size()));
    for (const Expect &e : ex) {
        int i = v.indexOf(e.user, e.name);
        CHECK(i >= 0, QString("%1: %2:%3 missing").arg(what).arg(e.user).arg(e.name));
        if (i < 0) continue;
        QByteArray d;
        QString err;
        CHECK(v.readFile(v.files()[i], &d, &err), what + ": read " + e.name + ": " + err);
        CHECK(d == e.data, QString("%1: %2:%3 content differs (%4 vs %5 bytes)").arg(what).arg(e.user).arg(e.name).arg(d.size()).arg(e.data.size()));
        CHECK(v.files()[i].attrs == e.attrs, QString("%1: %2 attributes %3, expected %4").arg(what, e.name).arg(v.files()[i].attrs).arg(e.attrs));
    }
    Cpm::Report r = v.check();
    CHECK(r.ok(), what + ": check: " + r.errors.join("; "));
    CHECK(r.warnings.isEmpty(), what + ": check warnings: " + r.warnings.join("; "));
    return failures == before;
}

static Disk::Kind kindOf(const QString &k) {
    if (k == "imd") return Disk::Kind::Imd;
    if (k == "edsk") return Disk::Kind::Edsk;
    return Disk::Kind::Raw;
}

static int formatTest(const QString &outdir, const QString &fname, const QString &kname) {
    const Cpm::Format *lf = Cpm::findFormat(fname);
    if (!lf) { printf("no format %s\n", qPrintable(fname)); return 2; }
    Cpm::Format f = *lf;
    QString err;
    f.derive(&err);
    Disk::Kind kind = kindOf(kname);
    QString ext = kname == "raw" ? "img" : kname == "imd" ? "imd" : "dsk";
    QString path = outdir + "/" + fname + "." + ext;
    QDir().mkpath(outdir);
    printf("== %s as %s: %s\n", qPrintable(fname), qPrintable(kname), qPrintable(f.summary()));
    bool stamps = f.os == Cpm::Os::V3;
    Disk::Image img;
    if (!Cpm::makeBlank(&img, kind, f, stamps, "TESTDISC", &err)) {
        if (f.t0sectrk > 0 && kind == Disk::Kind::Raw) { printf("  refused as raw, as it should be: %s\nok\n", qPrintable(err)); return 0; }
        printf("  FAIL makeBlank: %s\n", qPrintable(err));
        return 1;
    }
    if (!img.saveAs(path, kind, &err)) { printf("  FAIL saveAs: %s\n", qPrintable(err)); return 1; }

    // reopen with the named format
    Disk::Image im2;
    CHECK(im2.open(path, &err), "open: " + err);
    if (f.t0sectrk > 0) {
        const Disk::Track *t0 = im2.track(0, 0), *t1 = im2.track(1, 0);
        CHECK(t0 && t0->secs.size() == f.t0sectrk && t0->secs[0].size == f.t0seclen, "track 0 has its own shape");
        if (kind == Disk::Kind::Imd) CHECK(t0 && t1 && (t0->mode <= 2) == f.t0fm && t1->mode >= 3, QString("track 0 mode %1, track 1 mode %2").arg(t0 ? t0->mode : -1).arg(t1 ? t1->mode : -1));
    }
    if (kind == Disk::Kind::Raw) {
        Disk::RawLayout l;
        l.seclen = f.seclen; l.sectrk = f.sectrk; l.cyls = f.cylinders; l.heads = f.physHeads;
        CHECK(im2.setRawLayout(l, &err), err);
    }
    Cpm::Volume v;
    CHECK(v.attach(&im2, f, &err), "attach: " + err);
    CHECK(v.files().isEmpty(), "blank disc has files");
    CHECK(v.check().ok(), "blank disc check: " + v.check().errors.join("; "));
    if (stamps) CHECK(v.hasStamps() && v.label().present && v.label().name == "TESTDISC", "stamps/label missing on new CP/M 3 disc");

    // files of awkward sizes: empty, one byte, record and extent boundaries, several extents
    qint64 cap = v.freeBytes();
    QVector<int> sizes = {0, 1, 127, 128, 129, 1000, 16383, 16384, 16385, 40000};
    if (cap > 200 * 1024) sizes << 70000 << 131072 + 300;
    QVector<Expect> ex;
    int n = 0;
    QDateTime when(QDate(1986, 3, 14), QTime(15, 9));
    for (int s : sizes) {
        Expect e;
        e.user = n % 3 == 2 ? 3 : 0;
        e.name = QString("F%1.%2").arg(n).arg(n % 2 ? "TXT" : "BIN");
        e.data = content(s, quint32(n * 7 + 1), n % 2);
        e.attrs = n == 4 ? Cpm::AttrRO : n == 5 ? quint16(Cpm::AttrSYS | Cpm::AttrARC) : 0;
        if (v.freeBytes() < s + f.blocksize || v.freeEntries() < 4) break;
        CHECK(v.addFile(e.user, e.name, e.data, e.attrs, when, false, &err), "add " + e.name + ": " + err);
        ex << e;
        ++n;
    }
    CHECK(verify(v, ex, "after adding"), "");
    // replace refused for an existing name without replace, and for a read-only file even with it
    CHECK(!v.addFile(0, "F0.BIN", "x", 0, when, false, &err), "adding over an existing file without replace succeeded");
    CHECK(!v.addFile(ex[4].user, ex[4].name, "x", 0, when, true, &err), "replacing a read-only file succeeded");
    // replace, delete, rename (with a user change), set attributes
    ex[1].data = content(3000, 99, true);
    CHECK(v.addFile(ex[1].user, ex[1].name, ex[1].data, 0, when, true, &err), "replace: " + err);
    CHECK(v.deleteFile(ex[2].user, ex[2].name, &err), "delete: " + err);
    ex.removeAt(2);
    CHECK(v.rename(ex[2].user, ex[2].name, 5, "RENAMED.DOC", &err), "rename: " + err);
    ex[2].user = 5; ex[2].name = "RENAMED.DOC";
    CHECK(v.setAttrs(ex[0].user, ex[0].name, Cpm::AttrARC | Cpm::AttrF1, &err), "attrs: " + err);
    ex[0].attrs = Cpm::AttrARC | Cpm::AttrF1;
    CHECK(!v.rename(0, ex[0].name, ex[1].user, ex[1].name, &err), "rename onto an existing name succeeded");
    verify(v, ex, "after changes (staged)");
    if (stamps) {
        int i = v.indexOf(ex[3].user, ex[3].name);
        CHECK(i >= 0 && v.files()[i].created == when && v.files()[i].updated == when, "time stamps not as written");
    }
    CHECK(im2.commit(&err, false), "commit: " + err);

    // reopen by detection
    Disk::Image im3;
    CHECK(im3.open(path, &err), "reopen: " + err);
    Detect::Result r = Detect::detect(im3);
    CHECK(!r.ranked.isEmpty(), "detection found nothing: " + r.why);
    if (!r.ranked.isEmpty()) {
        const Cpm::Format &got = r.ranked.first().fmt;
        CHECK(got.sameLayout(f), QString("detection chose %1 (%2), not %3").arg(got.name, got.summary(), fname));
        printf("  detected: %s, confidence %s, %s\n", qPrintable(got.name), qPrintable(r.confidence), r.safeToWrite ? "writable" : "read-only");
        Cpm::Volume v3;
        if (v3.attach(&im3, r.ranked.first().fmt, &err)) verify(v3, ex, "after reopening by detection");
        else CHECK(false, "attach after detection: " + err);
    }

    // leave the material for the independent checks
    QFile dd(outdir + "/" + fname + ".diskdef");
    if (dd.open(QIODevice::WriteOnly)) {
        Cpm::Format plain = f;
        plain.sides = Cpm::SideOrder::Alt;     // the .logical image is already in logical track order
        dd.write((plain.toDiskdef() + "\n").toLatin1());
    }
    QFile lg(outdir + "/" + fname + ".logical");
    if (lg.open(QIODevice::WriteOnly)) {
        for (int t = 0; t < f.tracks; ++t)
            for (int s = 0; s < f.sectrk; ++s) {
                int c, h, i;
                Cpm::Format noSkew = f;
                noSkew.skewtab.clear(); noSkew.skew = 0;
                noSkew.derive(&err);
                noSkew.locate(t * f.sectrk + s, &c, &h, &i);
                QByteArray b;
                if (im3.sectorCount(c, h) != f.sectrk || !im3.read(c, h, i, &b) || b.size() != f.seclen)
                    b = QByteArray(f.seclen, char(0xE5));      // a track of another shape (boot area): filler
                lg.write(b);
            }
    }
    QDir().mkpath(outdir + "/" + fname + ".expect");
    for (const Expect &e : ex) {
        QString d = outdir + "/" + fname + ".expect/" + QString::number(e.user);
        QDir().mkpath(d);
        QFile o(d + "/" + e.name.toLower());
        if (o.open(QIODevice::WriteOnly)) o.write(e.data);
    }
    return 0;
}

static int roundtrip(const QString &path) {
    Disk::Image img;
    QString err;
    if (!img.open(path, &err)) { printf("FAIL open %s: %s\n", qPrintable(path), qPrintable(err)); return 1; }
    QByteArray out;
    if (!img.serialize(img.kind(), &out, &err)) { printf("FAIL serialize %s: %s\n", qPrintable(path), qPrintable(err)); return 1; }
    QFile f(path);
    f.open(QIODevice::ReadOnly);
    bool same = f.readAll() == out;
    printf("%s %s (%s)\n", same ? "same" : "FAIL differs:", qPrintable(QFileInfo(path).fileName()), qPrintable(Disk::kindName(img.kind())));
    return same ? 0 : 1;
}

static int atomic(const QString &outdir) {
    QString err;
    Cpm::Format f = *Cpm::findFormat("ibm-3740");
    f.derive(&err);
    Disk::Image img;
    Cpm::makeBlank(&img, Disk::Kind::Imd, f, false, QString(), &err);
    QString p = outdir + "/atomic.imd";
    QDir().mkpath(outdir);
    img.saveAs(p, Disk::Kind::Imd, &err);
    Cpm::Volume v;
    v.attach(&img, f, &err);
    CHECK(v.addFile(0, "A.BIN", content(100000, 1, false), 0, QDateTime(), false, &err), "first add: " + err);
    auto snap = img.snapshot();
    int freeB = v.freeBlocks(), freeE = v.freeEntries();
    CHECK(!v.addFile(0, "HUGE.BIN", content(200000, 2, false), 0, QDateTime(), false, &err), "an add bigger than the disc succeeded");
    printf("  disc full says: %s\n", qPrintable(err));
    CHECK(img.snapshot() == snap && v.freeBlocks() == freeB && v.freeEntries() == freeE, "a failed add left changes behind");
    // fill the directory with empty files, then one more must fail cleanly
    int k = 0;
    while (v.addFile(0, QString("E%1").arg(k), QByteArray(), 0, QDateTime(), false, &err)) ++k;
    printf("  directory full after %d more files: %s\n", k, qPrintable(err));
    CHECK(v.freeEntries() == 0, "directory not full");
    CHECK(v.check().ok(), "check after filling the directory: " + v.check().errors.join("; "));
    return 0;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QStringList a = app.arguments().mid(1);
    QStringList warn;
    Cpm::setExtraDiskdefFiles({}, &warn);
    int rc;
    if (a.size() >= 2 && a[0] == "roundtrip") {
        rc = 0;
        for (const QString &p : a.mid(1)) rc |= roundtrip(p);
        return rc;
    }
    bool force = a.removeAll("--force") > 0;      // addto: write with the best reading even if not confirmed
    if (a.size() == 4 && a[0] == "addto") {
        // addto <image> <NAME.EXT> <size>: detect, add a file, save; writes <image>.added (the content) beside it
        Disk::Image img;
        QString err;
        if (!img.open(a[1], &err)) { printf("FAIL open: %s\n", qPrintable(err)); return 1; }
        Detect::Result r = Detect::detect(img);
        if (r.ranked.isEmpty() || (!r.safeToWrite && !force)) { printf("FAIL: detection not confident enough to write (%s)\n", qPrintable(r.why)); return 1; }
        Cpm::Volume v;
        if (!v.attach(&img, r.ranked.first().fmt, &err)) { printf("FAIL attach: %s\n", qPrintable(err)); return 1; }
        QByteArray d = content(a[3].toInt(), 4242, true);
        if (!v.addFile(0, a[2], d, 0, QDateTime(), true, &err)) { printf("FAIL add: %s\n", qPrintable(err)); return 1; }
        if (!img.commit(&err, true)) { printf("FAIL commit: %s\n", qPrintable(err)); return 1; }
        QFile o(a[1] + ".added");
        o.open(QIODevice::WriteOnly);
        o.write(d);
        printf("added %s (%d bytes) using %s\n", qPrintable(a[2]), d.size(), qPrintable(r.ranked.first().fmt.name));
        return 0;
    }
    if (a.size() == 1 && a[0] == "library") {
        // a built-in definition that replaces a cpmtools one must describe the same layout
        QFile f(":/diskdefs/cpmtools");
        f.open(QIODevice::ReadOnly);
        QVector<Cpm::Format> ct = Cpm::parseDiskdefs(QString::fromUtf8(f.readAll()), "cpmtools", nullptr);
        int n = 0;
        for (const Cpm::Format &b : Cpm::library()) {
            if (b.origin != "built-in") continue;
            for (Cpm::Format c : ct) {
                if (c.name != b.name) continue;
                ++n;
                if (c.heads == 0 && b.heads) { c.heads = b.heads; c.sides = b.sides; QString e; c.derive(&e); }
                CHECK(c.sameLayout(b), QString("built-in %1 differs from cpmtools': %2 vs %3").arg(b.name, b.summary(), c.summary()));
                CHECK(c.dirBlocks == b.dirBlocks, QString("built-in %1 reserves %2 directory blocks, cpmtools %3").arg(b.name).arg(b.dirBlocks).arg(c.dirBlocks));
            }
        }
        printf("%d built-in definitions replace cpmtools ones\n", n);
        printf("%s\n", failures ? "FAILURES" : "ok");
        return failures ? 1 : 0;
    }
    if (a.size() == 2 && a[0] == "atomic") rc = atomic(a[1]);
    else if (a.size() == 3) rc = formatTest(a[0], a[1], a[2]);
    else { printf("usage: fstest <outdir> <format> <raw|imd|edsk> | roundtrip <image>... | atomic <outdir>\n"); return 2; }
    printf("%s\n", failures ? qPrintable(QString("%1 FAILURES").arg(failures)) : "ok");
    return rc || failures ? 1 : 0;
}
