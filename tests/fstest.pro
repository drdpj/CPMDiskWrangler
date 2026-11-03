# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT -= gui
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = fstest
SOURCES += fstest.cpp ../src/diskimage.cpp ../src/lzhuf.cpp ../src/cpmformat.cpp ../src/cpmfs.cpp ../src/torchfs.cpp ../src/detect.cpp
HEADERS += ../src/diskimage.h ../src/cpmformat.h ../src/cpmfs.h ../src/torchfs.h ../src/detect.h
RESOURCES += ../src/resources.qrc
