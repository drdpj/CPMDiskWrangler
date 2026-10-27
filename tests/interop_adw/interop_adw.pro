# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
# qmake ADW=/path/to/AcornDiskWrangler interop_adw.pro
isEmpty(ADW): error("set ADW to the Acorn Disk Wrangler source folder: qmake ADW=/path/to/AcornDiskWrangler")
QT += widgets
CONFIG += c++11 console
CONFIG -= app_bundle
TARGET = interop_adw
INCLUDEPATH += $$ADW
SOURCES += interop_adw.cpp $$ADW/dragout.cpp $$ADW/adfs.cpp $$ADW/dfs.cpp $$ADW/dosfs.cpp
HEADERS += $$ADW/dragout.h
