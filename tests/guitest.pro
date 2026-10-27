QT += widgets
CONFIG += c++11 console
CONFIG -= app_bundle
TARGET = guitest
SOURCES += guitest.cpp ../mainwindow.cpp ../formatdialog.cpp ../newdisc.cpp ../viewer.cpp ../viewtext.cpp ../icon.cpp \
           ../dragout.cpp ../diskimage.cpp ../lzhuf.cpp ../cpmformat.cpp ../cpmfs.cpp ../detect.cpp
HEADERS += ../mainwindow.h ../mainwindow_p.h ../formatdialog.h ../newdisc.h ../viewer.h ../viewtext.h ../icon.h \
           ../dragout.h ../diskimage.h ../cpmformat.h ../cpmfs.h ../detect.h
RESOURCES += ../resources.qrc
