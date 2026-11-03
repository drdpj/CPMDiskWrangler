# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
# qmake ADW=/path/to/AcornDiskWrangler interop_adw.pro
isEmpty(ADW): error("set ADW to the Acorn Disk Wrangler source folder: qmake ADW=/path/to/AcornDiskWrangler")
QT += widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = interop_adw
ADW_SRC = $$ADW/src                    # its sources (in the folder itself before 1.0.0)
!exists($$ADW_SRC/dragout.cpp): ADW_SRC = $$ADW
INCLUDEPATH += $$ADW_SRC
SOURCES += interop_adw.cpp $$ADW_SRC/dragout.cpp $$ADW_SRC/adfs.cpp $$ADW_SRC/dfs.cpp $$ADW_SRC/dosfs.cpp
HEADERS += $$ADW_SRC/dragout.h
