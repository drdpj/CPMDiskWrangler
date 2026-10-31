# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT += widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = chartest
SOURCES += chartest.cpp ../natchars.cpp ../diskimage.cpp ../lzhuf.cpp ../cpmformat.cpp ../cpmfs.cpp ../torchfs.cpp ../detect.cpp
HEADERS += ../natchars.h ../diskimage.h ../cpmformat.h ../cpmfs.h ../torchfs.h ../detect.h
RESOURCES += ../resources.qrc
