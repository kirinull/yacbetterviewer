import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
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
    // before figure out how to handle back button on Android...
    onClosing: function(close) {
        if (Qt.platform.os === "android") {
            close.accepted = false;
        }
    }
}
