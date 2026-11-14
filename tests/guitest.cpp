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
// guitest <workdir> [<osborne single-density .IMD> [<TeleDisk .TD0>]] [--adw <dir made by interop_adw make>]
// Runs offscreen (QT_QPA_PLATFORM=offscreen). Makes new discs, adds host files (with a userN folder), drags
// between windows with real drag events, drags out (retro-files JSON + files), drops JSON made by Acorn Disk
// Wrangler, changes attributes/users, saves, and checks the read-only rules. Leaves <workdir>/out.json (a drag
// out of this program) for interop_adw to read, and screenshots in <workdir>/shots.
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QMenuBar>
#include <QTest>
#include <QToolBar>
#include <QDir>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QFormLayout>
#include <QTextBrowser>
#include <QMenu>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QScrollBar>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QScreen>
#include <QMessageBox>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include "../src/formatdialog.h"
#include "../src/newdisc.h"
#include <QCheckBox>
#include <diskwrangler/app/fileopen.h>
#include <diskwrangler/xfer/mime.h>
#include "../src/mainwindow.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", qPrintable(QString(msg))); ++failures; } else printf("  ok: %s\n", qPrintable(QString(msg))); } while (0)

static void pump() { for (int i = 0; i < 5; ++i) { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(); } }

static QTreeWidgetItem *findRow(QTreeWidget *t, int kind, int user, const QString &name = QString()) {
    QList<QTreeWidgetItem *> stack;
    for (int i = 0; i < t->topLevelItemCount(); ++i) stack << t->topLevelItem(i);
    while (!stack.isEmpty()) {
        QTreeWidgetItem *it = stack.takeFirst();
        if (it->data(0, RoleKind).toInt() == kind && (kind == RowRoot || it->data(0, RoleUser).toInt() == user) &&
            (name.isEmpty() || it->data(0, RoleName).toString() == name))
            return it;
        for (int i = 0; i < it->childCount(); ++i) stack << it->child(i);
    }
    return nullptr;
}

