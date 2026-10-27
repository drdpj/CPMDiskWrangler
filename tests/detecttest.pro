# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT -= gui
CONFIG += c++11 console
CONFIG -= app_bundle
TARGET = detecttest
SOURCES += detecttest.cpp ../diskimage.cpp ../lzhuf.cpp ../cpmformat.cpp ../cpmfs.cpp ../detect.cpp
HEADERS += ../diskimage.h ../cpmformat.h ../cpmfs.h ../detect.h
RESOURCES += ../resources.qrc
