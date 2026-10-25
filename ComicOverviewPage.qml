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

    ComicViewer {
        parent: Overlay.overlay
        visible: root.startReading
        pageCount: root.comicPageCount
        comicId: AppController.selectedComicId
    }
}
