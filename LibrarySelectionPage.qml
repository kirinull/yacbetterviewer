import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import net.blumia.pineapple.comic.reader
import net.blumia.pineapple.comic.reader.libraryitem

Control {
    anchors.fill: parent
    padding: 5
    contentItem: ColumnLayout {
        Label {
            text: "Libraries"
            font.pixelSize: 20
        }
        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: DelegateModel {
                model: AppController.librariesModel
                delegate: ItemDelegate {
                    text: model.display
                    width: ListView.view.width
                    onClicked: function() {
                        AppController.selectedLibraryId = model.libraryId
                    }
                }
            }
        }

        // StackLayout 会提前创建页面，onCompleted 时可能还没连上服务器，
        // 那次请求必然失败且不会重试。改为每次页面可见时确认已加载。
        function ensureLibraries() {
            if (AppController.connectionState === AppController.Connected) {
                AppController.updateLibraries()
            }
        }

        Component.onCompleted: ensureLibraries()
        onVisibleChanged: {
            if (visible) {
                ensureLibraries()
            }
        }
    }

    Connections {
        target: AppController
        onLibrariesLoaded: function(count) {
            // 只有一个库时自动进入，省一次点击
            if (count === 1) {
                const model = AppController.librariesModel
                const idx = model.index(0, 0)
                AppController.selectedLibraryId = model.data(idx, LibraryItem.IdRole)
            }
        }
    }
}
