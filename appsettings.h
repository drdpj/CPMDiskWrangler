/*
 * Settings - shared, unchanged, by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// Where the program keeps its settings. Normally the system's own place (the registry on Windows, a plist in
// ~/Library/Preferences on macOS, ~/.config/<program>/<program>.conf elsewhere). Portable mode: if a file
// <program>.ini (acorndiskwrangler.ini, cpmdiskwrangler.ini) sits in the program's folder - beside the .exe, or
// beside the .app bundle on macOS - the settings are kept in that file instead, so the program can be carried on
// a USB stick with them. An empty file is enough to switch it on. (On a read-only medium the settings are read
// but not saved.)
//
// File > Create local config file makes that file (createPortableFileDialog), copying the current settings into
// it, and switches to it at once. Deleting the file goes back to the system's place at the next start.
//
// Use AppSettings wherever QSettings would be used; AppSettings::init() once in main(), after the organisation
// and application names are set. Header only.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>

class AppSettings : public QSettings {
public:
    AppSettings()
        : QSettings(portable() ? IniFormat : NativeFormat, UserScope, QCoreApplication::organizationName(),
                    // no application name: Qt then uses <path>/<organisation>.ini, which is the portable file
                    portable() ? QString() : QCoreApplication::applicationName()) {
        if (portable()) setFallbacksEnabled(false);          // nothing from system-wide (e.g. /etc/xdg) files
    }

    // The folder the program is in, as the user sees it: on macOS the folder that holds the .app bundle.
    static QString programFolder() {
        QDir d(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
        QDir up = d;
        if (up.dirName() == "MacOS" && up.cdUp() && up.dirName() == "Contents" && up.cdUp() &&
            up.dirName().endsWith(".app") && up.cdUp())
            return up.absolutePath();
#endif
        return d.absolutePath();
    }
    static QString portableFile() { return programFolder() + "/" + QCoreApplication::organizationName() + ".ini"; }

    static void init(const QString &folder = QString()) {      // folder: for tests, instead of the program's
        QString f = folder.isEmpty() ? portableFile() : folder + "/" + QCoreApplication::organizationName() + ".ini";
        portableFlag() = QFileInfo(f).isFile();
        if (portableFlag()) QSettings::setPath(IniFormat, UserScope, QFileInfo(f).absolutePath());
    }
    static bool portable() { return portableFlag(); }

    // Makes the portable file in the program's folder with a copy of the current settings, and uses it from now on.
    static bool createPortableFile(QString *err) {
        const QString f = portableFile();
        if (portable()) { *err = "the settings are already kept in " + QDir::toNativeSeparators(f); return false; }
        const bool existed = QFileInfo::exists(f);
        {
            QSettings ini(f, IniFormat);
            AppSettings current;
            for (const QString &k : current.allKeys()) ini.setValue(k, current.value(k));
            ini.sync();
            if (ini.status() != NoError || !QFileInfo(f).isFile()) {
                if (!existed) QFile::remove(f);
                *err = "could not write " + QDir::toNativeSeparators(f) + " (is the program's folder read-only?)";
                return false;
            }
        }
        init();
        if (!portable()) { *err = "could not switch to " + QDir::toNativeSeparators(f); return false; }
        return true;
    }

    // File > Create local config file: asks, makes the file, says what happened.
    static void createPortableFileDialog(QWidget *parent) {
        const QString f = QDir::toNativeSeparators(portableFile());
        if (portable()) {
            QMessageBox::information(parent, "Local config file", "The settings are already kept beside the program, in\n" + f);
            return;
        }
        if (QMessageBox::question(parent, "Create local config file",
                                  QString("Create %1 and keep this program's settings in it from now on?\n\n"
                                          "The current settings are copied into it. Carry the program's folder (on a USB "
                                          "stick, say) and its settings go with it. Delete the file to go back to the "
                                          "system's settings.").arg(f)) != QMessageBox::Yes)
            return;
        QString err;
        if (!createPortableFile(&err)) QMessageBox::warning(parent, "Create local config file", "Couldn't: " + err + ".");
        else QMessageBox::information(parent, "Create local config file", "The settings are now kept in\n" + f);
    }

    // Where the settings are, in words, for Help > About.
    static QString where() {
        AppSettings s;
        return portable() ? "Settings are kept beside the program (portable): " + QDir::toNativeSeparators(s.fileName())
                          : "Settings: " + QDir::toNativeSeparators(s.fileName()) +
                                QString(" (File > Create local config file keeps them beside the program instead, in %1.ini)")
                                    .arg(QCoreApplication::organizationName());
    }

private:
    static bool &portableFlag() { static bool p = false; return p; }
};
