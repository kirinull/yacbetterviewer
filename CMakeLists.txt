cmake_minimum_required(VERSION 3.16)

project(yacbetterviewer VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick Network)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(pcomic
    main.cpp
    appcontroller.cpp
    appcontroller.h
    coverimageprovider.cpp
    coverimageprovider.h
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

# 从删除服务的 config.json 读 token，编译时注入。
# 服务地址不需要配置：从 YACReader 地址推导（同主机、端口 8894）。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/delete-service/config.json" DELETE_CFG)
string(JSON DELETE_TOKEN ERROR_VARIABLE _tok_err GET "${DELETE_CFG}" token)
if(_tok_err)
    message(WARNING "无法从 delete-service/config.json 读取 token，删除功能将不可用")
    set(DELETE_TOKEN "")
endif()

target_compile_definitions(pcomic PRIVATE
    PINEAPPLE_DELETE_TOKEN="${DELETE_TOKEN}"
    PINEAPPLE_DELETE_PORT=8894
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

    # 尝试过用 EXCLUDE 剔除未使用的样式插件（FluentWinUI3 / Imagine，约 2MB），
    # 但 qt_import_plugins 管不到 QML 模块插件——那部分走 QML 依赖树自动打包，
    # 写了不生效。改用构建后剔除 .so 重打包，会导致 Qt 的 JNI 方法注册失败
    # （UnsatisfiedLinkError: QtNative.updateApplicationState），App 启动即崩。
    # 因此放弃：收益 5.5% 不值得冒崩溃风险。
)

include(GNUInstallDirs)
install(TARGETS pcomic
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 自定义 Android 资源（启动图标）。
# android/res/mipmap-*/ic_launcher.png 会覆盖 Qt 模板里的默认图标。
set_property(TARGET pcomic APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/android
)
