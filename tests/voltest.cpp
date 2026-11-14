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
// voltest - CP/M Disk Wrangler's discs through DiskWranglerCore's filesystem interface (step 4): a CP/M and a Torch
// CP/N disc as dw::fs::Disc (src/cpmvolume.h), changes in Disc transactions and the checked save; and detection by
// tiers: CP/M discs are opened here, Acorn Disk Wrangler's discs (DFS, ADFS, FAT) are handed to it.
//   voltest <work dir>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <cstdio>
#include <memory>
#include <diskwrangler/fs/dfs/dfs.h>
#include "../src/cpmvolume.h"

using dw::fs::Path;

static int fails = 0;
#define CHECK(c, what) do { if (!(c)) { printf("  FAIL line %d: %s\n", __LINE__, qPrintable(QString(what))); ++fails; } \
                            else printf("  ok: %s\n", qPrintable(QString(what))); } while (0)

static bool blank(const QString &path, const char *fmt, dw::device::Kind kind, QString *err) {
    const Cpm::Format *f = Cpm::findFormat(fmt);
    if (!f) { *err = QString("no format ") + fmt; return false; }
    Cpm::Format g = *f;
    if (!g.derive(err)) return false;
    dw::device::Container img;
    return Cpm::makeBlank(&img, kind, g, false, QString(), err) && img.saveAs(path, kind, err);
}

static dw::fs::Verdict verdictOf(const QString &path) {
    dw::device::Container img;
    QString err;
    if (!img.open(path, &err)) return dw::fs::Verdict();
    const Detect::Result r = Detect::detect(img);
    return Cpm::verdict(img, path, r);
}

static void spit(const QString &p, const QByteArray &d) { QFile f(p); f.open(QIODevice::WriteOnly); f.write(d); }

