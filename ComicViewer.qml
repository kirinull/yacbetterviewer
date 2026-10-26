import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import net.blumia.pineapple.comic.reader

Item {
    id: root
    required property int pageCount
    required property string comicId

    property bool osdVisible: false
    property int currentPage: 0          // 0-based，界面显示时 +1
    property bool startApplied: false
    property bool markedFinished: false

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

    // 取视口上部约 1/3 处所在的条目，最贴近"正在看的这一页"。
    // 触底时必须直接判为最后一页：最后一页贴在视口顶部时，
    // indexAt(contentY + height*0.3) 会落在下方空白区，算出更小的页码，
    // 导致"读完"永远判不出来。
    function currentVisibleIndex() {
        if (pageCount <= 0) return 0
        if (view.atYEnd) return pageCount - 1
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

        // 读到最后一页就自动归档（只写本地标记，不动文件）
        if (pageCount > 0 && idx >= pageCount - 1 && !markedFinished) {
            markedFinished = true
            AppController.markFinished(comicId, true)
        }
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
        // 定位后强制布局一次，让 delegate 的 y 就绪，
        // 否则 nearViewport 全为 false，Loader 永不激活（白屏只剩页码）
        Qt.callLater(function() {
            view.forceLayout()
        })
    }

    onPageCountChanged: applyStartPosition()
    Component.onCompleted: applyStartPosition()

    // 切到下一本时重新定位到该书自己的进度
    onComicIdChanged: {
        startApplied = false
        markedFinished = false
        currentPage = 0
        applyStartPosition()
    }

    ListView {
        id: view
        anchors.fill: parent
        clip: true
        orientation: ListView.Vertical
        model: root.pageCount
        spacing: 2
        // 预创建多留一些，减少滚动时反复创建销毁 delegate
        cacheBuffer: 2400
        boundsBehavior: Flickable.StopAtBounds
        flickDeceleration: 3000

        delegate: Item {
            id: pageItem
            required property int index
            width: view.width

            // 高度只在图片首次就绪时算一次，之后固定。
            // 原来 height 绑定图片实际尺寸，每加载完一页就改变一次，
            // 触发 ListView 重新布局：所有 delegate 的 y 变、nearViewport
            // 全部重算，滚动时肉眼可见掉帧。
            property real settledHeight: 0
            height: settledHeight > 0
                    ? settledHeight
                    : Math.round(view.width * 1.4)

            // 只加载视口附近的页。YACReader 服务端对同一 comic 的并发页面请求
            // 有限制，一次性请求全部页面会导致后续请求全部 404。
            //
            // index < 3 是打破死锁的兜底：初始定位时 delegate 的 y 可能还没
            // 布局完成，nearViewport 会全为 false，Loader 不激活，height 又
            // 依赖 Loader 的 item，于是永远激活不了，表现为白屏只剥页码。
            readonly property bool nearViewport: {
                if (index < 3) return true
                const vy = view.contentY
                const vh = view.height
                const top = pageItem.y
                const bottom = pageItem.y + pageItem.height
                return bottom > vy - vh * 0.35 && top < vy + vh * 1.35
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

                    // 按屏幕物理宽度解码，而不是原图分辨率。
                    // 漫画扫描件常见两千像素宽，按原尺寸解码再缩放，
                    // 内存带宽和 GPU 采样都吃不消，这是掉帧的主因之一。
                    sourceSize.width: Math.round(view.width * (Screen.devicePixelRatio > 0
                                                               ? Screen.devicePixelRatio : 2))

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
                            if (pageItem.settledHeight === 0
                                    && implicitWidth > 0 && implicitHeight > 0) {
                                pageItem.settledHeight =
                                    Math.max(Math.round(view.width * implicitHeight / implicitWidth), 1)
                            }
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

    // 页码指示器：屏幕底部居中，点一下呼出操作面板
    Rectangle {
        visible: !root.osdVisible
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 24
        width: pageLabel.implicitWidth + 28
        height: pageLabel.implicitHeight + 12
        radius: height / 2
        color: "#B3000000"

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
        background: Rectangle {
            color: sysPalette.window
        }

        SystemPalette {
            id: sysPalette
            colorGroup: SystemPalette.Active
        }

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
                    const p = Math.round(value)
                    view.positionViewAtIndex(p, ListView.Beginning)
                    root.currentPage = p
                    // 拖到最末端就直接当作读完，不等滚动事件
                    if (p >= root.pageCount - 1 && !root.markedFinished) {
                        root.markedFinished = true
                        AppController.saveProgress(root.comicId, p)
                        AppController.markFinished(root.comicId, true)
                    }
                }
            }
            Button {
                Layout.fillWidth: true
                text: "继续阅读"
                onClicked: root.osdVisible = false
            }
            Button {
                Layout.fillWidth: true
                text: "下一本"
                enabled: AppController.hasNextComic()
                onClicked: {
                    root.persistProgress()
                    root.osdVisible = false
                    AppController.openNextComic()
                }
            }
            Button {
                Layout.fillWidth: true
                text: "已看完，归档"
                onClicked: {
                    root.persistProgress()
                    AppController.markFinished(root.comicId, true)
                    root.osdVisible = false
                    AppController.closeComic()
                }
            }
            Button {
                Layout.fillWidth: true
                text: AppController.isFavorite(root.comicId)
                      ? "★ 已收藏（点击取消）"
                      : "☆ 加入收藏"
                onClicked: AppController.toggleFavorite(root.comicId)
            }
            Button {
                Layout.fillWidth: true
                text: "删除本子"
                visible: AppController.deleteServiceConfigured()
                palette.button: "#B3261E"
                palette.buttonText: "#FFFFFF"
                onClicked: {
                    root.osdVisible = false
                    viewerDeleteDialog.open()
                }
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

    // 阅读器内的删除确认：和详情页一致的两段式
    Dialog {
        id: viewerDeleteDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, 520)
        modal: true
        title: "删除本子"

        property bool armed: false
        onOpened: armed = false

        ColumnLayout {
            spacing: 12
            width: parent.width

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.bold: true
                text: `第 ${root.currentPage + 1} 页 / 共 ${root.pageCount} 页`
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#B3261E"
                text: "这会删除电脑上的漫画文件。文件不会立即销毁，"
                    + "而是移动到库外的回收站目录，需要时可以手工恢复。"
            }

            CheckBox {
                text: "我确认要删除这个本子"
                onCheckedChanged: viewerDeleteDialog.armed = checked
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8

                Button {
                    Layout.fillWidth: true
                    text: "取消"
                    onClicked: viewerDeleteDialog.close()
                }
                Button {
                    Layout.fillWidth: true
                    text: "确认删除"
                    enabled: viewerDeleteDialog.armed
                    palette.button: "#B3261E"
                    palette.buttonText: "#FFFFFF"
                    onClicked: {
                        viewerDeleteDialog.close()
                        root.persistProgress()
                        AppController.requestDelete(root.comicId)
                    }
                }
            }
        }
    }

    Connections {
        target: AppController
        function onDeleteFinished(ok, message) {
            if (ok) {
                // 删除成功后退出阅读器，详情页会跟着刷新
                AppController.closeComic()
            }
        }
    }
}
