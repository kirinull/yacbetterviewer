import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import net.blumia.pineapple.comic.reader

Item {
    anchors.fill: parent

    Drawer {
        id: drawer
        width: Math.min(0.66 * parent.width, 320)
        height: parent.height

        ColumnLayout {
            anchors.fill: parent
            ListView {
                id: listView
                clip: true
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: AppController.foldersModel
                delegate: ItemDelegate {
                    text: model.display
                    width: listView.width
                    onClicked: function() {
                        AppController.updateComicsInFolder(model.folderId)
                        drawer.close()
                    }
                }
            }
            Button {
                Layout.fillWidth: true
                text: "Select Library"
                onClicked: function() {
                    AppController.selectedLibraryId = -1
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 4

            ToolButton {
                flat: true
                icon.name: "folder-open"
                onClicked: function() {
                    drawer.open();
                }
            }
            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: "搜索标题"
                onTextChanged: AppController.setFilter(text)
            }
            Label {
                text: `${AppController.filteredCount}/${AppController.totalComicCount}`
                font.pixelSize: 12
                opacity: 0.7
            }
        }

        GridView {
            id: gridView
            clip: true
            cellHeight: cellWidth * 3 / 2
            cellWidth: width / Math.max(Math.floor(width / (65 * 2)), 1)
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: AppController.comicsModel
            delegate: Item {
                width: GridView.view.cellWidth
                height: GridView.view.cellHeight

                Image {
                    id: cover
                    anchors.fill: parent
                    source: AppController.coverImageSource(model.hash)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    retainWhileLoading: true
                }

                // 阅读进度角标
                Rectangle {
                    visible: model.currentPage > 0
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 4
                    height: progressLabel.implicitHeight + 4
                    width: progressLabel.implicitWidth + 10
                    radius: 4
                    color: "#CC000000"

                    Label {
                        id: progressLabel
                        anchors.centerIn: parent
                        color: "white"
                        font.pixelSize: 10
                        text: `${model.currentPage}/${model.pageCount}`
                    }
                }

                TapHandler {
                    onTapped: AppController.selectedComicId = model.comicId
                }
            }
            ScrollBar.vertical: ScrollBar { }
        }

        Component.onCompleted: function() {
            AppController.updateComicsInFolder()
        }
    }
}