// Everything a Store holds in memory, as text: what reload() must give back exactly (step 6b).
static QString describe(const Cpm::Store &st) {
    QStringList out;
    for (const Cpm::File &f : st.files()) {
        QStringList ents;
        for (int e : f.entries) ents << QString::number(e);
        out << QString("%1:%2 %3 a%4 e[%5] r%6 s%7 x%8 b%9 %10 %11 %12 %13").arg(f.user).arg(f.name, QString::fromLatin1(f.name11.toHex()))
                   .arg(f.attrs).arg(ents.join(',')).arg(f.records).arg(f.size).arg(f.exactSize).arg(f.blocks)
                   .arg(f.created.toString(Qt::ISODate), f.updated.toString(Qt::ISODate), f.accessed.toString(Qt::ISODate), f.problem);
    }
    const Cpm::Label &l = st.label();
    out << QString("label %1 %2 %3 %4 %5 %6").arg(l.present).arg(l.index).arg(l.name).arg(l.mode)
               .arg(l.created.toString(Qt::ISODate), l.updated.toString(Qt::ISODate));
    out << QString("free %1 %2 used %3 bytes %4").arg(st.freeBlocks()).arg(st.freeEntries()).arg(st.usedEntries()).arg(st.freeBytes());
    QVector<QByteArray> dir;
    if (const Cpm::Volume *v = dynamic_cast<const Cpm::Volume *>(&st)) dir = v->directory();
    else if (const Torch::Fs *t = dynamic_cast<const Torch::Fs *>(&st)) dir = t->directory();
    for (const QByteArray &e : dir) out << QString::fromLatin1(e.toHex());
    return out.join('\n');
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2) { printf("usage: voltest <work dir>\n"); return 2; }
    const QString W = argv[1];
    QDir().mkpath(W);
    QString err;
    QStringList warn;
    Cpm::setExtraDiskdefFiles({}, &warn);

    printf("CP/M disc (ibm-3740, ImageDisk)\n");
    {
        const QString p = W + "/cpm.imd";
        CHECK(blank(p, "ibm-3740", dw::device::Kind::Imd, &err), "blank disc: " + err);
        {   // a file to detect it by
            Cpm::OpenedDisc o;
            dw::device::Container img;
            img.open(p, &err);
            Cpm::Volume v;
            Cpm::Format f = *Cpm::findFormat("ibm-3740");
            f.derive(&err);
            CHECK(v.attach(&img, f, &err) && v.addFile(0, "SEED.TXT", "seed\x1a", 0, QDateTime(), false, &err) &&
                      img.commit(&err, false), "seed file: " + err);
        }
        Cpm::OpenedDisc o;
        CHECK(o.open(p, &err), "open by detection: " + err);
        CHECK(o.disc().mounts().size() == 1 && o.disc().volume(0)->fsId() == "cpm", "one CP/M mount");
        dw::fs::Volume *v = o.disc().volume(0);
        QVector<dw::fs::Entry> l;
        CHECK(v->list(Path(), &l, &err) && l.size() == 1 && l[0].name == "user 0" && l[0].kind == dw::fs::NodeKind::Namespace,
              "the root lists the user areas");
        CHECK(v->caps().namespaces && !v->caps().dirs && v->caps().maxEntriesPerDir == 64, "caps");
        CHECK(v->write(Path({"user 0"}), "GAME.COM", QByteArray(5000, 'g'), {{"readonly", true}}, QDateTime(), false, &err) &&
                  v->write(Path({"user 3"}), "OTHER.TXT", "other", {}, QDateTime(), false, &err), "write in user 0 and user 3: " + err);
        CHECK(v->list(Path(), &l, &err) && l.size() == 2 && l[1].name == "user 3", "user 3 appears");
        dw::fs::Entry e;
        CHECK(v->stat(Path({"user 0", "GAME.COM"}), &e, &err) && e.size == 5000 && e.attrs["readonly"].toBool(),
              QString("stat: R/O, %1 bytes").arg(e.size));
        QJsonObject c = v->toRetro(e)["cpm"];
        CHECK(c["readonly"].toBool() && !c.contains("f1"), "toRetro: the cpm family");
        dw::TransferItem t;
        t.name = "A.PROG"; t.sourceFs = "dfs";
        dw::fs::Placement pl = v->placeIncoming(t, Path());
        CHECK(pl.dir == Path({"user 0"}) && pl.name == "PROG", "a DFS file: its letter dropped, user 0");
        t.attrs["cpm"] = QJsonObject{{"user", 5}}; t.sourceFs = "cpm"; t.name = "x.txt";
        pl = v->placeIncoming(t, Path({"user 0"}));
        CHECK(pl.dir == Path({"user 5"}) && pl.name == "X.TXT", "a CP/M file keeps its user");
        CHECK(v->rename(Path({"user 3", "OTHER.TXT"}), Path({"user 4", "MOVED.TXT"}), &err) && v->stat(Path({"user 4", "MOVED.TXT"}), &e, &err),
              "rename into another user: " + err);
        CHECK(v->setAttrs(Path({"user 0", "GAME.COM"}), {{"system", true}}, &err) && v->stat(Path({"user 0", "GAME.COM"}), &e, &err) &&
                  e.attrs["system"].toBool() && e.attrs["readonly"].toBool(), "setAttrs: only system changes");
        const auto snap = o.container().snapshot();
        CHECK(!o.disc().transact([&](QString *x) {
                  return v->write(Path({"user 0"}), "A.TXT", "a", {}, QDateTime(), false, x) &&
                         v->write(Path({"user 0"}), "HUGE.DAT", QByteArray(400 * 1024, 'h'), {}, QDateTime(), false, x);
              }, &err), "a transaction that doesn't fit fails: " + err);
        CHECK(o.container().snapshot() == snap && !v->stat(Path({"user 0", "A.TXT"}), &e, &err), "and is put back");
        CHECK(o.disc().validateAll().ok() && o.save(&err), "checked save: " + err);
        Cpm::OpenedDisc o2;
        QByteArray d;
        CHECK(o2.open(p, &err) && o2.disc().volume(0)->read(Path({"user 0", "GAME.COM"}), &d, &err) && d.left(5000) == QByteArray(5000, 'g'),
              "read back: " + err);
        CHECK(o2.disc().volume(0)->remove(Path({"user 4"}), &err) && o2.disc().volume(0)->list(Path(), &l, &err) && l.size() == 1,
              "removing a user area removes its files");
        CHECK(Cpm::verdict(o2.container(), p, o2.detection()).action == dw::fs::Verdict::Open, "detection by tiers: open here");
    }

    printf("Torch CP/N\n");
    {
        const QString p = W + "/torch.img";
        err.clear();
        CHECK(blank(p, "torch-cpn", dw::device::Kind::Raw, &err), "blank Torch disc: " + err);
        Cpm::OpenedDisc o;
        CHECK(o.open(p, &err), "open by detection: " + err);
        if (o.disc().mounts().isEmpty()) { printf("\n%d FAILED\n", ++fails); return 1; }
        dw::fs::Volume *v = o.disc().volume(0);
        CHECK(v->fsId() == "torch", "fsId torch");
        CHECK(v->write(Path({"user 0"}), "HELLO.TXT", "hello\x1a", {}, QDateTime(), false, &err) && o.save(&err), "write and save: " + err);
        Cpm::OpenedDisc o2;
        QByteArray d;
        CHECK(o2.open(p, &err) && o2.disc().volume(0)->fsId() == "torch" && o2.disc().volume(0)->read(Path({"user 0", "HELLO.TXT"}), &d, &err) &&
                  d.startsWith("hello"), "detected and read back: " + err);
    }

    printf("Torch CP/N: TorchVolume over Torch::Fs, and Torch::Fs used directly (through openStore) gives the same bytes\n");
    {
        const QString pa = W + "/torch-store.imd", pb = W + "/torch-volume.imd";
        err.clear();
        CHECK(blank(pa, "torch-cpn", dw::device::Kind::Imd, &err) && blank(pb, "torch-cpn", dw::device::Kind::Imd, &err),
              "two blank Torch discs: " + err);
        QByteArray big;
        for (int i = 0; i < 70 * 1024; ++i) big += char(i * 7 + i / 251);          // over 128 sectors: an L2 index
        const QByteArray one("one\r\n\x1a"), two(300, 't'), reuse("again\x1a");
        // through the store (Cpm::openStore: what the window and the tools use for a disc that is not read as a Volume)
        {
            dw::device::Container img;
            Cpm::Format f = *Cpm::findFormat("torch-cpn");
            std::unique_ptr<Cpm::Store> sp;
            const bool attached = img.open(pa, &err) && f.derive(&err) && (sp = Cpm::openStore(&img, f, &err)) != nullptr;
            CHECK(attached && dynamic_cast<Torch::Fs *>(sp.get()), "store attached, a Torch::Fs: " + err);
            Cpm::Store &v = *sp;
            CHECK(attached && v.addFile(0, "ONE.TXT", one, 0, QDateTime(), false, &err) && v.addFile(3, "BIG.DAT", big, 0, QDateTime(), false, &err) &&
                      v.addFile(0, "TWO.COM", two, 0, QDateTime(), false, &err) && v.deleteFile(0, "ONE.TXT", &err) &&
                      v.addFile(0, "REUSE.TXT", reuse, 0, QDateTime(), false, &err) &&
                      v.rename(3, "BIG.DAT", 5, "MOVED.DAT", &err) &&
                      v.setAttrs(0, "TWO.COM", Cpm::AttrRO | Cpm::AttrSYS | Cpm::AttrF2, &err) &&
                      v.addFile(0, "TWO.COM", "x", 0, QDateTime(), true, &err) == false && img.commit(&err, false),
                  "store: add, delete, add (reusing the entry), rename to another user, attributes, a read-only file not replaced: " + err);
        }
        // through the adapter
        Cpm::OpenedDisc o;
        CHECK(o.open(pb, &err) && o.disc().mounts().size() == 1, "opened by detection: " + err);
        if (o.disc().mounts().isEmpty()) { printf("\n%d FAILED\n", ++fails); return 1; }
        dw::fs::Volume *v = o.disc().volume(0);
        auto *tv = dynamic_cast<Cpm::TorchVolume *>(v);
        CHECK(tv && tv->fsId() == "torch" && dynamic_cast<Torch::Fs *>(tv->cpm()) == tv->torch(), "a TorchVolume over a Torch::Fs, fsId torch");
        CHECK(v->caps().maxEntriesPerDir == 256 && !v->caps().title && !v->caps().dates && v->caps().namespaces,
              "caps: 256 entries, no title, no dates, user areas");
        CHECK(v->volumeAttrs().isEmpty() && v->volumeValues().isEmpty(), "no volume attributes");
        err.clear();
        CHECK(!v->setVolumeAttrs({{"label", "NAME"}}, &err) && err == "Torch CP/N discs have no label", "setVolumeAttrs refused: " + err);
        const dw::fs::VolumeInfo in = v->info();
        CHECK(in.total == 1600 * 256 && in.free == qint64(1600 - 20) * 256 && in.entriesFree == 256,
              QString("info in 256-byte sectors: total %1, free %2, %3 entries free").arg(in.total).arg(in.free).arg(in.entriesFree));
        err.clear();
        CHECK(v->write(Path({"user 0"}), "ONE.TXT", one, {}, QDateTime(), false, &err) &&
                  v->write(Path({"user 3"}), "BIG.DAT", big, {}, QDateTime(), false, &err) &&
                  v->write(Path({"user 0"}), "TWO.COM", two, {}, QDateTime(), false, &err) &&
                  v->remove(Path({"user 0", "ONE.TXT"}), &err) &&
                  v->write(Path({"user 0"}), "REUSE.TXT", reuse, {}, QDateTime(), false, &err) &&
                  v->rename(Path({"user 3", "BIG.DAT"}), Path({"user 5", "MOVED.DAT"}), &err) &&
                  v->setAttrs(Path({"user 0", "TWO.COM"}), {{"readonly", true}, {"system", true}, {"f2", true}}, &err) &&
                  !v->write(Path({"user 0"}), "TWO.COM", "x", {}, QDateTime(), true, &err) && o.disc().validateAll().ok() && o.save(&err),
              "TorchVolume: the same sequence, then a checked save: " + err);
        QFile a(pa), b(pb);
        CHECK(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly) && a.size() > 0 && a.readAll() == b.readAll(),
              QString("the two images are byte-identical (%1 bytes)").arg(a.size()));
        Cpm::OpenedDisc o2;
        QByteArray d;
        dw::fs::Entry e;
        CHECK(o2.open(pb, &err) && o2.disc().volume(0)->read(Path({"user 5", "MOVED.DAT"}), &d, &err) && d.left(big.size()) == big &&
                  o2.disc().volume(0)->stat(Path({"user 0", "TWO.COM"}), &e, &err) && e.attrs["readonly"].toBool() &&
                  e.attrs["system"].toBool() && e.attrs["f2"].toBool() && !e.attrs["archive"].toBool(),
              "reopened: the L2 file reads back, the attributes are kept: " + err);
        const Cpm::Report r = static_cast<Cpm::CpmVolume *>(o2.disc().volume(0))->cpm()->check();
        CHECK(r.ok() && r.warnings.isEmpty(), "Torch::Fs check: clean, map and directory agree");
    }

    // Torch's MCP (v1.22) stores a name's bytes as the program gives them and finds a file by its exact bytes (bit 7,
    // the attributes, aside), so readme.txt and README.TXT are two files; only the command line is upper-cased.
    printf("Torch CP/N: names kept and found as the MCP keeps them\n");
    {
        const QString p = W + "/torch-case.img";
        err.clear();
        CHECK(blank(p, "torch-cpn", dw::device::Kind::Raw, &err), "blank Torch disc: " + err);
        Cpm::OpenedDisc o;
        auto *tv = o.open(p, &err) ? dynamic_cast<Cpm::TorchVolume *>(o.disc().volume(0)) : nullptr;
        CHECK(tv, "opened as a TorchVolume: " + err);
        if (!tv) { printf("\n%d FAILED\n", ++fails); return 1; }
        Cpm::Store *st = tv->cpm();
        tv->setCharset("ascii", true);                    // a disc that takes lower-case names
        QByteArray d;
        CHECK(st->addFile(0, "readme.txt", "one\x1a", 0, QDateTime(), false, &err) && st->indexOf(0, "readme.txt") >= 0 &&
                  st->files()[st->indexOf(0, "readme.txt")].name == "readme.txt" && st->indexOf(0, "README.TXT") < 0,
              "a lower-case name is kept as given, and README.TXT is not it: " + err);
        CHECK(st->addFile(0, "README.TXT", "two\x1a", 0, QDateTime(), false, &err) && st->files().size() == 2,
              "README.TXT beside it is another file: " + err);
        err.clear();
        CHECK(!st->addFile(0, "readme.txt", "three\x1a", 0, QDateTime(), false, &err) && err == "readme.txt already exists in user 0",
              "readme.txt a second time is a file that exists: " + err);
        CHECK(st->addFile(0, "readme.txt", "three\x1a", 0, QDateTime(), true, &err) && st->files().size() == 2 &&
                  st->readFile(st->files()[st->indexOf(0, "readme.txt")], &d, &err) && d.startsWith("three") &&
                  st->readFile(st->files()[st->indexOf(0, "README.TXT")], &d, &err) && d.startsWith("two"),
              "... and replacing it replaces that file, not README.TXT: " + err);
        CHECK(st->setAttrs(0, "readme.txt", Cpm::AttrSYS, &err) && st->rename(0, "readme.txt", 2, "notes.txt", &err) &&
                  st->indexOf(2, "notes.txt") >= 0 && (st->files()[st->indexOf(2, "notes.txt")].attrs & Cpm::AttrSYS) &&
                  st->deleteFile(2, "notes.txt", &err) && st->indexOf(0, "README.TXT") >= 0 && st->files().size() == 1,
              "attributes, rename (keeping the case) and delete find the lower-case name by its own bytes: " + err);
        err.clear();
        CHECK(!st->deleteFile(0, "readme.txt", &err), "deleting readme.txt doesn't take README.TXT");
        tv->setCharset("ascii", false);                   // a disc that doesn't take lower-case names: upper-cased, as before
        CHECK(st->addFile(0, "lower.txt", "x\x1a", 0, QDateTime(), false, &err) && st->indexOf(0, "LOWER.TXT") >= 0 && st->indexOf(0, "lower.txt") < 0,
              "without lower-case names a name is upper-cased: " + err);
        CHECK(o.save(&err), "checked save: " + err);
        Cpm::OpenedDisc o2;
        CHECK(o2.open(p, &err) && o2.disc().volume(0)->read(Path({"user 0", "README.TXT"}), &d, &err) && d.startsWith("two"),
              "reopened: " + err);
        const Cpm::Report r = static_cast<Cpm::CpmVolume *>(o2.disc().volume(0))->cpm()->check();
        CHECK(r.ok() && r.warnings.isEmpty(), "Torch::Fs check: clean");
    }

    // Step 6b: Cpm::Volume and Torch::Fs keep no snapshot of their own. A change that fails half way leaves what it
    // staged; dw::fs::Disc::transact puts it back and has the volume reload, and reload() must give exactly the state
    // the changes left in memory (a second Store attached to the same staged container is that reload).
    printf("Step 6b: Disc::transact is the only rollback, and reload() is faithful\n");
    err.clear();
    for (const char *name : {"ibm-3740", "pcw", "torch-cpn"}) {
        printf(" %s\n", name);
        const QString p = W + "/staging-" + name + ".img";
        const Cpm::Format *fp = Cpm::findFormat(name);
        CHECK(fp, QString("format ") + name);
        if (!fp) continue;
        Cpm::Format f = *fp;
        CHECK(f.derive(&err), "derive: " + err);
        const bool stamps = f.os == Cpm::Os::V3;
        {
            dw::device::Container img;
            CHECK(Cpm::makeBlank(&img, dw::device::Kind::Raw, f, stamps, stamps ? "SIXB" : QString(), &err) &&
                      img.saveAs(p, dw::device::Kind::Raw, &err), "blank disc: " + err);
        }
        Cpm::OpenedDisc o;
        CHECK(o.open(p, &err) && o.disc().mounts().size() == 1, "opened: " + err);
        if (o.disc().mounts().size() != 1) continue;
        Cpm::Store *st = static_cast<Cpm::CpmVolume *>(o.disc().volume(0))->cpm();
        auto faithful = [&](const QString &after) {
            QString e;
            std::unique_ptr<Cpm::Store> fresh = Cpm::openStore(&o.container(), st->format(), &e);
            const bool same = fresh && describe(*fresh) == describe(*st);
            CHECK(same, "reload() gives what " + after + " left in memory" + (fresh ? QString() : ": " + e));
        };
        const QDateTime when(QDate(1986, 5, 4), QTime(12, 34, 0));
        CHECK(st->addFile(0, "ONE.TXT", QByteArray(3000, '1'), 0, when, false, &err), "add ONE.TXT: " + err);
        faithful("an add");
        CHECK(st->addFile(3, "TWO.COM", QByteArray(9000, '2'), Cpm::AttrSYS, when, false, &err), "add TWO.COM in user 3: " + err);
        CHECK(st->addFile(0, "EMPTY", QByteArray(), 0, when, false, &err), "add an empty file: " + err);
        faithful("adds in two users");
        CHECK(st->addFile(0, "ONE.TXT", QByteArray(5000, 'o'), 0, when, true, &err), "replace ONE.TXT: " + err);
        faithful("a replace");
        CHECK(st->rename(3, "TWO.COM", 5, "MOVED.COM", &err) && st->setAttrs(5, "MOVED.COM", Cpm::AttrRO | Cpm::AttrF2, &err),
              "rename into user 5, set attributes: " + err);
        faithful("a rename and attributes");
        CHECK(st->deleteFile(0, "EMPTY", &err), "delete: " + err);
        faithful("a delete");
        if (st->labelAllowed()) {
            CHECK(st->setLabel("NEWNAME", &err), "label: " + err);
            faithful("a label");
        }
        // A replace that doesn't fit: the old file is deleted first, then the disc is full.
        const auto snap = o.container().snapshot();
        const QString before = describe(*st);
        QByteArray old;
        const int ix = st->indexOf(0, "ONE.TXT");
        CHECK(ix >= 0 && st->readFile(st->files()[ix], &old, &err), "ONE.TXT read: " + err);
        const QByteArray huge(int(st->freeBytes() + 64 * 1024), 'h');
        CHECK(!o.disc().transact([&](QString *e) { return st->addFile(0, "ONE.TXT", huge, 0, when, true, e); }, &err),
              "a replace that doesn't fit fails: " + err);
        QByteArray now;
        const int ix2 = st->indexOf(0, "ONE.TXT");
        CHECK(o.container().snapshot() == snap && describe(*st) == before && ix2 >= 0 && st->readFile(st->files()[ix2], &now, &err) &&
                  now == old, "the transaction put everything back: the staging, the directory in memory and ONE.TXT");
        // Without a transaction the class now leaves its half-done change (the contract: changes go inside one).
        QString e2;
        CHECK(!st->addFile(0, "ONE.TXT", huge, 0, when, true, &e2) && !(o.container().snapshot() == snap),
              "without a transaction, the old file's deletion stays staged (" + e2 + ")");
        o.container().restore(snap);
        CHECK(st->reload(&err) && describe(*st) == before, "put back by hand and reloaded: " + err);
        // A failure in the middle of several changes, as Delete or Properties make them.
        CHECK(!o.disc().transact([&](QString *e) {
                  return st->deleteFile(5, "MOVED.COM", e) && st->addFile(1, "NEW.TXT", "new", 0, when, false, e) &&
                         st->rename(0, "NOSUCH.TXT", 0, "X.TXT", e);
              }, &err) && o.container().snapshot() == snap && describe(*st) == before, "a delete, an add, then a failure: all put back: " + err);
        CHECK(st->check().ok() && o.disc().validateAll().ok() && o.save(&err), "checked and saved: " + err);
        dw::device::Container saved;
        std::unique_ptr<Cpm::Store> back;
        if (saved.open(p, &err) && saved.setRawLayout(o.container().rawLayout(), &err)) back = Cpm::openStore(&saved, f, &err);
        CHECK(back && describe(*back) == before, "the saved disc reads back the same: " + err);
    }

    printf("Acorn Disk Wrangler's discs are handed to it\n");
    {
        QByteArray dfs(800 * 256, char(0xE5));             // a DFS disc with one file
        dfs[0] = 'G'; dfs[1] = 'A'; dfs[2] = 'M'; dfs[3] = 'E'; dfs[4] = 'S';
        dfs.replace(8, 8, QByteArray("ELITE  $"));
        QByteArray s1(256, 0);
        s1[5] = 8; s1[6] = 3; s1[7] = char(0x20);          // one file, 800 sectors
        s1[8] = 0; s1[9] = 0x19; s1[10] = 0x23; s1[11] = char(0x80); s1[12] = 0; s1[13] = 0x10; s1[14] = 0; s1[15] = 2;
        dfs.replace(256, 256, s1);
        spit(W + "/game.ssd", dfs);
        dw::fs::Verdict v = verdictOf(W + "/game.ssd");
        CHECK(v.action == dw::fs::Verdict::Handoff && v.bestCandidate()->owner.id == "acorndiskwrangler" &&
                  v.bestCandidate()->description.contains("DFS"), "a DFS disc: " + (v.bestCandidate() ? v.bestCandidate()->description : QString("nothing")));
        QByteArray pc(737280, 0);                          // a PC 720K floppy (boot record with a BPB)
        const unsigned char bpb[] = {0xEB, 0x3C, 0x90, 'M', 'S', 'D', 'O', 'S', '5', '.', '0', 0x00, 0x02, 2, 1, 0, 2, 112, 0, 0xA0, 0x05, 0xF9, 3, 0, 9, 0, 2, 0};
        pc.replace(0, int(sizeof(bpb)), QByteArray(reinterpret_cast<const char *>(bpb), int(sizeof(bpb))));
        pc[510] = char(0x55); pc[511] = char(0xAA);
        for (int fat : {1, 4}) { pc[fat * 512] = char(0xF9); pc[fat * 512 + 1] = char(0xFF); pc[fat * 512 + 2] = char(0xFF); }
        spit(W + "/pc.img", pc);
        v = verdictOf(W + "/pc.img");
        CHECK(v.action == dw::fs::Verdict::Handoff && v.bestCandidate()->owner.id == "acorndiskwrangler", "a PC FAT disc");
        QByteArray z80(800 * 256, char(0xE5));             // Acorn Z80 CP/M: its DFS catalogue only fences off CP/M's data
        z80.replace(0, 512, Cpm::acornDfsCatalogue(400));
        CHECK(Cpm::isAcornDfsCatalogue(z80) && dw::dfs::isCpmFence(z80) && Cpm::acornDfsCatalogue(800) == dw::dfs::cpmFenceCatalogue(800),
              "Acorn Z80's catalogue is the DFS module's");
        spit(W + "/junk.img", QByteArray(100000, 'j'));
        CHECK(verdictOf(W + "/junk.img").action == dw::fs::Verdict::None, "junk: nothing");
    }

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails ? 1 : 0;
}
