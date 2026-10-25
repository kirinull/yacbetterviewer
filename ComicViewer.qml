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
    property bool startApplied: false

    anchors.fill: parent

    // 断点续读：本地进度优先，没有再用服务端记录的 current_page
    function resolveStartPage() {
        if (pageCount <= 0) return 0
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

    // pageCount 是异步拿到的：初始为 0，数据到位后才变成真实值。
    // 所以初始定位不能只在 onCompleted 里做，必须等 pageCount 就绪。
    function applyStartPosition() {
        if (startApplied || pageCount <= 0) return
        const start = resolveStartPage()
        if (start > 0) {
            view.positionViewAtIndex(start, ListView.Beginning)
        }
        currentPage = start
        startApplied = true
    }

    onPageCountChanged: applyStartPosition()
    Component.onCompleted: applyStartPosition()

    ListView {
        id: view
        anchors.fill: parent
        clip: true
        orientation: ListView.Vertical
        model: root.pageCount
        spacing: 2
        cacheBuffer: 600
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 3000

        delegate: Item {
            id: pageItem
            required property int index
            width: view.width

            // 高度必须由图片原始尺寸算出，不能用 paintedHeight：
            // paintedHeight 依赖 Image 当前高度，会与这里的 height 形成绑定循环，
            // 表现为页面卡在 1/xxx 且界面无响应。
            height: {
                const it = pageLoader.item
                if (it && it.implicitWidth > 0 && it.implicitHeight > 0) {
                    return Math.max(Math.round(view.width * it.implicitHeight / it.implicitWidth), 1)
                }
                return Math.round(view.width * 1.4)
            }

            // 只加载视口附近的页。YACReader 服务端对同一 comic 的并发页面请求
            // 有限制，一次性请求全部页面会导致后续请求全部 404。
            readonly property bool nearViewport: {
                const vy = view.contentY
                const vh = view.height
                const top = pageItem.y
                const bottom = pageItem.y + pageItem.height
                return bottom > vy - vh * 0.5 && top < vy + vh * 1.5
            }

            Loader {
                id: pageLoader
                anchors.fill: parent
                active: pageItem.nearViewport && root.visible

                sourceComponent: Image {
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    source: AppController.comicImageSource(pageItem.index)

                    // 服务端打开 cbz 需要时间，首个请求常返回 412（opening file）或 404。
                    // 用递增间隔重试，而不是固定 800ms 死循环。
                    property int retryCount: 0

                    onStatusChanged: {
                        if (status === Image.Error) {
                            if (retryCount < 6) {
                                retryCount++
                                retryTimer.interval = 500 * Math.pow(2, retryCount - 1)
                                retryTimer.restart()
                            }
                        } else if (status === Image.Ready) {
                            retryCount = 0
                        }
                    }

                    Timer {
                        id: retryTimer
                        onTriggered: {
                            const s = AppController.comicImageSource(pageItem.index)
                            parent.source = ""
                            parent.source = s
                        }
                    }
                }
            }
        }

        // 滚动停止后落盘，避免频繁写设置
        onMovementEnded: saveTimer.restart()
        onFlickEnded: saveTimer.restart()
    }

    Timer {
        id: saveTimer
        interval: 800
        onTriggered: root.persistProgress()
    }

    // 离开前再存一次
    Component.onDestruction: {
        if (startApplied) persistProgress()
    }

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
                    root.currentPage = Math.round(value)
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
