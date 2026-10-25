import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import net.blumia.pineapple.comic.reader

Item {
    id: root
    anchors.fill: parent

    // 跟随系统主题取前景色，浅色/深色主题下都能保证对比度
    SystemPalette {
        id: sysPalette
        colorGroup: SystemPalette.Active
    }

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
                color: sysPalette.windowText
                opacity: 0.75
            }
        }

        GridView {
            id: gridView
            clip: true
            // 每格 = 封面(3:2) + 两行标题（固定预留 34px）
            cellWidth: width / Math.max(Math.floor(width / (70 * 2)), 1)
            cellHeight: cellWidth * 3 / 2 + 34
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: AppController.comicsModel
            delegate: Item {
                id: cell
                width: GridView.view.cellWidth
                height: GridView.view.cellHeight

                Image {
                    id: cover
                    width: parent.width
                    height: parent.width * 3 / 2
                    source: AppController.coverImageSource(model.hash)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    retainWhileLoading: true
                }

                // 阅读进度角标
                Rectangle {
                    visible: model.currentPage > 0
                    anchors.right: cover.right
                    anchors.bottom: cover.bottom
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

                Label {
                    id: titleLabel
                    anchors.top: cover.bottom
                    anchors.topMargin: 4
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 2
                    anchors.rightMargin: 2
                    text: model.display
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    lineHeight: 1.15
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    color: sysPalette.windowText
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
