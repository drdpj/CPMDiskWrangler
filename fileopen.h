/*
 * Opening files handed over by the system - shared, unchanged, by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#pragma once

// macOS hands a program the files opened from the Finder (double-click, Open With, a file dropped on the Dock icon)
// as a QFileOpenEvent - the file that starts the program too - and never on the command line; with the program
// already running it doesn't start another copy. Windows and Linux start a copy with the file as an argument
// (main.cpp), so on them this never fires. main() installs a FileOpenFilter on the application.
//
// Like File > Open and a drop: an empty window takes the image (the active one first, so the window a cold start
// made is used), otherwise it gets a window of its own. An image already open in a window brings that window
// forward instead of being opened twice. The work is queued, so no dialog runs inside the event. Needs only
// MainWindow::windows(), image().isOpen(), image().path() and openImage(path), which both programs have.

#include "mainwindow.h"
#include <QApplication>
#include <QEvent>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QObject>
#include <QTimer>

namespace FileOpen {

inline void bringForward(MainWindow *w) {
    w->setWindowState(w->windowState() & ~Qt::WindowMinimized);
    w->show();
    w->raise();
    w->activateWindow();
}

inline void open(const QString &path) {
    const QString canonical = QFileInfo(path).canonicalFilePath();
    MainWindow *empty = qobject_cast<MainWindow *>(QApplication::activeWindow());
    if (empty && empty->image().isOpen()) empty = nullptr;
    for (MainWindow *w : MainWindow::windows()) {
        const QString held = w->image().isOpen() ? w->image().path() : QString();
        if (!held.isEmpty() && !canonical.isEmpty() && QFileInfo(held).canonicalFilePath() == canonical) {
            bringForward(w);                                // already open here: don't open it twice
            return;
        }
        if (!empty && !w->image().isOpen()) empty = w;
    }
    if (empty) {
        bringForward(empty);
        empty->openImage(path);
        return;
    }
    auto *w = new MainWindow;
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->show();
    if (!w->openImage(path)) w->close();
}

class FileOpenFilter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() == QEvent::FileOpen) {
            const QString path = static_cast<QFileOpenEvent *>(e)->file();
            if (!path.isEmpty()) {
                QTimer::singleShot(0, qApp, [path] { open(path); });
                return true;
            }
        }
        return QObject::eventFilter(o, e);
    }
};

} // namespace FileOpen
