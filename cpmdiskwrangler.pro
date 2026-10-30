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
SOURCES += main.cpp mainwindow.cpp formatdialog.cpp newdisc.cpp viewer.cpp viewtext.cpp icon.cpp dragout.cpp licences.cpp \
           diskimage.cpp lzhuf.cpp cpmformat.cpp cpmfs.cpp torchfs.cpp detect.cpp
HEADERS += appsettings.h buttonbar.h fileopen.h mainwindow.h mainwindow_p.h formatdialog.h newdisc.h viewer.h viewtext.h icon.h dragout.h licences.h \
           diskimage.h cpmformat.h cpmfs.h torchfs.h detect.h
RESOURCES += resources.qrc
RESOURCES += licences.qrc                          # Help > Licences: LICENSE, notices, the LGPL for Qt
exists(licences-bundled.qrc): RESOURCES += licences-bundled.qrc   # a release build's library licences (ci/)
include(version.pri)                              # APP_VERSION from git describe (Help > About)
