# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT += widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = screenshot
SOURCES += screenshot.cpp ../src/mainwindow.cpp ../src/formatdialog.cpp ../src/newdisc.cpp ../src/natchars.cpp ../src/natsets.cpp ../src/viewer.cpp ../src/viewtext.cpp ../src/icon.cpp \
           ../src/dragout.cpp ../src/hostpolicy.cpp ../src/cpmformat.cpp ../src/cpmfs.cpp ../src/torchfs.cpp ../src/detect.cpp ../src/cpmvolume.cpp
HEADERS += ../src/mainwindow.h ../src/mainwindow_p.h ../src/formatdialog.h ../src/newdisc.h ../src/natchars.h ../src/natsets.h ../src/viewer.h ../src/viewtext.h ../src/icon.h \
           ../src/dragout.h ../src/hostpolicy.h ../src/cpmformat.h ../src/cpmfs.h ../src/torchfs.h ../src/detect.h ../src/cpmvolume.h
RESOURCES += ../src/resources.qrc
!include(../core/diskwranglercore.pri): error("core/ is empty: run 'git submodule update --init'")
include(../core/diskwrangler-dfs.pri)                # DiskWranglerCore's DFS module (step 4)
