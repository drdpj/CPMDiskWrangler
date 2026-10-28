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
// but not saved.) An AppImage runs from a temporary mount, so its folder is the one holding the .AppImage file
// ($APPIMAGE). A program installed on Linux (a .deb, make install) has no folder of its own to carry about, so
// it has no portable mode at all: offered() is false, and a .ini in /usr/bin is never looked for.
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
#include <QMenu>
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

    // Whether portable mode exists here: Windows, macOS, and a Linux AppImage; not a program installed on Linux.
    static bool offered() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        return true;
#else
        return !appImage().isEmpty();
#endif
    }

    // The .AppImage file this program was started from, or empty.
    static QString appImage() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        return QString();
#else
        const QString a = qEnvironmentVariable("APPIMAGE");
        return !a.isEmpty() && QFileInfo(a).isFile() ? QFileInfo(a).absoluteFilePath() : QString();
#endif
    }

    // The folder the program is in, as the user sees it: on macOS the folder that holds the .app bundle, for an
    // AppImage the folder that holds the .AppImage file.
    static QString programFolder() {
        const QString ai = appImage();
        if (!ai.isEmpty()) return QFileInfo(ai).absolutePath();
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
        portableFlag() = (offered() || !folder.isEmpty()) && QFileInfo(f).isFile();
        if (portableFlag()) QSettings::setPath(IniFormat, UserScope, QFileInfo(f).absolutePath());
    }
    static bool portable() { return portableFlag(); }

    // Makes the portable file in the program's folder with a copy of the current settings, and uses it from now on.
    static bool createPortableFile(QString *err) {
        const QString f = portableFile();
        if (!offered()) { *err = "an installed program keeps its settings in the system's place"; return false; }
        if (portable()) { *err = "the settings are already kept in " + QDir::toNativeSeparators(f); return false; }
        const bool existed = QFileInfo::exists(f);
        // Make the file first: QSettings writes nothing when there are no settings yet (a first run), and the
        // file's existence is what switches portable mode on. This also says plainly why it can't be written.
        QFile touch(f);
        if (!touch.open(QIODevice::WriteOnly | QIODevice::Append)) {
            *err = "could not write " + QDir::toNativeSeparators(f) + ": " + touch.errorString();
            return false;
        }
        touch.close();
        {
            QSettings ini(f, IniFormat);
            AppSettings current;
            for (const QString &k : current.allKeys()) ini.setValue(k, current.value(k));
            ini.sync();
            if (ini.status() != NoError) {
                if (!existed) QFile::remove(f);
                *err = "could not write the settings into " + QDir::toNativeSeparators(f);
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

    // Adds File > Create local config file to a menu, where portable mode exists.
    static void addMenuItem(QMenu *menu, QWidget *parent) {
        if (offered()) menu->addAction("Create &local config file...", parent, [parent] { createPortableFileDialog(parent); });
    }

    // Where the settings are, in words, for Help > About.
    static QString where() {
        AppSettings s;
        if (portable()) return "Settings are kept beside the program (portable): " + QDir::toNativeSeparators(s.fileName());
        QString w = "Settings: " + QDir::toNativeSeparators(s.fileName());
        if (offered())
            w += QString(" (File > Create local config file keeps them beside the program instead, in %1.ini)")
                     .arg(QCoreApplication::organizationName());
        return w;
    }

private:
    static bool &portableFlag() { static bool p = false; return p; }
};
