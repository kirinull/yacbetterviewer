import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import net.blumia.pineapple.comic.reader

ApplicationWindow {
    id: window
    width: 480
    height: 640
    visible: true
    title: qsTr("yacbetterviewer")

    // 关键：列表页必须常驻。
    // 之前用 Loader 切页，从详情返回时 ComicSelectionPage 被销毁重建，
    // 滚动位置无论怎么记都难还原 —— 像素和索引在数据重新加载后都会错位。
    // 改成 StackLayout 后各页常驻，滚动位置天然保留。
    //
    // 详情页仍用 Loader 按需加载：它依赖 selectedComicId，
    // 常驻会在没有选中时也初始化，索引无效会出问题。
    StackLayout {
        anchors.fill: parent
        currentIndex: {
            if (AppController.connectionState !== AppController.Connected) return 0
            if (AppController.selectedLibraryId === -1) return 1
            if (AppController.selectedComicId === "") return 2
            return 3
        }

        ConnectServerPage { }
        LibrarySelectionPage { }
        ComicSelectionPage { }

        Item {
            Loader {
                anchors.fill: parent
                active: AppController.selectedComicId !== ""
                source: "ComicOverviewPage.qml"
            }
        }
    }

    // Android 的返回键与全面屏返回手势都走 closing。
    // 这里按层级逐级返回，最外层才真正退出。
    onClosing: function(close) {
        if (Qt.platform.os !== "android") return

        close.accepted = false

        if (AppController.selectedComicOpened) {
            AppController.closeComic()
        } else if (AppController.selectedComicId !== "") {
            AppController.selectedComicId = ""
        } else if (AppController.authorFilter !== "") {
            AppController.clearAuthorFilter()
        } else if (AppController.selectedLibraryId !== -1) {
            AppController.selectedLibraryId = -1
        } else {
            Qt.exit(0)
        }
    }
}
