import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import net.blumia.pineapple.comic.reader

Item {
    id: root
    required property int pageCount
    required property string comicId

    property bool osdVisible: false
    property int currentPage: 0          // 0-based，界面显示时 +1

    anchors.fill: parent

    // 断点续读：本地进度优先，没有再用服务端记录的 current_page
    function resolveStartPage() {
        const local = AppController.savedProgress(comicId)
        if (local > 0) return Math.min(local, Math.max(pageCount - 1, 0))
        const server = AppController.serverProgress()
        if (server > 0) return Math.min(server, Math.max(pageCount - 1, 0))
        return 0
    }

    // 取视口上部约 1/3 处所在的条目，最贴近"正在看的这一页"
    function currentVisibleIndex() {
        if (pageCount <= 0) return 0
        let idx = view.indexAt(view.contentX + 1, view.contentY + view.height * 0.3)
        if (idx < 0) idx = view.indexAt(view.contentX + 1, view.contentY + 1)
        return idx >= 0 ? idx : 0
    }

    function persistProgress() {
        const idx = currentVisibleIndex()
        if (idx !== currentPage) {
            currentPage = idx
        }
        AppController.saveProgress(comicId, idx)
    }

    ListView {
        id: view
        anchors.fill: parent
        clip: true
        orientation: ListView.Vertical
        model: root.pageCount
        spacing: 2
        cacheBuffer: 4000
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 3000

        delegate: Item {
            id: pageItem
            required property int index
            width: view.width
            height: Math.max(pageImage.paintedHeight, 1)

            Image {
                id: pageImage
                width: view.width
                height: implicitWidth > 0 ? Math.round(width * implicitHeight / implicitWidth)
                                         : Math.round(view.width * 1.4)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                source: AppController.comicImageSource(pageItem.index)

                onStatusChanged: function() {
                    if (status === Image.Error) {
                        retryTimer.restart()
                    }
                }

                Timer {
                    id: retryTimer
                    interval: 600
                    onTriggered: {
                        pageImage.source = ""
                        pageImage.source = AppController.comicImageSource(pageItem.index)
                    }
                }
            }
        }

        // 滚动停止后落盘，避免频繁写设置
        onMovementEnded: saveTimer.restart()
        onFlickEnded: saveTimer.restart()

        Component.onCompleted: {
            const start = root.resolveStartPage()
            if (start > 0) {
                positionViewAtIndex(start, ListView.Beginning)
            }
            root.currentPage = start
        }
    }

    Timer {
        id: saveTimer
        interval: 800
        onTriggered: root.persistProgress()
    }

    // 关闭或离开前再存一次
    Component.onDestruction: persistProgress()

    // 顶部页码指示，点一下呼出操作面板
    Rectangle {
        visible: !root.osdVisible
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 8
        width: pageLabel.implicitWidth + 24
        height: pageLabel.implicitHeight + 10
        radius: height / 2
        color: "#99000000"

        Label {
            id: pageLabel
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 13
            text: `${root.currentPage + 1} / ${root.pageCount}`
        }

        TapHandler {
            onTapped: root.osdVisible = true
        }
    }

    // 操作面板
    Pane {
        visible: root.osdVisible
        anchors.fill: parent
        opacity: 0.9

        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width * 0.7
            spacing: 12

            Label {
                Layout.alignment: Qt.AlignHCenter
                font.pixelSize: 16
                text: `第 ${root.currentPage + 1} 页 / 共 ${root.pageCount} 页`
            }
            Slider {
                id: pageSlider
                Layout.fillWidth: true
                from: 0
                to: Math.max(root.pageCount - 1, 0)
                stepSize: 1
                value: root.currentPage
                onMoved: {
                    view.positionViewAtIndex(Math.round(value), ListView.Beginning)
                }
            }
            Button {
                Layout.fillWidth: true
                text: "继续阅读"
                onClicked: root.osdVisible = false
            }
            Button {
                Layout.fillWidth: true
                text: "关闭漫画"
                onClicked: {
                    root.persistProgress()
                    root.osdVisible = false
                    AppController.closeComic()
                }
            }
        }
    }
}
