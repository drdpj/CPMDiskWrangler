QT += widgets
CONFIG += c++17
TARGET = cpmdiskwrangler
TEMPLATE = app
win32:RC_ICONS = icons/cpmdiskwrangler.ico      # qmake generates the .rc for us
macx {
    ICON = icons/cpmdiskwrangler.icns
    QMAKE_TARGET_BUNDLE_PREFIX = io.github.drdpj
}
unix:!macx {
    isEmpty(PREFIX): PREFIX = /usr/local
    target.path = $$PREFIX/bin
    desktop.files = cpmdiskwrangler.desktop
    desktop.path = $$PREFIX/share/applications
    svgicon.files = icons/cpmdiskwrangler.svg
    svgicon.path = $$PREFIX/share/icons/hicolor/scalable/apps
    INSTALLS += target desktop svgicon
    for(s, $$list(16 24 32 48 64 128 256 512)) {
        icon$${s}.path = $$PREFIX/share/icons/hicolor/$${s}x$${s}/apps
        icon$${s}.extra = $(INSTALL_FILE) $$PWD/icons/cpmdiskwrangler-$${s}.png $(INSTALL_ROOT)$$PREFIX/share/icons/hicolor/$${s}x$${s}/apps/cpmdiskwrangler.png
        INSTALLS += icon$${s}
    }
}
QMAKE_TARGET_PRODUCT = CP/M Disk Wrangler
QMAKE_TARGET_DESCRIPTION = CP/M Disk Wrangler
QMAKE_TARGET_COPYRIGHT = Copyright (C) 2026 Daniel Jameson. Licensed under the GNU GPL v3 or later.
SOURCES += src/main.cpp src/mainwindow.cpp src/formatdialog.cpp src/newdisc.cpp src/natchars.cpp src/natsets.cpp src/viewer.cpp src/viewtext.cpp src/icon.cpp src/dragout.cpp src/hostpolicy.cpp \
           src/cpmformat.cpp src/cpmfs.cpp src/torchfs.cpp src/detect.cpp src/cpmvolume.cpp
HEADERS += src/mainwindow.h src/mainwindow_p.h src/formatdialog.h src/newdisc.h src/natchars.h src/natsets.h src/viewer.h src/viewtext.h src/icon.h src/dragout.h src/hostpolicy.h \
           src/cpmformat.h src/cpmfs.h src/torchfs.h src/detect.h src/cpmvolume.h
RESOURCES += src/resources.qrc
RESOURCES += src/licences.qrc                      # Help > Licences: LICENSE, notices, the LGPL for Qt
exists(licences-bundled.qrc): RESOURCES += licences-bundled.qrc   # a release build's library licences (ci/)
!include(core/diskwranglercore.pri): error("core/ is empty: run 'git submodule update --init'")
include(core/diskwrangler-dfs.pri)                # DiskWranglerCore's DFS module: Acorn Z80 CP/M's DFS catalogue
include(version.pri)                              # APP_VERSION from git describe (Help > About)
