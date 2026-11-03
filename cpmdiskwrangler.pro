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
SOURCES += src/main.cpp src/mainwindow.cpp src/formatdialog.cpp src/newdisc.cpp src/natchars.cpp src/viewer.cpp src/viewtext.cpp src/icon.cpp src/dragout.cpp src/licences.cpp \
           src/diskimage.cpp src/lzhuf.cpp src/cpmformat.cpp src/cpmfs.cpp src/torchfs.cpp src/detect.cpp
HEADERS += src/appsettings.h src/buttonbar.h src/fileopen.h src/mainwindow.h src/mainwindow_p.h src/formatdialog.h src/newdisc.h src/natchars.h src/viewer.h src/viewtext.h src/icon.h src/dragout.h src/licences.h \
           src/diskimage.h src/cpmformat.h src/cpmfs.h src/torchfs.h src/detect.h
RESOURCES += src/resources.qrc
RESOURCES += src/licences.qrc                      # Help > Licences: LICENSE, notices, the LGPL for Qt
exists(licences-bundled.qrc): RESOURCES += licences-bundled.qrc   # a release build's library licences (ci/)
include(version.pri)                              # APP_VERSION from git describe (Help > About)
