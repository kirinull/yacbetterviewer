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
            text: "yacbetterviewer"
            font.pixelSize: 20
        }
        Item {
            Layout.fillHeight: true
            Layout.verticalStretchFactor: 2
        }
        // 首次使用引导。这个 App 本身不含漫画，只是 YACReader 的前端，
        // 不给提示的话新用户打开只会看到空页面，不知道从哪开始。
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            // 已经有地址（老用户升级）就不再占位，只在首次使用时出现
            visible: baseUrlEdit.text === ""

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 13
                opacity: 0.75
                text: "这是一个阅读客户端，本身不含漫画。需要先在电脑上运行 "
                    + "YACReader Library Server，这里填入它的地址才能连上。"
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                opacity: 0.55
                text: "地址形如 http://192.168.1.10:8080，用电脑的局域网 IP。"
                    + "手机需要和电脑在同一个 Wi-Fi 下。"
            }
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
