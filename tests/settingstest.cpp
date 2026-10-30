// Shared, unchanged, by Acorn Disk Wrangler and CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
// settingstest: where settings live on Linux (appsettings.h). Run offscreen; needs nothing else.
//  - installed (no $APPIMAGE): no portable mode - no menu item, an ini beside the program is ignored, creating fails
//  - an AppImage: the folder is the one holding the .AppImage file; creating the ini there copies the settings,
//    switches to it at once, and is found again at the next start
//  - $APPIMAGE naming a file that isn't there: treated as installed
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QTemporaryDir>
#include <cstdio>
#include "../appsettings.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", msg); ++failures; } else printf("  ok: %s\n", msg); } while (0)

static int menuItems() { QMenu m; AppSettings::addMenuItem(&m, nullptr); return m.actions().size(); }

int main(int argc, char **argv) {
    QTemporaryDir tmp;
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(tmp.path() + "/config"));     // keep away from the real settings
    qunsetenv("APPIMAGE");
    QApplication app(argc, argv);
    QApplication::setOrganizationName("wranglersettingstest");
    QApplication::setApplicationName("wranglersettingstest");
    const QString ini = QApplication::organizationName() + ".ini";

    printf("== installed (no AppImage)\n");
    QFile stray(QApplication::applicationDirPath() + "/" + ini);     // an ini beside the program must be ignored
    bool made = stray.open(QIODevice::WriteOnly);
    stray.close();
    AppSettings::init();
    CHECK(!AppSettings::offered(), "portable mode is not offered");
    CHECK(menuItems() == 0, "no File menu item");
    CHECK(!made || !AppSettings::portable(), "an ini beside the program is ignored");
    { AppSettings s; s.setValue("backup", false); }
    CHECK(AppSettings().fileName().startsWith(tmp.path() + "/config/"), "settings are in the system's place");
    QString err;
    CHECK(!AppSettings::createPortableFile(&err), "creating a local config file is refused");
    if (made) QFile::remove(stray.fileName());

    printf("== the Windows installer's folder (unins000.dat beside the program)\n");
    {
        QTemporaryDir dir;
        CHECK(!AppSettings::installedHere(dir.path()), "a folder without the uninstaller is an unzipped copy");
        QFile exe(dir.path() + "/unins000.exe");
        exe.open(QIODevice::WriteOnly);
        exe.close();
        CHECK(!AppSettings::installedHere(dir.path()), "the uninstaller's .exe alone isn't enough");
        QFile dat(dir.path() + "/unins001.dat");
        dat.open(QIODevice::WriteOnly);
        dat.close();
        CHECK(AppSettings::installedHere(dir.path()), "with its log (unins001.dat) it was installed");
    }

    printf("== AppImage that isn't there\n");
    qputenv("APPIMAGE", QFile::encodeName(tmp.path() + "/gone.AppImage"));
    CHECK(!AppSettings::offered(), "a missing .AppImage counts as installed");

    printf("== AppImage, first run (nothing saved yet)\n");
    {
        QTemporaryDir fresh;
        { AppSettings s; s.clear(); }                       // no settings at all (Qt fixes the config folder at first use)
        QFile img(fresh.path() + "/First.AppImage");
        img.open(QIODevice::WriteOnly);
        img.close();
        qputenv("APPIMAGE", QFile::encodeName(img.fileName()));
        AppSettings::init();
        CHECK(AppSettings().allKeys().isEmpty(), "no settings saved yet");
        CHECK(AppSettings::createPortableFile(&err), "creating the local config file still works");
        CHECK(QFileInfo::exists(fresh.path() + "/" + ini) && AppSettings::portable(), "the (empty) ini is made and used");
        AppSettings::init();
        CHECK(AppSettings::portable(), "and found at the next start");
        qunsetenv("APPIMAGE");
        AppSettings::init();
        { AppSettings s; s.setValue("backup", false); }     // as the first section left it
    }

    printf("== a folder that can't be written\n");
    {
        QTemporaryDir ro;
        QFile img(ro.path() + "/Locked.AppImage");
        img.open(QIODevice::WriteOnly);
        img.close();
        QFile::setPermissions(ro.path(), QFile::ReadOwner | QFile::ExeOwner);
        qputenv("APPIMAGE", QFile::encodeName(img.fileName()));
        AppSettings::init();
        bool writable = QFileInfo(ro.path()).isWritable();      // root can write anywhere: then this can't be tested
        CHECK(writable || (!AppSettings::createPortableFile(&err) && err.contains("could not write")),
              "refused, saying it can't write there");
        CHECK(writable || !AppSettings::portable(), "and the system's settings are still used");
        QFile::setPermissions(ro.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        qunsetenv("APPIMAGE");
        AppSettings::init();
    }

    printf("== AppImage\n");
    QDir().mkpath(tmp.path() + "/stick");
    const QString image = tmp.path() + "/stick/Wrangler.AppImage";
    QFile f(image);
    CHECK(f.open(QIODevice::WriteOnly), "made a stand-in .AppImage");
    f.close();
    qputenv("APPIMAGE", QFile::encodeName(image));
    AppSettings::init();
    CHECK(AppSettings::offered(), "portable mode is offered");
    CHECK(menuItems() == 1, "File menu has the item");
    CHECK(AppSettings::programFolder() == tmp.path() + "/stick", "the folder is the .AppImage's, not the mount");
    CHECK(!AppSettings::portable(), "not portable until the ini exists");
    CHECK(AppSettings::createPortableFile(&err), "creating the local config file works");
    CHECK(QFileInfo::exists(tmp.path() + "/stick/" + ini), "the ini is beside the .AppImage");
    CHECK(AppSettings::portable() && AppSettings().fileName() == tmp.path() + "/stick/" + ini, "and is used at once");
    CHECK(AppSettings().value("backup", true).toBool() == false, "the existing settings were copied into it");
    { AppSettings s; s.setValue("lastDir", "/media/stick"); }
    AppSettings::init();                                            // as at the next start
    CHECK(AppSettings::portable() && AppSettings().value("lastDir").toString() == "/media/stick", "found again at the next start");
    CHECK(!AppSettings::createPortableFile(&err), "a second create says it's already there");

    printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
