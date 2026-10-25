cmake_minimum_required(VERSION 3.16)

project(pineapple-comic-reader VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick Network)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(pcomic
    main.cpp
    appcontroller.cpp
    appcontroller.h
    dataitems/libraryitem.cpp
    dataitems/libraryitem.h
    dataitems/comicitem.cpp
    dataitems/comicitem.h
    dataitems/folderitem.cpp
    dataitems/folderitem.h
)

qt_add_qml_module(pcomic
    URI net.blumia.pineapple.comic.reader
    VERSION 1.0
    QML_FILES
        Main.qml
        ConnectServerPage.qml
        LibrarySelectionPage.qml
        ComicSelectionPage.qml
        ComicOverviewPage.qml
        ComicViewer.qml
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(pcomic PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER net.blumia.pineapple.comic.reader
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    QT_ANDROID_PACKAGE_NAME "net.blumia.pineapple.comic.reader"
)

target_link_libraries(pcomic
    PRIVATE Qt6::Quick Qt6::Network
)

qt_import_plugins(pcomic
    INCLUDE_BY_TYPE imageformats
        # These are the ones that ship by default if we don't use `qt_import_plugins`
        Qt::QGifPlugin Qt::QIcoPlugin Qt::QSvgPlugin Qt::QJpegPlugin
        # We need webp support from Qt imageformats module.
        Qt::QWebpPlugin
)

include(GNUInstallDirs)
install(TARGETS pcomic
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
