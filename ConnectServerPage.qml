import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import net.blumia.pineapple.comic.reader

Control {
    anchors.fill: parent
    padding: 5
    contentItem: ColumnLayout {
        Label {
            text: "Pineapple Comic Reader"
            font.pixelSize: 20
        }
        Item {
            Layout.fillHeight: true
            Layout.verticalStretchFactor: 2
        }
        Label {
            text: "YACReader Library Server URL:"
        }
        TextField {
            id: baseUrlEdit
            Layout.fillWidth: true
            placeholderText: "e.g. http://192.168.123.123:8080"
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
            enabled: AppController.connectionState === AppController.NotConnected
        }
        Label {
            text: AppController.lastErrorMessage
        }
        Button {
            Layout.fillWidth: true
            text: AppController.connectionState === AppController.NotConnected ? "Connect" : "Connecting"
            enabled: baseUrlEdit.enabled
            onClicked: function() {
                settings.setValue("server_url", baseUrlEdit.text)
                AppController.connectServer(baseUrlEdit.text)
            }
        }
        Item {
            Layout.fillHeight: true
            Layout.verticalStretchFactor: 3
        }
    }

    Settings {
        id: settings
        Component.onCompleted: function() {
            let srvUrl = settings.value("server_url", "")
            if (srvUrl !== "") {
                baseUrlEdit.text = srvUrl
            }
        }
    }
}
