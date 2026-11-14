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
// Renders the main window and saves PNGs, for eyeballing layout/alignment (as Acorn Disk Wrangler's screenshot).
//   screenshot <outdir> <image> [<image> ...]      (images are opened read/write: pass copies)
// Each image is saved as <outdir>/<image file name>.png at a wide (1300px) window and as
// <image file name>_narrow.png at the default window size, so both the column stretch and the narrow-window
// case can be checked. For the first image, every dialog the main window opens is also saved, as
// <outdir>/dialog_<slot>.png.
#include "../src/mainwindow.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>

static void pump() { for (int i = 0; i < 5; ++i) QCoreApplication::processEvents(); }
static void shoot(QWidget *w, const QString &file) {
    pump();
    w->grab().save(file); printf("wrote %s\n", qPrintable(file));
}
// The first file row of a tree (not a directory row), selected so that a slot acting on the selection has
// something to act on.
static void selectFirstFile(QTreeWidget *t) {
    QList<QTreeWidgetItem *> q;
    for (int i = 0; i < t->topLevelItemCount(); ++i) q << t->topLevelItem(i);
    while (!q.isEmpty()) {
        QTreeWidgetItem *it = q.takeFirst();
        if (it->data(0, RoleKind).toInt() == RowFile) { t->setCurrentItem(it); return; }
        for (int i = 0; i < it->childCount(); ++i) q << it->child(i);
    }
}
static QList<QDialog *> shownDialogs() {
    QList<QDialog *> l;
    for (QWidget *w : QApplication::topLevelWidgets())
        if (QDialog *d = qobject_cast<QDialog *>(w)) if (d->isVisible()) l << d;
    return l;
}
static void saveDialog(QWidget *d, const QString &file) {
    QDialog *dlg = qobject_cast<QDialog *>(d);
    d->resize(dlg ? 1120 : 1000, 640);
    pump();
    d->grab().save(file); printf("wrote %s\n", qPrintable(file));
    if (dlg) dlg->reject(); else d->close();
}
// Calls the slot and saves the dialog it opens as dialog_<slot>.png, then closes it. A modal dialog is caught
// while the slot waits for it; a dialog the slot shows without waiting is caught once the slot has returned.
// Returns false if the slot opened no dialog.
static bool dialogShot(MainWindow &w, const char *slot, const QString &outdir) {
    const QString file = outdir + "/dialog_" + slot + ".png";
    QTimer t; t.setInterval(20);
    bool taken = false;
    QObject::connect(&t, &QTimer::timeout, [&] {
        QWidget *d = QApplication::activeModalWidget();
        if (!d || taken) return;
        taken = true;
        saveDialog(d, file);
    });
    const QList<QDialog *> before = shownDialogs();
    t.start();
    if (!QMetaObject::invokeMethod(&w, slot))           // natDialog is not a slot: the menu action that opens it
        for (QAction *a : w.findChildren<QAction *>())
            if (a->text().contains("Character set for names")) { a->trigger(); break; }
    t.stop();
    if (!taken)
        for (QDialog *d : shownDialogs())
            if (!taken && !before.contains(d)) { taken = true; saveDialog(d, file); }
    if (!taken) printf("FAIL: %s opened no dialog\n", slot);
    pump();
    return taken;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("cpmdiskwrangler-shots"); QApplication::setApplicationName("s");
    if (argc < 3) { printf("usage: screenshot <outdir> <image> [<image> ...]\n"); return 2; }
    QString out = argv[1];
    bool ok = true;
    for (int i = 2; i < argc; ++i) {
        MainWindow w; w.resize(1300, 560); w.show();
        if (!w.openFile(argv[i])) { printf("FAIL: %s did not open\n", argv[i]); ok = false; continue; }
        pump();
        QString base = out + "/" + QFileInfo(argv[i]).fileName();
        shoot(&w, base + ".png");
        if (i == 2) {
            // the dialogs, for the first image; a file row is selected first for those that act on one
            selectFirstFile(w.pane());
            for (const char *slot : {"newImageDialog", "formatDialog", "natDialog", "setLabelDialog", "viewSelected", "editProperties", "about"})
                ok = dialogShot(w, slot, out) && ok;
        }
        MainWindow n; n.show();                  // default size, clamped to the minimum width
        n.openFile(argv[i]); pump();
        shoot(&n, base + "_narrow.png");
    }
    return ok ? 0 : 1;
}
