# CP/M Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
cmake_minimum_required(VERSION 3.16)   # Qt 6 needs 3.16
project(cpmdiskwrangler LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# Qt 6 if it is installed, otherwise Qt 5 (5.15); -DUSE_QT5=ON builds with Qt 5 even when Qt 6 is there.
option(USE_QT5 "Build with Qt 5 instead of Qt 6" OFF)
if(USE_QT5)
    find_package(QT NAMES Qt5 REQUIRED COMPONENTS Widgets)
else()
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
endif()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
include(GNUInstallDirs)
include(version.cmake)         # APP_VERSION from git describe (Help > About)
set(CPMDW_RC "")
if(WIN32)
    enable_language(RC)
    set(CPMDW_RC cpmdiskwrangler.rc)
endif()
set(CPMDW_ICNS "")
if(APPLE)
    set(CPMDW_ICNS icons/cpmdiskwrangler.icns)
    set_source_files_properties(${CPMDW_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()
add_executable(cpmdiskwrangler MACOSX_BUNDLE ${CPMDW_RC} ${CPMDW_ICNS} resources.qrc licences.qrc licences.cpp licences.h
    main.cpp mainwindow.cpp formatdialog.cpp newdisc.cpp viewer.cpp viewtext.cpp icon.cpp dragout.cpp
    diskimage.cpp lzhuf.cpp cpmformat.cpp cpmfs.cpp detect.cpp
    appsettings.h buttonbar.h mainwindow.h mainwindow_p.h formatdialog.h newdisc.h viewer.h viewtext.h icon.h dragout.h
    diskimage.h cpmformat.h cpmfs.h detect.h)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/licences-bundled.qrc)   # a release build's library licences (ci/)
    target_sources(cpmdiskwrangler PRIVATE licences-bundled.qrc)
endif()
target_compile_definitions(cpmdiskwrangler PRIVATE APP_VERSION="${APP_VERSION}")
if(APP_VERSION_NUMBER)
    set_target_properties(cpmdiskwrangler PROPERTIES MACOSX_BUNDLE_SHORT_VERSION_STRING ${APP_VERSION_NUMBER}
                                                 MACOSX_BUNDLE_BUNDLE_VERSION ${APP_VERSION_NUMBER})
endif()
target_link_libraries(cpmdiskwrangler Qt${QT_VERSION_MAJOR}::Widgets)
set_target_properties(cpmdiskwrangler PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "CP/M Disk Wrangler"
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.drdpj.cpmdiskwrangler
    MACOSX_BUNDLE_ICON_FILE cpmdiskwrangler.icns)
install(TARGETS cpmdiskwrangler RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} BUNDLE DESTINATION .)
if(UNIX AND NOT APPLE)
    install(FILES cpmdiskwrangler.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES icons/cpmdiskwrangler.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    foreach(s 16 24 32 48 64 128 256 512)
        install(FILES icons/cpmdiskwrangler-${s}.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${s}x${s}/apps
                RENAME cpmdiskwrangler.png)
    endforeach()
endif()
