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
#include <QApplication>
#include "cpmformat.h"
#include "appsettings.h"
#include "dragout.h"
#include "fileopen.h"
#include "icon.h"
#include "mainwindow.h"

#ifndef APP_VERSION                // set by version.pri / version.cmake from git describe
#define APP_VERSION "unknown"
#endif

int main(int argc, char *argv[]) {
    QGuiApplication::setDesktopFileName("cpmdiskwrangler");   // before QApplication: registers with the portal first
    QApplication app(argc, argv);
    QApplication::setOrganizationName("cpmdiskwrangler");
    QApplication::setApplicationName("cpmdiskwrangler");
    AppSettings::init();                             // portable mode if <program>.ini is beside the program
    QApplication::setApplicationVersion(APP_VERSION);
    QGuiApplication::setDesktopFileName("cpmdiskwrangler");
    QApplication::setWindowIcon(appIcon());
    RetroFiles::cleanStale();
    FileOpen::FileOpenFilter fileOpen;               // macOS: files opened from the Finder (fileopen.h)
    app.installEventFilter(&fileOpen);
    QStringList warnings;
    Cpm::setExtraDiskdefFiles(AppSettings().value("diskdefFiles").toStringList(), &warnings);
    MainWindow w;
    w.show();
    for (int i = 1; i < argc; ++i) {
        QString p = QString::fromLocal8Bit(argv[i]);
        if (i == 1) {
            w.openImage(p);
        } else {
            auto *x = new MainWindow;
            x->setAttribute(Qt::WA_DeleteOnClose);
            x->show();
            x->openImage(p);
        }
    }
    return app.exec();
}
