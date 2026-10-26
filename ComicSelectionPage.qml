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
            Layout.margins: 2
            spacing: 0

            // 三条横线：打开文件夹导航（手绘，避免字体缺字形变成方框）
            ToolButton {
                flat: true
                Layout.preferredWidth: 40
                padding: 0
                onClicked: function() {
                    drawer.open();
                }

                contentItem: Item {
                    implicitWidth: 20
                    implicitHeight: 16
                    Column {
                        anchors.centerIn: parent
                        spacing: 4
                        Repeater {
                            model: 3
                            Rectangle {
                                width: 20
                                height: 2
                                radius: 1
                                color: sysPalette.windowText
                            }
                        }
                    }
                }
            }

            // 已看过视图
            ToolButton {
                id: finishedButton
                flat: true
                text: "✓"
                font.pixelSize: 18
                Layout.preferredWidth: 36
                padding: 0
                opacity: AppController.showFinished ? 1.0 : 0.45
                onClicked: AppController.setShowFinished(!AppController.showFinished)
            }

            // 收藏视图
            ToolButton {
                id: favButton
                flat: true
                text: "★"
                font.pixelSize: 18
                Layout.preferredWidth: 36
                padding: 0
                opacity: AppController.showFavorites ? 1.0 : 0.45
                onClicked: AppController.setShowFavorites(!AppController.showFavorites)
            }

            TextField {
                id: searchField
                Layout.fillWidth: true
                Layout.minimumWidth: 90
                placeholderText: "搜索标题"
                onTextChanged: AppController.setFilter(text)
            }

            ToolButton {
                id: sortButton
                flat: true
                text: "⇅"
                font.pixelSize: 18
                onClicked: sortMenu.open()

                Menu {
                    id: sortMenu
                    y: sortButton.height

                    MenuItem {
                        text: "添加时间 ↓ 新"
                        checkable: true
                        checked: AppController.sortMode === 0 && !AppController.sortAscending
                        onTriggered: AppController.setSort(0, false)
                    }
                    MenuItem {
                        text: "添加时间 ↑ 旧"
                        checkable: true
                        checked: AppController.sortMode === 0 && AppController.sortAscending
                        onTriggered: AppController.setSort(0, true)
                    }
                    MenuSeparator { }
                    MenuItem {
                        text: "名称 A-Z"
                        checkable: true
                        checked: AppController.sortMode === 1 && !AppController.sortAscending
                        onTriggered: AppController.setSort(1, false)
                    }
                    MenuItem {
                        text: "名称 Z-A"
                        checkable: true
                        checked: AppController.sortMode === 1 && AppController.sortAscending
                        onTriggered: AppController.setSort(1, true)
                    }
                    MenuSeparator { }
                    MenuItem {
                        text: "作者 A-Z"
                        checkable: true
                        checked: AppController.sortMode === 2 && !AppController.sortAscending
                        onTriggered: AppController.setSort(2, false)
                    }
                    MenuItem {
                        text: "作者 Z-A"
                        checkable: true
                        checked: AppController.sortMode === 2 && AppController.sortAscending
                        onTriggered: AppController.setSort(2, true)
                    }
                    MenuSeparator { }
                    MenuItem {
                        text: "大小 ↓ 大"
                        checkable: true
                        checked: AppController.sortMode === 3 && !AppController.sortAscending
                        onTriggered: AppController.setSort(3, false)
                    }
                    MenuItem {
                        text: "大小 ↑ 小"
                        checkable: true
                        checked: AppController.sortMode === 3 && AppController.sortAscending
                        onTriggered: AppController.setSort(3, true)
                    }
                    MenuSeparator { }
                    MenuItem {
                        text: "页数 ↓ 多"
                        checkable: true
                        checked: AppController.sortMode === 4 && !AppController.sortAscending
                        onTriggered: AppController.setSort(4, false)
                    }
                    MenuItem {
                        text: "页数 ↑ 少"
                        checkable: true
                        checked: AppController.sortMode === 4 && AppController.sortAscending
                        onTriggered: AppController.setSort(4, true)
                    }
                    MenuSeparator { }
                    MenuItem {
                        text: "随机洗牌"
                        onTriggered: AppController.setSort(5, false)
                    }
                }
            }
            Label {
                text: `${AppController.filteredCount}/${AppController.totalComicCount}`
                font.pixelSize: 12
                color: sysPalette.windowText
                opacity: 0.75
            }
        }

        // 作者筛选条：从详情页点作者进来时显示
        Rectangle {
            visible: AppController.authorFilter !== ""
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: "#1AC2185B"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 6

                Label {
                    Layout.fillWidth: true
                    text: `作者：${AppController.authorFilter}`
                    font.pixelSize: 13
                    color: "#C2185B"
                    elide: Text.ElideRight
                }
                ToolButton {
                    text: "✕"
                    font.pixelSize: 14
                    Layout.preferredWidth: 32
                    padding: 0
                    onClicked: AppController.clearAuthorFilter()
                }
            }
        }

        GridView {
            id: gridView
            clip: true
            // 允许下拉过冲，用来触发刷新
            boundsBehavior: Flickable.DragAndOvershootBounds

            // 滚动时持续记录（防抖 250ms 落定）。
            // 只靠 onMovementEnded 会漏：手指慢慢拖的时候不一定触发。
            // restoring 用于避免恢复过程把自己又记一遍。
            property bool restoring: false

            // 记住「第一个可见的是第几个」。像素值在数据重新加载后不可靠：
            // 布局还没算完时给 contentY 赋值会被立刻重置回 0。
            function rememberPosition() {
                if (restoring) return
                const idx = gridView.indexAt(1, Math.max(gridView.contentY, 0) + 1)
                if (idx >= 0) {
                    AppController.gridFirstIndex = idx
                    AppController.gridScrollY = gridView.contentY
                }
            }

            Timer {
                id: scrollSaveTimer
                interval: 250
                onTriggered: rememberPosition()
            }

            Component.onDestruction: rememberPosition()

            // 数据是网络异步回来的，可能好几秒后才到，所以重试窗口给足 12 秒。
            // 每拍都检查：数据够不够撑起滚动、当前是否已经到位。
            function scheduleRestore() {
                restoreTimer.attempts = 0
                restoreTimer.restart()
            }

            Timer {
                id: restoreTimer
                property int attempts: 0
                interval: 150
                repeat: true
                onTriggered: {
                    attempts++
                    const idx = AppController.gridFirstIndex
                    const y = AppController.gridScrollY

                    if (idx < 0 && y <= 0) { stop(); return }
                    if (count <= 0) {
                        if (attempts >= 80) stop()
                        return
                    }

                    restoring = true
                    if (idx > 0 && idx < count) {
                        // 用索引定位：不依赖像素，布局没算完也能定位
                        positionViewAtIndex(idx, GridView.Beginning)
                    } else if (y > 0 && contentHeight > height + y) {
                        contentY = y
                    }
                    restoring = false

                    // 再校验一次是否真的到位
                    const nowIdx = indexAt(1, Math.max(contentY, 0) + 1)
                    if (idx < 0 || (nowIdx >= 0 && Math.abs(nowIdx - idx) <= 1)) {
                        stop()
                        return
                    }
                    if (attempts >= 80) stop()
                }
            }

            // contentY 变化时做两件事：
            // 1) 防抖记录滚动位置（返回详情页时要还原到这里）
            // 2) 下拉超过 80px 触发刷新
            onContentYChanged: {
                if (!restoring) {
                    scrollSaveTimer.restart()
                    const idx = indexAt(1, Math.max(contentY, 0) + 1)
                    if (idx >= 0) {
                        AppController.gridFirstIndex = idx
                    }
                }
                if (contentY < -80 && !refreshIndicator.refreshing) {
                    refreshIndicator.refreshing = true
                    AppController.updateComicsInFolder()
                    refreshResetTimer.restart()
                }
            }
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

                // 收藏星标
                Label {
                    visible: AppController.isFavorite(model.comicId)
                    anchors.left: cover.left
                    anchors.top: cover.top
                    anchors.margins: 4
                    text: "★"
                    color: "#FFD54F"
                    font.pixelSize: 14
                    style: Text.Outline
                    styleColor: "#99000000"
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

        // 刷新状态提示
        Rectangle {
            id: refreshIndicator
            property bool refreshing: false
            visible: refreshing
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 20
            width: refreshLabel.implicitWidth + 28
            height: refreshLabel.implicitHeight + 14
            radius: height / 2
            color: "#CC000000"

            Label {
                id: refreshLabel
                anchors.centerIn: parent
                color: "white"
                font.pixelSize: 12
                text: "正在刷新…"
            }
        }

        Timer {
            id: refreshResetTimer
            interval: 1800
            onTriggered: refreshIndicator.refreshing = false
        }

        // 同 LibrarySelectionPage：页面可能被提前创建，那时还没连上服务器，
        // 请求会静默失败。改为页面可见时确保加载过。
        function ensureComics() {
            if (AppController.connectionState !== AppController.Connected) return
            if (AppController.selectedLibraryId === -1) return
            if (AppController.totalComicCount === 0) {
                AppController.updateComicsInFolder()
            }
        }

        Component.onCompleted: ensureComics()
        onVisibleChanged: {
            if (visible) {
                ensureComics()
            }
        }

        Connections {
            target: AppController
            // 数据到位（或筛后数量变化）才具备还原条件
            function onComicsReloaded() {
                gridView.scheduleRestore()
            }
        }
    }
}
