QT -= gui
CONFIG += c++11 console
CONFIG -= app_bundle
TARGET = fstest
SOURCES += fstest.cpp ../diskimage.cpp ../lzhuf.cpp ../cpmformat.cpp ../cpmfs.cpp ../detect.cpp
HEADERS += ../diskimage.h ../cpmformat.h ../cpmfs.h ../detect.h
RESOURCES += ../resources.qrc
