/*
 * CP/M Disk Wrangler - view and edit CP/M disc images
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
// Real drag-and-drop out of a disc window, on a real X server, driven by moving the real pointer with xdotool (series
// 6c-2; Acorn Disk Wrangler's xdragtest is its twin). Linux / X11 only:
//     xvfb-run -a -s "-screen 0 1280x800x24" xdragtest <outdir>       (needs Xvfb and xdotool)
// The drop targets are other processes (this program started again):
//   "xdragtest qt <log>": a Qt window that asks for the files when the drag enters it and takes the drop;
//   "xdragtest xdnd <silent> <log>": a target written straight on xcb (core/tests/xdndtarget.h) that asks as GTK 4
//   (Nautilus) and mutter's XWayland bridge do - TARGETS, a portal format nobody offers, then text/uri-list, taking
//   only an answer for the very target it asked - or, silent, never asks.
// Each drop must give the files (or none) and the window's message for them: "Copied 1 file out of the disc." when
// the files were asked for and written, a "No files were copied" line when they weren't. Before 6c-2 the xcb target
// got no answer to TARGETS or the portal format (Qt's answers name no target), and the window said nothing of a drop
// whose files are asked for after QDrag::exec() returns (X11 targets ask at the drop) or never.
#include "../src/mainwindow.h"
#include "../src/mainwindow_p.h"
#include "../src/cpmformat.h"
#include "../core/tests/xdndtarget.h"
#include <QApplication>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTreeWidget>
#include <cstdio>

namespace {
struct QtTarget : QWidget {
    QString log;
    explicit QtTarget(QString l) : log(l) { setAcceptDrops(true); }
    void dragEnterEvent(QDragEnterEvent *e) override {
        xdndtarget::logLine(log, QString("asked %1").arg(e->mimeData()->urls().size()));
        e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *e) override { e->acceptProposedAction(); }
    void dropEvent(QDropEvent *e) override {
        QStringList names;
        for (const QUrl &u : e->mimeData()->urls()) names << (QFile::exists(u.toLocalFile()) ? u.fileName() : "missing:" + u.fileName());
        xdndtarget::logLine(log, "dropped " + names.join(' '));
        e->setDropAction(Qt::CopyAction);
        e->accept();
    }
};
} // namespace

static void wait(int ms) { QElapsedTimer t; t.start(); while (t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 20); }

static QTreeWidgetItem *fileRow(QTreeWidget *t, const QString &name) {
    QList<QTreeWidgetItem *> stack;
    for (int i = 0; i < t->topLevelItemCount(); ++i) stack << t->topLevelItem(i);
    while (!stack.isEmpty()) {
        QTreeWidgetItem *it = stack.takeFirst();
        if (it->data(0, RoleKind).toInt() == RowFile && it->data(0, RoleName).toString() == name) return it;
        for (int i = 0; i < it->childCount(); ++i) stack << it->child(i);
    }
    return nullptr;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc > 2 && QString(argv[1]) == "qt") {
        QtTarget t(argv[2]); t.setGeometry(700, 50, 300, 300); t.show();
        return app.exec();
    }
    if (argc > 3 && QString(argv[1]) == "xdnd") return xdndtarget::Target(argv[3], QString(argv[2]) == "1").run(700, 50, 300, 300);
    if (argc < 2) { printf("usage: xdragtest <outdir>\n"); return 2; }
    const QString dir = argv[1], img = dir + "/d.img", host = dir + "/README.TXT";
    { QFile f(host); f.open(QIODevice::WriteOnly); f.write(QByteArray(3000, 'r')); }
    {
        MainWindow w;
        w.newImage(img, dw::device::Kind::Raw, *Cpm::findFormat("ibm-3740"), false, QString());
        w.importPaths({host}, 0);
        w.save();
    }
    QFile::remove(host);
    struct Case { const char *what, *mode, *silent; };
    const Case cases[] = {{"dropped on a Qt target", "qt", nullptr}, {"dropped on a GTK 4-like target", "xdnd", "0"},
                          {"dropped on a target that never asks", "xdnd", "1"}};
    int fails = 0, sc = 0;
    for (const Case &c : cases) {
        const QString log = dir + QString("/log%1.txt").arg(sc);
        QFile::remove(log);
        QProcess tg;
        tg.start(argv[0], c.silent ? QStringList{c.mode, c.silent, log} : QStringList{c.mode, log});
        wait(1500);
        MainWindow w; w.setGeometry(20, 50, 600, 400); w.show(); w.openFile(img); wait(800);
        auto *tree = static_cast<DropTree *>(w.pane());
        QTreeWidgetItem *it = fileRow(tree, "README.TXT");
        if (!it) { printf("%-40s README.TXT not on the disc\n", c.what); ++fails; ++sc; continue; }
        tree->setCurrentItem(it); it->setSelected(true);
        const QPoint p = tree->viewport()->mapToGlobal(tree->visualItemRect(it).center());
        QString msg; bool nothing = false;
        QObject::connect(tree, &DropTree::dragOutFinished, [&](int f, int, const QStringList &) { msg = QString("copied %1").arg(f); });
        QObject::connect(tree, &DropTree::dragOutNothing, [&] { nothing = true; });
        // press on the row and move the real pointer: the tree starts the drag itself, as for a person
        const QString path = QString("xdotool mousemove %1 %2 mousedown 1; sleep 0.3; xdotool mousemove %3 %4; sleep 0.3; ").arg(p.x()).arg(p.y()).arg(p.x() + 30).arg(p.y() + 5) +
                             "xdotool mousemove 850 200; sleep 0.5; xdotool mousemove 860 210; sleep 0.5; xdotool mouseup 1";
        QProcess drv; drv.start("sh", {"-c", path});
        while (drv.state() != QProcess::NotRunning) wait(50);
        QElapsedTimer t; t.start();               // the target may ask after the drop; "nothing" waits for it a while
        while (msg.isEmpty() && !nothing && t.elapsed() < 2 * dw::ui::VolumePane::DragOutWaitMs) wait(50);
        wait(300);
        // StatusLabel has no Q_OBJECT, so findChild can't look for it by type (Qt 6.10 refuses to compile that): by RTTI
        QString status;
        for (QLabel *l : w.findChildren<QLabel *>())
            if (StatusLabel *sl = dynamic_cast<StatusLabel *>(l)) { status = sl->toolTip(); break; }
        tg.kill(); tg.waitForFinished();
        QFile l(log); l.open(QIODevice::ReadOnly);
        const QString got = QString::fromUtf8(l.readAll()).trimmed().replace('\n', " | ");
        printf("%-40s target: %s; status line: %s\n", c.what, qPrintable(got), qPrintable(status));
        const bool copied = msg == "copied 1" && !nothing && status == "Copied 1 file out of the disc.";
        bool ok;
        if (sc == 0) ok = got == "asked 1 | dropped readme.txt" && copied;
        else if (sc == 1) ok = got.contains(QRegularExpression("^targets TARGETS .*text/uri-list")) && got.contains("portal refused") &&
                               got.contains("uri-list 1 readme.txt") && copied;
        else ok = got == "dropped, asked for nothing" && nothing && msg.isEmpty() && status.startsWith("No files were copied out of the disc");
        if (!ok) ++fails;
        ++sc;
    }
    printf(fails ? "FAILED (%d)\n" : "ALL OK\n", fails);
    return fails;
}
