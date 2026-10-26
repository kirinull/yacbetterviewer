import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import net.blumia.pineapple.comic.reader

ApplicationWindow {
    id: window
    width: 480
    height: 640
    visible: true
    title: qsTr("YACReader Client")

    Loader {
        anchors.fill: parent
        source: {
            if (AppController.connectionState !== AppController.Connected) return "ConnectServerPage.qml"
            if (AppController.selectedLibraryId === -1) return "LibrarySelectionPage.qml"
            if (AppController.selectedComicId === "") return "ComicSelectionPage.qml"
            return "ComicOverviewPage.qml"
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
