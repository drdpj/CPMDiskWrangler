# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
# qmake ADW=/path/to/AcornDiskWrangler interop_adw.pro
isEmpty(ADW): error("set ADW to the Acorn Disk Wrangler source folder: qmake ADW=/path/to/AcornDiskWrangler")
QT += widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = interop_adw
ADW_SRC = $$ADW/src                    # its sources (in the folder itself before 1.0.0)
!exists($$ADW_SRC/hostpolicy.cpp): ADW_SRC = $$ADW
CORE = $$PWD/../../core                # DiskWranglerCore, this program's copy: Acorn's host naming is built on it
INCLUDEPATH += $$ADW_SRC $$CORE
SOURCES += interop_adw.cpp $$ADW_SRC/adfs.cpp
SOURCES += $$ADW_SRC/hostpolicy.cpp
HEADERS += $$ADW_SRC/hostpolicy.h
include($$CORE/diskwrangler-device.pri)    # Acorn's adfs.cpp sits on the core's device layer (DiskWranglerCore v0.3.0 on)
include($$CORE/diskwrangler-dfs.pri)
include($$CORE/diskwrangler-fat.pri)
include($$CORE/diskwrangler-xfer.pri)
