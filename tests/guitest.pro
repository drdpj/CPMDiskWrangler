# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT += widgets testlib
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = guitest
SOURCES += guitest.cpp ../src/mainwindow.cpp ../src/licences.cpp ../src/formatdialog.cpp ../src/newdisc.cpp ../src/natchars.cpp ../src/viewer.cpp ../src/viewtext.cpp ../src/icon.cpp \
           ../src/dragout.cpp ../src/diskimage.cpp ../src/lzhuf.cpp ../src/cpmformat.cpp ../src/cpmfs.cpp ../src/torchfs.cpp ../src/detect.cpp
HEADERS += ../src/fileopen.h ../src/mainwindow.h ../src/mainwindow_p.h ../src/formatdialog.h ../src/newdisc.h ../src/natchars.h ../src/viewer.h ../src/viewtext.h ../src/icon.h \
           ../src/dragout.h ../src/diskimage.h ../src/cpmformat.h ../src/cpmfs.h ../src/torchfs.h ../src/detect.h
RESOURCES += ../src/resources.qrc
