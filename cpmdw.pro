# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
# cpmdw: the command-line program (QtCore only: no window, no display needed). Build it beside the GUI program:
#     mkdir b && cd b && qmake ../cpmdw.pro && make
QT = core
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = cpmdw
TEMPLATE = app
win32 {                                            # version-info strings in the .exe (Explorer: file Properties > Details)
    QMAKE_TARGET_PRODUCT = CP/M Disk Wrangler
    QMAKE_TARGET_DESCRIPTION = cpmdw, the command-line program of CP/M Disk Wrangler
    QMAKE_TARGET_COPYRIGHT = Copyright (C) 2026 Daniel Jameson. Licensed under the GNU GPL v3 or later.
}
unix:!macx {
    isEmpty(PREFIX): PREFIX = /usr/local
    target.path = $$PREFIX/bin
    INSTALLS += target
}
RESOURCES += src/resources.qrc                      # the built-in and cpmtools diskdefs
SOURCES += src/cli/main.cpp src/cli/backend.cpp src/cpmformat.cpp src/cpmfs.cpp src/torchfs.cpp src/detect.cpp src/cpmvolume.cpp \
           src/natsets.cpp src/hostpolicy.cpp
HEADERS += src/cli/backend.h src/cpmformat.h src/cpmstore.h src/cpmfs.h src/torchfs.h src/detect.h src/cpmvolume.h src/natsets.h src/hostpolicy.h src/diskimage.h
!exists(core/diskwrangler/cli/commands.cpp): error("core/ is empty or too old: run 'git submodule update --init'")
INCLUDEPATH += core
include(core/diskwrangler-dfs.pri)                # the DFS module: Acorn Z80 CP/M's DFS catalogue
include(core/diskwrangler-cli.pri)
include(version.pri)