// a real drag-and-drop sequence onto a tree row (Qt ignores a lone drop event)
static void dropOn(QTreeWidget *t, QTreeWidgetItem *target, QMimeData *m) {
    t->scrollToItem(target);
    QPoint p = t->visualItemRect(target).center();
    QDragEnterEvent enter(p, Qt::CopyAction, m, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(t->viewport(), &enter);
    QDragMoveEvent move(p, Qt::CopyAction, m, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(t->viewport(), &move);
    QDropEvent drop(p, Qt::CopyAction, m, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(t->viewport(), &drop);
    pump();
}

// The disc's filesystem on a side (through the kit's document), and the open CP/M windows (the kit's list).
static Cpm::Store &V(MainWindow &w, int side = 0) { return *w.document()->store(side); }
static QList<MainWindow *> openWindows() {
    QList<MainWindow *> l;
    for (dw::ui::DiscWindow *w : dw::ui::DiscWindow::all()) if (auto *m = qobject_cast<MainWindow *>(w)) l << m;
    return l;
}

// The window's tree (its one pane).
static DropTree *Tree(MainWindow &w) { return static_cast<DropTree *>(w.pane()); }

// A file of a disc as the kit names it.
static dw::ui::DiscWindow::Item ItemOf(int side, int user, const QString &name) {
    return dw::ui::DiscWindow::Item(side, dw::fs::Path({Cpm::CpmVolume::userName(user), name}));
}

static QByteArray fileData(Cpm::Store &v, int user, const QString &name) {
    int i = v.indexOf(user, name);
    if (i < 0) return QByteArray("<missing>");
    QByteArray d;
    QString err;
    v.readFile(v.files()[i], &d, &err);
    return d;
}

static void shot(QWidget *w, const QString &path) {
    w->resize(w->inherits("QDialog") ? 1120 : 1000, 640);
    pump();
    w->grab().save(path);
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("cpmdiskwrangler-test");
    QStringList a = app.arguments().mid(1);
    QString adwDir, rmImage;
    int ai = a.indexOf("--adw");
    if (ai >= 0 && ai + 1 < a.size()) { adwDir = a[ai + 1]; a.removeAt(ai); a.removeAt(ai); }
    ai = a.indexOf("--rm");
    if (ai >= 0 && ai + 1 < a.size()) { rmImage = a[ai + 1]; a.removeAt(ai); a.removeAt(ai); }
    if (a.isEmpty()) { printf("usage: guitest <workdir> [osborne.imd [teledisk.td0]] [--adw dir]\n"); return 2; }
    QString wd = a[0];
    QDir(wd).removeRecursively();
    QDir().mkpath(wd + "/shots");
    MainWindow::autoAnswer = true;
    QString err;

    printf("== new discs\n");
    MainWindow A, B;
    A.show(); B.show();
    CHECK(A.newImage(wd + "/a.dsk", dw::device::Kind::Edsk, *Cpm::findFormat("pcw"), true, "DISCA"), "new PCW disc (EDSK, CP/M 3 stamps)");
    CHECK(B.newImage(wd + "/b.imd", dw::device::Kind::Imd, *Cpm::findFormat("ibm-3740"), false, QString()), "new IBM 3740 disc (IMD)");
    CHECK(A.writable(0) && B.writable(0), "new discs are writable");
    CHECK(V(A).label().name == "DISCA", "label on the new CP/M 3 disc");

    printf("== new Research Machines double-sided disc: a separate filesystem on each side\n");
    {
        NewDiscDialog nd;
        QCheckBox *two = nullptr;
        for (QCheckBox *c : nd.findChildren<QCheckBox *>()) if (c->text().startsWith("Double-sided")) two = c;
        CHECK(two, "the new-disc dialog has a double-sided box");
        if (two) {
            CHECK(nd.selectFormat("ibm-3740") && !two->isEnabled(), "not offered for a format with no double-sided disc");
            CHECK(nd.selectFormat("rm-qd") && two->isEnabled() && !two->isChecked(), "offered for rm-qd, unticked");
            CHECK(!nd.format().separateSides && nd.format().physHeads == 1, "unticked: a single-sided disc");
            two->setChecked(true);
            Cpm::Format g = nd.format();
            CHECK(g.separateSides && g.physHeads == 2 && g.cylinders == 80, "ticked: two sides of 80 cylinders, a disc on each");
            nd.selectFormat("ibm-3740");
            CHECK(!two->isChecked(), "choosing a format with no double-sided disc unticks it");
        }
        Cpm::Format ds = *Cpm::findFormat("rm-qd");
        ds.separateSides = true; ds.heads = 2; ds.derived = false;
        CHECK(ds.derive(&err), err);
        for (dw::device::Kind k : {dw::device::Kind::Imd, dw::device::Kind::Raw}) {
            const QString p = wd + (k == dw::device::Kind::Imd ? "/rmds.imd" : "/rmds.img");
            MainWindow R;
            R.show();
            CHECK(R.newImage(p, k, ds, false, QString()), "new double-sided rm-qd disc");
            CHECK(R.document()->sides() == 2 && R.writable(0), "two filesystems, writable");
            QFile::remove(wd + "/side1.txt");
            QFile f1(wd + "/side1.txt"); f1.open(QIODevice::WriteOnly); f1.write("on the second side\r\n"); f1.close();
            CHECK(V(R, 0).files().isEmpty() && V(R, 1).files().isEmpty(), "both sides empty");
            CHECK(V(R, 1).addFile(0, "SIDE1.TXT", "on the second side\r\n", 0, QDateTime(), false, &err), "a file on side 1: " + err);
            CHECK(V(R, 0).files().isEmpty() && V(R, 1).files().size() == 1, "side 0 untouched by it");
            CHECK(R.save(), "save");
            MainWindow R2;
            CHECK(R2.openFile(p), "reopen it");
            CHECK(R2.document()->sides() == 2 && V(R2, 0).files().isEmpty() && V(R2, 1).files().size() == 1, "reopened: a disc on each side, the file on side 1");
        }
    }

    printf("== host files and folders\n");
    QDir().mkpath(wd + "/host/user3");
    QDir().mkpath(wd + "/host/misc");
    auto put = [&](const QString &p, const QByteArray &d) { QFile f(wd + "/host/" + p); f.open(QIODevice::WriteOnly); f.write(d); };
    put("readme.txt", "Read me first\r\n");
    put("A very long name.document", QByteArray(5000, 'x'));
    put("user3/game.com", QByteArray::fromHex("c30001") + QByteArray(2000, '\0'));
    put("misc/notes.txt", "notes\r\n");
    put("prog.inf", "PROG FFFF1900 FFFF8023\n");        // an Acorn sidecar: must be ignored
    A.importPaths({wd + "/host/readme.txt", wd + "/host/A very long name.document", wd + "/host/user3",
                   wd + "/host/misc", wd + "/host/prog.inf"}, -1);
    Cpm::Store &va = V(A);
    CHECK(va.indexOf(0, "README.TXT") >= 0, "readme.txt -> 0:README.TXT");
    CHECK(va.indexOf(0, "AVERYLON.DOC") >= 0, "a long host name becomes AVERYLON.DOC");
    CHECK(va.indexOf(3, "GAME.COM") >= 0, "a host folder called user3 puts its files in user 3");
    CHECK(va.indexOf(0, "NOTES.TXT") >= 0, "other folders are flattened into the target user");
    CHECK(va.indexOf(0, "PROG.INF") < 0, ".inf sidecars are not copied");
    CHECK(fileData(va, 0, "README.TXT") == "Read me first\r\n", "README.TXT content (exact size kept)");
    int i = va.indexOf(0, "README.TXT");
    CHECK(i >= 0 && va.files()[i].updated.isValid(), "CP/M 3 update stamp set on an added file");
    // drop onto the user 3 group
    put("drop.txt", "dropped\r\n");
    auto *m = new QMimeData;
    m->setUrls({QUrl::fromLocalFile(wd + "/host/drop.txt")});
    dropOn(Tree(A), findRow(Tree(A), RowUser, 3), m);
    pump();
    CHECK(va.indexOf(3, "DROP.TXT") >= 0, "a file dropped on the User 3 row lands in user 3");
    delete m;

    printf("== attributes and users\n");
    CHECK(va.setAttrs(3, "GAME.COM", Cpm::AttrRO | Cpm::AttrSYS, &err), "set R/O SYS on GAME.COM");
    CHECK(va.rename(0, "NOTES.TXT", 7, "NOTES.TXT", &err), "move NOTES.TXT to user 7");
    A.refresh();
    QString e2;
    CHECK(!A.deleteFiles({ItemOf(0, 3, "GAME.COM")}, &e2), "deleting a read-only file is refused: " + e2);
    CHECK(A.save(), "save disc A");
    CHECK(!A.document()->container().hasPending(), "nothing pending after the save");

    printf("== dragging between windows (real drag events)\n");
    Tree(A)->clearSelection();
    findRow(Tree(A), RowUser, 3)->setSelected(true);                 // the whole of user 3
    findRow(Tree(A), RowFile, 0, "README.TXT")->setSelected(true);
    QMimeData *dm = Tree(A)->makeMime(Tree(A)->selectedItems());
    CHECK(dm && DropTree::fromThisProcess(dm), "the drag carries this program's private format");
    dropOn(Tree(B), findRow(Tree(B), RowRoot, -1), dm);             // onto the root: users are kept
    pump();
    Cpm::Store &vb = V(B);
    CHECK(vb.indexOf(3, "GAME.COM") >= 0 && vb.indexOf(3, "DROP.TXT") >= 0 && vb.indexOf(0, "README.TXT") >= 0,
          "files copied with their user numbers");
    int g = vb.indexOf(3, "GAME.COM");
    CHECK(g >= 0 && (vb.files()[g].attrs & (Cpm::AttrRO | Cpm::AttrSYS)) == (Cpm::AttrRO | Cpm::AttrSYS), "attributes copied");
    CHECK(fileData(vb, 3, "GAME.COM") == fileData(va, 3, "GAME.COM"), "content copied exactly");
    CHECK(fileData(vb, 0, "README.TXT") == "Read me first\r\n", "exact size survives a CP/M 2.2 disc too");
    delete dm;
    // dropping again onto user 5 puts copies in user 5
    Tree(A)->clearSelection();
    findRow(Tree(A), RowFile, 0, "README.TXT")->setSelected(true);
    dm = Tree(A)->makeMime(Tree(A)->selectedItems());
    findRow(Tree(B), RowRoot, -1)->setExpanded(true);
    dropOn(Tree(B), findRow(Tree(B), RowUser, 3), dm);
    pump();
    CHECK(vb.indexOf(3, "README.TXT") >= 0, "a drop on the User 3 row overrides the source's user");
    delete dm;

    printf("== dragging out: files and retro-files JSON\n");
    Tree(A)->clearSelection();
    findRow(Tree(A), RowRoot, -1)->setSelected(true);                 // everything
    QMimeData *out = Tree(A)->makeMime(Tree(A)->selectedItems());
    QByteArray json = out->data(RetroFiles::MimeType);
    QList<QUrl> urls = out->urls();
    QJsonObject o = QJsonDocument::fromJson(json).object();
    CHECK(o["format"].toString() == "retro-files" && o["version"].toInt() == 1, "retro-files version 1 document");
    CHECK(o["source"].toObject()["filesystem"].toString() == "cpm", "source filesystem is cpm");
    CHECK(o["generator"].toString() == "CP/M Disk Wrangler", "generator");
    int nItems = o["items"].toArray().size();
    CHECK(nItems == va.files().size(), QString("one item per file (%1)").arg(nItems));
    CHECK(urls.size() == va.files().size(), "text/uri-list lists the files");
    bool userOk = false, sizeOk = true;
    for (const QJsonValue &v : o["items"].toArray()) {
        QJsonObject it = v.toObject();
        QJsonObject c = it["attributes"].toObject()["cpm"].toObject();
        if (it["name"].toString() == "GAME.COM")
            userOk = c["user"].toInt() == 3 && c["readonly"].toBool() && c["system"].toBool() && !c["archive"].toBool();
        QFile f(QUrl(o["base"].toString()).toLocalFile() + "/" + it["path"].toString());
        if (f.size() != qint64(it["size"].toDouble())) sizeOk = false;
    }
    CHECK(userOk, "GAME.COM carries user 3, readonly and system in the cpm namespace");
    CHECK(sizeOk, "every listed file exists at its stated size");
    // the drag's folder goes when this program exits (as the format says): keep a copy for interop_adw
    QString keep = wd + "/outfiles";
    QDir().mkpath(keep);
    QString base = QUrl(o["base"].toString()).toLocalFile();
    for (const QString &n : QDir(base).entryList(QDir::Files)) QFile::copy(base + "/" + n, keep + "/" + n);
    o["base"] = QUrl::fromLocalFile(keep).toString();
    QFile jf(wd + "/out.json");
    jf.open(QIODevice::WriteOnly);
    jf.write(QJsonDocument(o).toJson());
    jf.close();
    delete out;

    printf("== a retro-files drop from another program (not this process)\n");
    MainWindow C;
    C.show();
    CHECK(C.newImage(wd + "/c.img", dw::device::Kind::Raw, *Cpm::findFormat("kpiv"), false, QString()), "new Kaypro 4 raw disc");
    auto *rm = new QMimeData;
    rm->setData(RetroFiles::MimeType, json);
    dropOn(Tree(C), findRow(Tree(C), RowRoot, -1), rm);
    pump();
    CHECK(V(C).indexOf(3, "GAME.COM") >= 0 && V(C).indexOf(7, "NOTES.TXT") >= 0, "JSON drop keeps user numbers");
    delete rm;
    if (!adwDir.isEmpty()) {
        printf("== drops made by Acorn Disk Wrangler's own code\n");
        for (const QString &n : {"adfs", "dfs", "fat"}) {
            QFile f(adwDir + "/" + n + ".json");
            CHECK(f.open(QIODevice::ReadOnly) && C.importRetroFiles(f.readAll(), -1), "import " + n + ".json");
        }
        Cpm::Store &vc = V(C);
        int e = vc.indexOf(0, "ELITE");
        CHECK(e >= 0 && (vc.files()[e].attrs & Cpm::AttrRO), "ADFS Elite (locked) -> 0:ELITE, read-only");
        CHECK(vc.indexOf(0, "README") >= 0, "ADFS directory Games flattened: README");
        CHECK(vc.indexOf(0, "BOOT") >= 0 && vc.indexOf(0, "PROG") >= 0, "DFS $.BOOT and A.PROG lose their directory letters");
        int p = vc.indexOf(0, "PROG");
        CHECK(p >= 0 && (vc.files()[p].attrs & Cpm::AttrRO), "locked DFS file -> read-only");
        int ae = vc.indexOf(0, "AUTOEXEC.BAT");
        CHECK(ae >= 0 && (vc.files()[ae].attrs & (Cpm::AttrRO | Cpm::AttrSYS | Cpm::AttrARC)) == (Cpm::AttrRO | Cpm::AttrSYS | Cpm::AttrARC),
              "DOS R S A -> CP/M R/O SYS ARC");
        CHECK(fileData(vc, 0, "AUTOEXEC.BAT") == "ECHO HI\r\n", "DOS file content and exact size");
    }
    CHECK(C.save(), "save the Kaypro disc");

    printf("== format dialog on a disc made here and filled since (Acorn Z80: side 1 runs back)\n");
    {
        MainWindow Z;
        CHECK(Z.newImage(wd + "/z.img", dw::device::Kind::Raw, *Cpm::findFormat("acorn-z80"), false, QString()), "new Acorn Z80 raw disc");
        Z.importPaths({wd + "/host/readme.txt"}, -1);
        int n = V(Z).files().size();
        CHECK(n > 0 && Z.save(), "add a file and save");
        CHECK(Z.detection().ranked.first().files == 0, "the reading was scored when the disc was empty");
        Z.rescoreReadings();
        CHECK(Z.detection().ranked.first().files == n, QString("scored again it lists the files (%1 of %2)").arg(Z.detection().ranked.first().files).arg(n));
        FormatDialog fd(&Z.document()->container(), Z.detection(), 0, true, &Z);
        {   // "How scoring works": the help covers the search, the tie-breaks and the Sharp inversion
            QPushButton *help = nullptr;
            for (QPushButton *b : fd.findChildren<QPushButton *>()) if (b->text().contains("How scoring works")) help = b;
            CHECK(help, "the format dialog has a How scoring works button");
            QString text;
            QTimer t;
            QObject::connect(&t, &QTimer::timeout, [&] {
                QWidget *m = QApplication::activeModalWidget();
                if (m && m != &fd) {
                    if (auto *tb = m->findChild<QTextBrowser *>()) {
                        text = tb->toPlainText();
                        m->resize(640, 900);
                        pump();
                        m->grab().save(wd + "/shots/scoring-help.png");
                    }
                    m->close();
                }
            });
            t.start(100);
            if (help) help->click();
            t.stop();
            for (const char *want : {"Where the layouts come from", "Readings that are otherwise equal", "outback", "inverted", "never-written"})
                CHECK(text.contains(want), QString("the scoring help mentions: %1").arg(want));
        }
        QSpinBox *heads = nullptr;
        QComboBox *order = nullptr;
        QPushButton *tryBtn = nullptr;
        for (QFormLayout *fl : fd.findChildren<QFormLayout *>())
            for (int r = 0; r < fl->rowCount(); ++r) {
                QLayoutItem *li = fl->itemAt(r, QFormLayout::LabelRole), *fi = fl->itemAt(r, QFormLayout::FieldRole);
                auto *lab = li ? qobject_cast<QLabel *>(li->widget()) : nullptr;
                if (lab && lab->text() == "Sides" && fi && fi->layout()) {
                    heads = qobject_cast<QSpinBox *>(fi->layout()->itemAt(0)->widget());
                    order = qobject_cast<QComboBox *>(fi->layout()->itemAt(2)->widget());
                }
            }
        for (QPushButton *b : fd.findChildren<QPushButton *>()) if (b->text() == "Try these settings") tryBtn = b;
        CHECK(heads && order && tryBtn, "found the hand-made format form");
        if (heads && order && tryBtn) {
            heads->setValue(1);
            tryBtn->click();
            CHECK(fd.chosen().files == 0 && order->currentText() == "outback", "one side: nothing read, the side order shown is kept");
            heads->setValue(2);
            tryBtn->click();
            CHECK(fd.chosen().files == n, QString("back to two sides: the files again (%1)").arg(fd.chosen().files));
        }
    }

    printf("== read-only rules\n");
    if (a.size() >= 2) {
        QFile::copy(a[1], wd + "/osborne.imd");
        MainWindow D;
        CHECK(D.openFile(wd + "/osborne.imd"), "open a real Osborne single-density IMD");
        CHECK(V(D).format().name == "osb1sssd" && D.writable(0), "detected as osb1sssd and writable: " + D.readOnlyReason(0));
        shot(&D, wd + "/shots/osborne.png");
        // try a wrong format by hand in the format dialog: the reading is different and not writable
        FormatDialog fd(&D.document()->container(), D.detection(), 0, false, &D);
        CHECK(fd.tryDiskdef("diskdef wrong\n seclen 256\n tracks 40\n sectrk 10\n blocksize 2048\n maxdir 64\n skew 0\n boottrk 3\nend\n", &err),
              "the format dialog evaluates a hand-made format");
        Detect::Candidate wrong = fd.chosen();
        CHECK(wrong.score < D.detection().ranked.first().score, QString("the hand-made (no skew) reading scores lower (%1 vs %2)")
              .arg(wrong.score).arg(D.detection().ranked.first().score));
        CHECK(D.useFormat(wrong, false) && !D.writable(0), "reading with an unconfirmed format is read-only");
        CHECK(D.useFormat(D.detection().ranked.first(), true) && D.writable(0), "back to the detected format");
        shot(&fd, wd + "/shots/formatdialog.png");
    }
    if (a.size() >= 3) {
        MainWindow T;
        CHECK(T.openFile(a[2]), "open a TeleDisk image");
        CHECK(!T.writable(0) && T.readOnlyReason(0).contains("TeleDisk"), "TeleDisk is read-only: " + T.readOnlyReason(0));
        int before = V(T).files().size();
        T.importPaths({wd + "/host/readme.txt"}, -1);
        CHECK(V(T).files().size() == before && !T.document()->container().hasPending(), "adding to a read-only disc does nothing");
        CHECK(T.saveAs(wd + "/fromtd0.imd", dw::device::Kind::Imd), "Save As ImageDisk from TeleDisk");
        MainWindow T2;
        CHECK(T2.openFile(wd + "/fromtd0.imd") && V(T2).files().size() == before, "the converted image reads the same files");
        CHECK(T2.writable(0), "and is writable: " + T2.readOnlyReason(0));
    }
    if (!rmImage.isEmpty()) {
        printf("== a Research Machines raw image: FM track 0 on each side, a disc on each side\n");
        QFile::copy(rmImage, wd + "/rm.img");
        QFile orig(rmImage);
        orig.open(QIODevice::ReadOnly);
        QByteArray before = orig.readAll();
        MainWindow R;
        R.show();
        auto *om = new QMimeData;
        om->setUrls({QUrl::fromLocalFile(wd + "/rm.img")});
        QDragEnterEvent enter(QPoint(50, 50), Qt::CopyAction, om, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(Tree(R)->viewport(), &enter);
        CHECK(enter.isAccepted(), "an empty window accepts a dropped image");
        QDragMoveEvent mv(QPoint(50, 50), Qt::CopyAction, om, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(Tree(R)->viewport(), &mv);
        QDropEvent dr(QPoint(50, 50), Qt::CopyAction, om, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(Tree(R)->viewport(), &dr);
        pump();
        delete om;
        CHECK(R.document()->container().isOpen() && R.document()->container().path() == wd + "/rm.img", "dropping the image on the empty window opens it");
        CHECK(R.document()->sides() == 2 && V(R, 0).format().name == "rm-qd", QString("two sides of rm-qd (%1 sides, %2)").arg(R.document()->sides()).arg(V(R, 0).format().name));
        CHECK(R.writable(0), "writable: " + R.readOnlyReason(0));
        CHECK(Tree(R)->topLevelItemCount() == 2, "a root row per side");
        int n0 = V(R, 0).files().size(), n1 = V(R, 1).files().size();
        put("rmnote.txt", "added to side 1\r\n");
        auto *rmm = new QMimeData;
        rmm->setUrls({QUrl::fromLocalFile(wd + "/host/rmnote.txt")});
        dropOn(Tree(R), findRow(Tree(R), RowRoot, -1) ? Tree(R)->topLevelItem(1) : nullptr, rmm);
        delete rmm;
        CHECK(V(R, 1).indexOf(0, "RMNOTE.TXT") >= 0 && V(R, 0).indexOf(0, "RMNOTE.TXT") < 0, "a file dropped on side 1 goes to side 1 only");
        // drag BASIC.COM from side 0 onto side 1 in the same window
        Tree(R)->clearSelection();
        QTreeWidgetItem *basic = nullptr;
        for (QTreeWidgetItem *it : Tree(R)->findItems("BASIC.COM", Qt::MatchExactly | Qt::MatchRecursive))
            if (it->data(0, RoleSide).toInt() == 0) basic = it;
        CHECK(basic != nullptr, "BASIC.COM on side 0");
        if (basic) {
            basic->setSelected(true);
            QMimeData *bm = Tree(R)->makeMime(Tree(R)->selectedItems());
            dropOn(Tree(R), Tree(R)->topLevelItem(1), bm);
            delete bm;
            pump();
            CHECK(V(R, 1).indexOf(0, "BASIC.COM") >= 0 && fileData(V(R, 1), 0, "BASIC.COM") == fileData(V(R, 0), 0, "BASIC.COM"),
                  "BASIC.COM copied from side 0 to side 1 within the window");
        }
        CHECK(V(R, 0).files().size() == n0, "side 0 unchanged");
        CHECK(R.save(), "save");
        QFile after(wd + "/rm.img");
        after.open(QIODevice::ReadOnly);
        QByteArray b = after.readAll();
        const int side = 2048 + 79 * 4608;
        CHECK(b.size() == before.size(), "the file is still 732160 bytes");
        CHECK(b.left(2048) == before.left(2048) && b.mid(side, 2048) == before.mid(side, 2048), "both FM track 0s unchanged");
        CHECK(b.left(side) == before.left(side), "side 0 byte-identical (nothing was written to it)");
        MainWindow R2;
        CHECK(R2.openFile(wd + "/rm.img") && R2.document()->sides() == 2 && V(R2, 1).files().size() == n1 + 2, "reopened: side 1 has the two new files");
        shot(&R2, wd + "/shots/rm.png");
    }
    printf("== Torch CP/N: a new disc, files in, a paste from another disc, saved and reopened\n");
    {
        MainWindow T;
        T.show();
        QString tp = wd + "/torch.dsd";
        CHECK(T.newImage(tp, dw::device::Kind::Raw, *Cpm::findFormat("torch-cpn"), false, QString()), "new Torch CP/N disc");
        CHECK(T.writable(0) && V(T).format().torch, "the new Torch disc is writable");
        T.importPaths({wd + "/host/readme.txt", wd + "/host/user3"}, -1);
        CHECK(V(T).indexOf(0, "README.TXT") >= 0 && V(T).indexOf(3, "GAME.COM") >= 0, "host files added, user3 folder into user 3");
        QAction *label = nullptr;
        for (QAction *x : T.findChildren<QAction *>()) if (x->text() == "Disc &label...") label = x;
        CHECK(label && !label->isEnabled(), "Torch discs have no label: Disc label is off");
        Tree(B)->clearSelection();
        findRow(Tree(B), RowFile, 0, "README.TXT")->setSelected(true);
        QMetaObject::invokeMethod(&B, "copySelected", Qt::DirectConnection);
        QString e2;
        CHECK(T.deleteFiles({ItemOf(0, 0, "README.TXT")}, &e2), "delete README.TXT from the Torch disc: " + e2);
        Tree(T)->clearSelection();
        QMetaObject::invokeMethod(&T, "paste", Qt::DirectConnection);
        CHECK(V(T).indexOf(0, "README.TXT") >= 0, "pasted from the IBM 3740 disc onto the Torch disc");
        CHECK(V(T).check().ok(), "the Torch disc checks clean: " + V(T).check().errors.join("; "));
        CHECK(T.save(), "saved");
        MainWindow T2;
        CHECK(T2.openFile(tp) && V(T2).format().torch && T2.writable(0), "reopened by detection as Torch CP/N, writable");
        CHECK(V(T2).files().size() == 2, QString("2 files after reopening (README.TXT, 3:GAME.COM), %1 found").arg(V(T2).files().size()));
        QMetaObject::invokeMethod(&T2, "discInfo", Qt::DirectConnection);      // autoAnswer: no dialog, just no crash
        shot(&T2, wd + "/shots/torch.png");
    }

    printf("== menus and buttons: the same order as Acorn Disk Wrangler's\n");
    {   // (Acorn Disk Wrangler has a few more items in between; with them taken out, both list the same things in the same order)
        QStringList edit, buttons, common;
        for (QAction *mm : A.menuBar()->actions())
            if (mm->menu() && mm->text() == "&Edit")
                for (QAction *x : mm->menu()->actions()) edit << (x->isSeparator() ? QString("-") : x->text().remove('&'));
        for (QToolBar *tb : A.findChildren<QToolBar *>()) for (QPushButton *b : tb->findChildren<QPushButton *>()) buttons << b->text();
        printf("  Edit: %s\n  buttons: %s\n", qPrintable(edit.join(" | ")), qPrintable(buttons.join(" | ")));
        CHECK(edit == QStringList({"View (text / hex)", "Extract to folder...", "-", "Add files...", "-", "Copy", "Paste", "-",
                                   "Rename...", "Properties (attributes, user)...", "Delete", "-", "Disc label..."}), "the Edit menu's order");
        CHECK(buttons == QStringList({"View", "Extract", "Add files", "Rename", "Properties", "Delete", "Format"}), "the buttons' order");
        for (const QString &b : buttons) if (b != "Format") common << b;
        CHECK(common == QStringList({"View", "Extract", "Add files", "Rename", "Properties", "Delete"}), "the buttons both programs have, in the same order");
    }

    printf("== copy and paste, with the platform's keys\n");
    {
        auto key = [](MainWindow *w, QKeySequence::StandardKey k) {
            w->activateWindow(); QTest::qWaitForWindowActive(w); Tree(*w)->setFocus(); pump();
            QTest::keySequence(Tree(*w), QKeySequence(k)); pump();
        };
        QClipboard *cb = QApplication::clipboard();
        int ia = V(A).indexOf(0, "AVERYLON.DOC");
        CHECK(ia >= 0 && V(B).indexOf(0, "AVERYLON.DOC") < 0, "0:AVERYLON.DOC is on disc A, not on B");
        QByteArray doc = fileData(V(A), 0, "AVERYLON.DOC");
        Tree(A)->clearSelection();
        findRow(Tree(A), RowFile, 0, "AVERYLON.DOC")->setSelected(true);
        key(&A, QKeySequence::Copy);       // the tree's own Ctrl+C (a row's text) must not get it
        const QMimeData *cm = cb->mimeData();
        CHECK(cm && cm->hasFormat(DW_MIME_CLIP) && cm->hasFormat(RetroFiles::MimeType) && cm->hasUrls() &&
              cm->hasFormat("x-special/gnome-copied-files"), "Ctrl+C puts the files on the clipboard");
        // what was copied is kept: delete it from A, then paste into B
        QString e;
        CHECK(A.deleteFiles({ItemOf(0, 0, "AVERYLON.DOC")}, &e), "delete the copied file from disc A: " + e);
        A.refresh();
        Tree(B)->clearSelection();
        findRow(Tree(B), RowUser, 3)->setSelected(true);
        key(&B, QKeySequence::Paste);
        CHECK(fileData(V(B), 3, "AVERYLON.DOC") == doc, "Ctrl+V pastes it into disc B, into the selected User 3, unchanged");
        Tree(B)->clearSelection();
        // files copied in a file manager: a URL list, or GNOME Files' own list (nothing selected: user 0)
        auto *u = new QMimeData;
        u->setUrls({QUrl::fromLocalFile(wd + "/host/misc/notes.txt")});
        cb->setMimeData(u);
        pump();
        QMetaObject::invokeMethod(&B, "paste", Qt::DirectConnection);
        CHECK(fileData(V(B), 0, "NOTES.TXT") == "notes\r\n", "a file copied in a file manager is pasted");
        auto *g = new QMimeData;
        g->setData("x-special/gnome-copied-files", "copy\n" + QUrl::fromLocalFile(wd + "/host/drop.txt").toEncoded());
        cb->setMimeData(g);
        pump();
        QMetaObject::invokeMethod(&B, "paste", Qt::DirectConnection);
        CHECK(fileData(V(B), 0, "DROP.TXT") == "dropped\r\n", "GNOME Files' list is pasted");
        // copied here, pasted in a file manager: the files are written when it asks, as for a drag
        Tree(B)->clearSelection();
        findRow(Tree(B), RowFile, 3, "AVERYLON.DOC")->setSelected(true);
        QMetaObject::invokeMethod(&B, "copySelected", Qt::DirectConnection);
        QList<QUrl> urls = cb->mimeData() ? cb->mimeData()->urls() : QList<QUrl>();
        QFile out(urls.value(0).toLocalFile());
        CHECK(urls.size() == 1 && QFileInfo(out).fileName() == "averylon.doc" && out.open(QIODevice::ReadOnly) && out.readAll() == doc,
              "a file manager gets averylon.doc (lower case, as Extract writes it) when it asks: " + QFileInfo(out).fileName());
        CHECK(cb->mimeData()->data("x-special/gnome-copied-files").startsWith("copy\nfile://"), "... and GNOME's list of it");
    }

    printf("== national character sets\n");
    {
        MainWindow N;
        CHECK(N.newImage(wd + "/nat.imd", dw::device::Kind::Imd, *Cpm::findFormat("ibm-3740"), false, QString()), "new disc for the character set test");
        CHECK(N.natId() == "ascii", "a new disc starts on plain ASCII");
        N.setNat("dk-no", false);
        QDir().mkpath(wd + "/host/nat");
        { QFile f(wd + "/host/nat/" + QString::fromUtf8("br\xC3\xB8" "d.txt")); f.open(QIODevice::WriteOnly); f.write("m\xC3\xA6lk\r\n"); }
        N.importPaths({wd + "/host/nat/" + QString::fromUtf8("br\xC3\xB8" "d.txt")}, -1);
        CHECK(V(N).indexOf(0, "BR\\D.TXT") >= 0, "brød.txt is stored as BR\\D.TXT with Danish chosen");
        N.setNat("ascii", false);
        QDir().mkpath(wd + "/host/nat2");
        { QFile f(wd + "/host/nat2/" + QString::fromUtf8("br\xC3\xB8" "e.txt")); f.open(QIODevice::WriteOnly); f.write("x\r\n"); }
        N.importPaths({wd + "/host/nat2/" + QString::fromUtf8("br\xC3\xB8" "e.txt")}, -1);
        CHECK(V(N).indexOf(0, "BR-E.TXT") >= 0, "without a set the same sort of name becomes BR-E.TXT as before");
        CHECK(V(N).indexOf(0, "BR\\D.TXT") >= 0, "the earlier file is untouched");
        N.setNat("dk-no", false);
        // retro-files: a name travels as the disc holds it; only the host file takes the national letters
        {
            Tree(N)->clearSelection();
            findRow(Tree(N), RowRoot, -1)->setSelected(true);
            QMimeData *dm = Tree(N)->makeMime(Tree(N)->selectedItems());
            QByteArray dj = dm->data(RetroFiles::MimeType);
            QList<QUrl> du = dm->urls();
            bool rawName = false, hostName = false;
            QString hostPath;
            for (const QJsonValue &v : QJsonDocument::fromJson(dj).object()["items"].toArray()) {
                QJsonObject it = v.toObject();
                if (it["name"].toString() == "BR\\D.TXT") {
                    rawName = true;
                    hostName = it["path"].toString() == QString::fromUtf8("br\xC3\xB8" "d.txt");
                    hostPath = it["path"].toString();
                }
            }
            CHECK(rawName, "retro-files name is BR\\D.TXT, unconverted");
            CHECK(hostName, "...while the file written to the host is brød.txt (" + hostPath + ")");
            CHECK(!du.isEmpty(), "dragged files listed");
            MainWindow P;                                       // a disc using plain ASCII
            CHECK(P.newImage(wd + "/natp.imd", dw::device::Kind::Imd, *Cpm::findFormat("ibm-3740"), false, QString()), "an ASCII disc to drop on");
            CHECK(P.importRetroFiles(dj, -1), "drop on the ASCII disc");
            CHECK(V(P).indexOf(0, "BR\\D.TXT") >= 0, "a name moved between CP/M discs stays exactly as it was");
            MainWindow Q;
            CHECK(Q.newImage(wd + "/natq.imd", dw::device::Kind::Imd, *Cpm::findFormat("ibm-3740"), false, QString()), "a Swedish disc to drop on");
            Q.setNat("se-fi", false);
            CHECK(Q.importRetroFiles(dj, -1) && V(Q).indexOf(0, "BR\\D.TXT") >= 0, "...and the same on a disc with another set chosen");
            delete dm;
        }
        // dropping a host file whose name has letters ASCII can't hold offers the national set (answered here by a timer)
        {
            MainWindow H;
            CHECK(H.newImage(wd + "/nath.imd", dw::device::Kind::Imd, *Cpm::findFormat("rm-dd"), false, QString()), "an RM disc to drop a Danish name on");
            QDir().mkpath(wd + "/host/nat3");
            const QString hp = wd + "/host/nat3/" + QString::fromUtf8("l\xC3\xB8nsedd.l");
            { QFile f(hp); f.open(QIODevice::WriteOnly); f.write("x"); }
            bool asked = false, offered = false;
            QTimer t;
            QObject::connect(&t, &QTimer::timeout, [&] {
                QWidget *m = QApplication::activeModalWidget();
                if (auto *box = qobject_cast<QMessageBox *>(m)) {
                    asked = true;
                    for (QAbstractButton *b : box->buttons()) if (b->text().startsWith("Choose")) { b->click(); break; }
                } else if (auto *d = qobject_cast<Nat::Dialog *>(m)) {
                    offered = d->chosen() == "dk-no";
                    d->accept();
                }
            });
            t.start(50);
            MainWindow::autoAnswer = false;
            {   // as a file manager drags it: a text/uri-list dropped on the tree
                auto *um = new QMimeData;
                um->setUrls({QUrl::fromLocalFile(hp)});
                H.show();
                dropOn(Tree(H), findRow(Tree(H), RowRoot, -1), um);
                for (int i = 0; i < 40 && V(H).files().isEmpty(); ++i) { QTest::qWait(50); pump(); }
                delete um;
            }
            MainWindow::autoAnswer = true;
            t.stop();
            CHECK(asked && offered, "it asked, and offered Danish/Norwegian as the likely set");
            CHECK(H.natId() == "dk-no" && V(H).indexOf(0, "L\\NSEDD.L") >= 0, "lønsedd.l comes onto the disc as L\\NSEDD.L");
        }
        // Tools > Allow lower-case file names: off for a new disc, so names are made upper case; on, they keep their case
        {
            MainWindow L;
            CHECK(L.newImage(wd + "/natl.imd", dw::device::Kind::Imd, *Cpm::findFormat("ibm-3740"), false, QString()), "a disc for lower-case names");
            L.setNat("dk-no", false);
            CHECK(!L.lowerNames(), "lower-case names are not allowed on a new disc");
            QDir().mkpath(wd + "/host/low");
            const QString lp = wd + "/host/low/" + QString::fromUtf8("l\xC3\xB8nsedde.l");
            { QFile f(lp); f.open(QIODevice::WriteOnly); f.write("again"); }
            MainWindow::autoAnswer = true;
            L.importPaths({lp}, -1, 0);
            CHECK(V(L).indexOf(0, "L\\NSEDDE.L") >= 0 && V(L).indexOf(0, "l|nsedde.l") < 0, "unticked: lønsedde.l becomes L\\NSEDDE.L");
            L.setLowerNames(true, false);
            L.importPaths({lp}, -1, 0);
            CHECK(V(L).indexOf(0, "l|nsedde.l") >= 0 && V(L).files().size() == 2, "ticked: lønsedde.l is written as l|nsedde.l, in lower case");
            QString e;
            CHECK(V(L).rename(0, "l|nsedde.l", 0, "l|nsede2.l", &e) && V(L).indexOf(0, "l|nsede2.l") >= 0, "a rename keeps lower case when allowed: " + e);
            CHECK(V(L).rename(0, "l|nsede2.l", 0, "l|nsedde.l", &e), "...and back: " + e);
            Tree(L)->clearSelection();
            findRow(Tree(L), RowRoot, -1)->setSelected(true);
            QMimeData *lm = Tree(L)->makeMime(Tree(L)->selectedItems());
            QByteArray lj = lm->data(RetroFiles::MimeType);
            bool lowName = false, lowPath = false;
            for (const QJsonValue &v : QJsonDocument::fromJson(lj).object()["items"].toArray())
                if (v.toObject()["name"].toString() == "l|nsedde.l") { lowName = true; lowPath = v.toObject()["path"].toString().startsWith(QString::fromUtf8("l\xC3\xB8nsedde")); }   // (L\\NSEDDE.L is on the disc too, so this may be lønsedde_1.l)
            CHECK(lowName && lowPath, "dragged out: name l|nsedde.l, host file lønsedde.l");
            delete lm;
            shot(&L, wd + "/shots/lowercase.png");
            L.resize(430, 300); pump(); L.grab().save(wd + "/shots/narrow.png");
            CHECK(L.save(), "save the disc with a lower-case name");
            MainWindow L2;
            CHECK(L2.openFile(wd + "/natl.imd") && L2.lowerNames(), "reopened: ticked automatically because a name is lower case");
            L2.setLowerNames(false, true);
            MainWindow L3;
            CHECK(L3.openFile(wd + "/natl.imd") && !L3.lowerNames(), "an untick is remembered for that disc");
            L3.setLowerNames(true, true);
            MainWindow L4;
            CHECK(L4.openFile(wd + "/natl.imd") && L4.lowerNames(), "...and so is ticking it again");
            QAction *act = nullptr;
            for (QAction *x : L4.findChildren<QAction *>()) if (x->text().contains("lower-case file names")) act = x;
            CHECK(act && act->isCheckable() && act->isChecked(), "the Tools menu has the ticked 'Allow lower-case file names'");
            QToolButton *nb4 = nullptr;
            for (QToolButton *b : L4.findChildren<QToolButton *>()) if (b->text().startsWith("Names:")) nb4 = b;
            CHECK(nb4 && nb4->text().contains("any case"), "the status bar button says names keep their case (lower case is allowed on this disc): " + (nb4 ? nb4->text() : QString()));
            QMenu *nm = nb4 ? nb4->menu() : nullptr;
            bool hasLower = false, hasSet = false;
            if (nm) for (QAction *x : nm->actions()) { hasLower |= x == act; hasSet |= x->text().contains("Character set"); }
            CHECK(hasLower && hasSet, "its menu has the lower-case option and the character set dialog");
            if (act && nb4) { act->trigger(); CHECK(!L4.lowerNames() && nb4->text().contains("upper case"), "unticking it changes the setting and the button's text"); }
        }
        // two names that differ only in case, written to the host and dropped back
        {
            QVector<dw::TransferItem> xi;
            auto mk = [&](const QString &name, const QString &host) {
                dw::TransferItem x; x.name = name; x.hostName = host; x.sourceFs = "cpm"; x.data = "z";
                QJsonObject a; a["user"] = 0; x.attrs["cpm"] = a;
                return x;
            };
            xi << mk("L\\NSEDDE.L", QString::fromUtf8("L\xC3\x98NSEDDE.L")) << mk("l|nsedde.l", QString::fromUtf8("l\xC3\xB8nsedde.l")) << mk("PLAIN.TXT", "PLAIN.TXT");
            QDir().mkpath(wd + "/host/out");
            QStringList top, errs;
            QJsonArray ja;
            dw::xfer::writeHost(xi, wd + "/host/out", RetroFiles::CpmPolicy(false, false), &top, &ja, &errs);
            QDir od(wd + "/host/out");
            const QStringList got = od.entryList(QDir::Files, QDir::Name);
            const bool sensitive = got.size() == 3 && got.contains(QString::fromUtf8("l\xC3\xB8nsedde.l")) && got.contains(QString::fromUtf8("L\xC3\x98NSEDDE.L"));
            printf("  host file names: %s\n", qPrintable(got.join(" | ")));
            CHECK(errs.isEmpty() && got.contains("plain.txt") && got.contains(QString::fromUtf8("l\xC3\xB8nsedde.l")), "the lower-case name keeps its case; an upper-case name with no clash is lower-cased");
            CHECK(sensitive || got.contains(QString::fromUtf8("L\xC3\x98NSEDDE_2.L")), "the upper-case one keeps its case too, and gets _2 only if the host can't tell them apart");
            // dropped back with the suffix the host needed
            MainWindow B2;
            CHECK(B2.newImage(wd + "/natsuf.imd", dw::device::Kind::Imd, *Cpm::findFormat("ibm-3740"), false, QString()), "a disc for the dropped-back names");
            B2.setNat("dk-no", false);
            B2.setLowerNames(true, false);
            const QString p1 = wd + "/host/out/" + QString::fromUtf8("l\xC3\xB8nsedde.l");
            QDir().mkpath(wd + "/host/out2");
            { QFile f(p1); QFile g(wd + "/host/out2/" + QString::fromUtf8("l\xC3\xB8nsedde.l")); f.copy(g.fileName()); }
            { QFile f(wd + "/host/out2/" + QString::fromUtf8("L\xC3\x98NSEDDE_2.L")); f.open(QIODevice::WriteOnly); f.write("z"); }
            { QFile f(wd + "/host/out2/foo_1.txt"); f.open(QIODevice::WriteOnly); f.write("z"); }
            MainWindow::autoAnswer = true;
            B2.importPaths({wd + "/host/out2/" + QString::fromUtf8("l\xC3\xB8nsedde.l"), wd + "/host/out2/" + QString::fromUtf8("L\xC3\x98NSEDDE_2.L"), wd + "/host/out2/foo_1.txt"}, -1, 0);
            CHECK(V(B2).indexOf(0, "l|nsedde.l") >= 0 && V(B2).indexOf(0, "L\\NSEDDE.L") >= 0, "the _2 is taken off again and the case kept: l|nsedde.l and L\\NSEDDE.L");
            CHECK(V(B2).indexOf(0, "foo-1.txt") >= 0, "a real foo_1.txt is left alone (becomes foo-1.txt)");
        }
        N.setNat("dk-no", true);
        {
            QToolButton *nb = nullptr;
            for (QToolButton *b : N.findChildren<QToolButton *>()) if (b->text().startsWith("Names:")) nb = b;
            {   // the character set dialog: the list is as wide as its entries, the preview takes the rest
                QVector<const Cpm::Store *> vs{&V(N)};
                Nat::Dialog dlg(vs, "dk-no");
                shot(&dlg, wd + "/shots/natdialog.png");
                QListWidget *lw = dlg.findChild<QListWidget *>();
                CHECK(lw && lw->width() < 340 && lw->horizontalScrollBar()->maximum() == 0, "character set list fits its entries, no sideways scrolling");
            }
            {   // adding lønsedde.l: the dialog names the likely set, selects it, and previews the name on the disc
                QVector<const Cpm::Store *> vs{&V(N)};
                Nat::Dialog dlg(vs, "ascii", nullptr, {QString::fromUtf8("l\xC3\xB8nsedde.l")});
                QListWidget *lw = dlg.findChild<QListWidget *>();
                QPlainTextEdit *pv = dlg.findChild<QPlainTextEdit *>();
                CHECK(dlg.chosen() == "dk-no" && lw && lw->currentItem()->text().startsWith("Danish"), "the letter ø selects Danish/Norwegian");
                CHECK(pv && pv->toPlainText().contains("L\\NSEDDE.L") && pv->toPlainText().contains(QString::fromUtf8("L\xC3\x98NSEDDE.L")),
                      "the preview shows lønsedde.l written as L\\NSEDDE.L and shown as LØNSEDDE.L");
            }
            CHECK(nb && nb->text().contains("Danish"), "the status bar says which character set is in use: " + (nb ? nb->text() : QString("(missing)")));
        }
        shot(&N, wd + "/shots/nat.png");
        CHECK(N.save(), "save");
        MainWindow N2;
        CHECK(N2.openFile(wd + "/nat.imd"), "reopen it");
        CHECK(N2.natId() == "dk-no", "the choice is remembered for that disc");
        N2.setNat("ascii", true);
        MainWindow N3;
        CHECK(N3.openFile(wd + "/nat.imd") && N3.natId() == "ascii", "choosing ASCII again forgets it");
    }

    printf("== file dialog filters match upper-case names too\n");
    CHECK(dw::anyCase("*.imd *.dsk") == "*.imd *.IMD *.dsk *.DSK", "*.dsk also lists DISK.DSK: " + dw::anyCase("*.imd *.dsk"));
    CHECK(dw::anyCase("*") == "*", "a bare * is left alone");

    printf("== the status line fits its text to the width\n");
    {
        StatusLabel sl;
        sl.setVariants({"rm-dd - 0 files, 164.0 KB free of 296.0 KB", "rm-dd - 0 files, 164.0 KB free", "0 files, 164.0 KB free", "164.0 KB free"});
        const QFontMetrics fm = sl.fontMetrics();
        sl.resize(fm.horizontalAdvance("rm-dd - 0 files, 164.0 KB free of 296.0 KB") + 4, 20);
        CHECK(sl.shown() == "rm-dd - 0 files, 164.0 KB free of 296.0 KB", "wide enough: the whole text");
        sl.resize(fm.horizontalAdvance("rm-dd - 0 files, 164.0 KB free") + 4, 20);
        CHECK(sl.shown() == "rm-dd - 0 files, 164.0 KB free", "a little narrower: without the capacity");
        sl.resize(fm.horizontalAdvance("0 files, 164.0 KB free") + 4, 20);
        CHECK(sl.shown() == "0 files, 164.0 KB free", "narrower: without the format name");
        sl.resize(fm.horizontalAdvance("164.0 KB free") + 4, 20);
        CHECK(sl.shown() == "164.0 KB free" && sl.toolTip().startsWith("rm-dd - 0 files, 164.0 KB free of"), "narrowest: the free space; the tooltip keeps the whole text");
        sl.resize(30, 20);
        CHECK(sl.shown().endsWith(QString::fromUtf8("\xE2\x80\xA6")), "too narrow even for that: cut with an ellipsis");
    }

    printf("== files opened from the Finder (fileopen.h)\n");
    {
        FileOpen::FileOpenFilter<MainWindow> filter;
        qApp->installEventFilter(&filter);
        auto holding = [](const QString &p) {
            QList<MainWindow *> r;
            for (MainWindow *w : openWindows())
                if (w->document()->container().isOpen() && QFileInfo(w->document()->container().path()).canonicalFilePath() == QFileInfo(p).canonicalFilePath()) r << w;
            return r;
        };
        auto finderOpen = [](const QString &p) { QFileOpenEvent ev(p); QCoreApplication::sendEvent(qApp, &ev); pump(); };
        CHECK(QFile::copy(wd + "/b.imd", wd + "/fo1.imd") && QFile::copy(wd + "/b.imd", wd + "/fo2.imd"), "two copies of b.imd to open");
        const int n = openWindows().size();
        finderOpen(wd + "/b.imd");
        CHECK(openWindows().size() == n && holding(wd + "/b.imd").size() == 1, "an image already open is brought forward, not opened twice");
        finderOpen(wd + "/fo1.imd");
        CHECK(openWindows().size() == n + 1 && holding(wd + "/fo1.imd").size() == 1, "with every window holding a disc, it gets a window of its own");
        auto *empty = new MainWindow;
        empty->setAttribute(Qt::WA_DeleteOnClose);
        empty->show();
        pump();
        finderOpen(wd + "/fo2.imd");
        CHECK(openWindows().size() == n + 2 && holding(wd + "/fo2.imd").value(0) == empty, "an empty window takes it");
    }

    shot(&A, wd + "/shots/pcw.png");
    shot(&B, wd + "/shots/ibm3740.png");
    printf("%s\n", failures ? qPrintable(QString("%1 FAILURES").arg(failures)) : "all ok");
    return failures ? 1 : 0;
}
