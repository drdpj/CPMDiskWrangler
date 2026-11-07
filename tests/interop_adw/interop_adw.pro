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
CORE = $$PWD/../../core                # DiskWranglerCore, this program's copy: Acorn's dragout.cpp is built on it
INCLUDEPATH += $$ADW_SRC $$CORE
SOURCES += interop_adw.cpp $$ADW_SRC/dragout.cpp $$ADW_SRC/adfs.cpp \
           $$CORE/diskwrangler/xfer/retrofiles.cpp $$CORE/diskwrangler/ui/dragoutmime.cpp
HEADERS += $$ADW_SRC/dragout.h $$CORE/diskwrangler/ui/dragoutmime.h
include($$CORE/diskwrangler-device.pri)    # Acorn's adfs.cpp sits on the core's device layer (DiskWranglerCore v0.3.0 on)
exists($$ADW_SRC/dfs.cpp) {
    SOURCES += $$ADW_SRC/dfs.cpp $$ADW_SRC/dosfs.cpp       # Acorn's own DFS and FAT (before DiskWranglerCore v0.4.0)
} else {
    include($$CORE/diskwrangler-dfs.pri)                     # DiskWranglerCore's modules (v0.4.0 on)
    include($$CORE/diskwrangler-fat.pri)
}
