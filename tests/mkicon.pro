# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
QT += gui
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = mkicon
SOURCES += mkicon.cpp ../src/icon.cpp
HEADERS += ../src/icon.h
