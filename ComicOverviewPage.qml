import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import net.blumia.pineapple.comic.reader
import net.blumia.pineapple.comic.reader.comicitem

Control {
    id: root
    anchors.fill: parent
    padding: 5

    property bool startReading: AppController.selectedComicOpened
    property bool deleteSucceeded: false

    SystemPalette {
        id: sysPalette
        colorGroup: SystemPalette.Active
    }

    // 必须显式依赖 selectedComicId，否则切到下一本时这里不会重算，
    // pageCount 会继续用旧书的页数。
    property var curComicIndex: {
        const _dep = AppController.selectedComicId
        return AppController.currentComicModelIndex()
    }

    readonly property int comicPageCount: dataByRole(ComicItem.PageCountRole)

    // 本地进度优先，其次服务端
    readonly property int resumePage: {
        const local = AppController.savedProgress(AppController.selectedComicId)
        if (local > 0) return local + 1
        return dataByRole(ComicItem.CurrentPageRole)
    }

    function dataByRole(role) {
        return AppController.comicsModel.data(curComicIndex, role)
    }

    contentItem: ColumnLayout {
        enabled: !root.startReading
        Image {
            Layout.fillWidth: true
            Layout.maximumHeight: Math.min(root.width / 2, root.height / 2)
            fillMode: Image.PreserveAspectFit
            source: AppController.coverImageSource(dataByRole(ComicItem.HashRole))
        }
        Label {
            Layout.preferredWidth: root.width - 10
            font.pixelSize: 18
            font.bold: true
            text: dataByRole(Qt.DisplayRole)
            wrapMode: Text.Wrap
        }
        // 作者：点一下进入该作者的全部作品
        RowLayout {
            Layout.preferredWidth: root.width - 10
            spacing: 6
            visible: AppController.currentAuthor !== ""

            Label {
                text: "作者"
                font.pixelSize: 12
                color: sysPalette.windowText
                opacity: 0.55
            }
            Label {
                id: authorLabel
                Layout.fillWidth: true
                text: AppController.currentAuthor
                font.pixelSize: 14
                color: "#C2185B"
                elide: Text.ElideRight

                TapHandler {
                    onTapped: AppController.filterByAuthor(AppController.currentAuthor)
                }
            }
            Label {
                text: "›"
                font.pixelSize: 16
                color: "#C2185B"
            }
        }

        Label {
            Layout.preferredWidth: root.width - 10
            text: `${root.comicPageCount} pages`
            wrapMode: Text.Wrap
        }
        Label {
            Layout.preferredWidth: root.width - 10
            text: root.resumePage > 0
                  ? `第 ${root.resumePage} 页续读`
                  : "还没开始读"
            wrapMode: Text.Wrap
        }
        Label {
            Layout.preferredWidth: root.width - 10
            text: "Added: " + new Date(Number(`${dataByRole(ComicItem.AddedTimeRole)}000`)).toLocaleString(Qt.locale())
            wrapMode: Text.Wrap
            visible: dataByRole(ComicItem.AddedTimeRole) > 0
        }
        Button {
            Layout.fillWidth: true
            text: root.resumePage > 1 ? "继续阅读" : "开始阅读"
            enabled: root.comicPageCount > 0
            onClicked: function() {
                AppController.openComic()
            }
        }
        Button {
            Layout.fillWidth: true
            text: AppController.isFavorite(AppController.selectedComicId)
                  ? "★ 已收藏（点击取消）"
                  : "☆ 加入收藏"
            onClicked: function() {
                AppController.toggleFavorite(AppController.selectedComicId)
            }
        }
        // 与阅读器面板的"已看完，归档"一致，归档后回列表
        Button {
            Layout.fillWidth: true
            text: "删除本子"
            enabled: AppController.deleteServiceConfigured()
            onClicked: deleteDialog.open()

            // 危险操作用醒目色，避免顺手点到
            palette.button: "#B3261E"
            palette.buttonText: "#FFFFFF"
        }
        Button {
            Layout.fillWidth: true
            visible: !AppController.isFinished(AppController.selectedComicId)
            text: "已看完，归档"
            onClicked: function() {
                const id = AppController.selectedComicId
                AppController.selectedComicId = ""
                AppController.markFinished(id, true)
            }
        }
        Button {
            Layout.fillWidth: true
            visible: AppController.isFinished(AppController.selectedComicId)
            text: "移出已看过"
            onClicked: function() {
                AppController.markFinished(AppController.selectedComicId, false)
            }
        }
        Button {
            Layout.fillWidth: true
            text: "Back"
            onClicked: function() {
                AppController.selectedComicId = ""
            }
        }
        Item {
            Layout.fillHeight: true
        }
    }


    // 删除确认：两段式，先开对话框，勾选确认后才允许执行
    Dialog {
        id: deleteDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, 520)
        modal: true
        title: "删除本子"
        closePolicy: Popup.CloseOnEscape

        property bool armed: false

        onOpened: armed = false

        ColumnLayout {
            spacing: 12
            width: parent.width

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.bold: true
                text: dataByRole(Qt.DisplayRole)
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#B3261E"
                text: "这会删除电脑上的漫画文件。文件不会立即销毁，"
                    + "而是移动到库目录的 .deleted 文件夹，需要时可以手工恢复。"
            }

            CheckBox {
                id: confirmBox
                text: "我确认要删除这个本子"
                onCheckedChanged: deleteDialog.armed = checked
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8

                Button {
                    Layout.fillWidth: true
                    text: "取消"
                    onClicked: deleteDialog.close()
                }
                Button {
                    Layout.fillWidth: true
                    text: "确认删除"
                    enabled: deleteDialog.armed
                    palette.button: "#B3261E"
                    palette.buttonText: "#FFFFFF"
                    onClicked: {
                        deleteDialog.close()
                        AppController.requestDelete(AppController.selectedComicId)
                    }
                }
            }
        }
    }

    // 删除结果提示
    Dialog {
        id: resultDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, 520)
        modal: true
        title: "删除"

        property string message: ""

        ColumnLayout {
            spacing: 12
            width: parent.width

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: resultDialog.message
            }
            Button {
                Layout.fillWidth: true
                text: "知道了"
                onClicked: {
                    resultDialog.close()
                    if (root.deleteSucceeded) {
                        AppController.selectedComicId = ""
                    }
                }
            }
        }
    }

    Connections {
        target: AppController
        function onDeleteFinished(ok, message) {
            root.deleteSucceeded = ok
            resultDialog.message = message
            resultDialog.open()
        }
    }

    ComicViewer {
        parent: Overlay.overlay
        visible: root.startReading
        pageCount: root.comicPageCount
        comicId: AppController.selectedComicId
    }
}
