# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT += widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = chartest
SOURCES += chartest.cpp ../src/natchars.cpp ../src/cpmformat.cpp ../src/cpmfs.cpp ../src/torchfs.cpp ../src/detect.cpp
HEADERS += ../src/natchars.h ../src/diskimage.h ../src/cpmformat.h ../src/cpmfs.h ../src/torchfs.h ../src/detect.h
RESOURCES += ../src/resources.qrc
!include(../core/diskwrangler-device.pri): error("core/ is empty: run 'git submodule update --init'")   # the disc image containers
