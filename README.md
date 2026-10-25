# Pineapple Comic Reader Plus

YACReader Library Server 的 Android 客户端，基于 [BLumia/pineapple-comic-reader](https://github.com/BLumia/pineapple-comic-reader) 改造。

## 相对上游的改动

### 1. 纵向阅读

阅读器从左右翻页（`SwipeView`）改为纵向连续滚动（垂直 `ListView`），第 1 页在上、第 2 页在下，适合长条漫画和手机竖屏。

### 2. 阅读进度

- 每本书读完的位置存在本地（`QSettings`，键 `progress/<comicId>`）
- 重新打开时自动跳回上次位置
- 漫画网格上显示 `当前页/总页数` 角标
- 详情页显示「第 N 页续读」
- 本地没有记录时，回退使用服务端 `current_page`

### 3. 搜索筛选

列表页顶部有搜索框，输入即在已加载的列表上本地过滤（不区分大小写），旁边显示 `筛后/总数`。

### 4. 修复标题显示

`LibrarySelectionPage.qml` 的 `Connections` 里直接引用了不存在的 `model`，导致 `ReferenceError: model is not defined`。改为通过 `AppController.librariesModel` 访问。

### 5. Android 平台适配

- `QT_QUICK_CONTROLS_STYLE` 设为 `FluentWinUI3` 是 Windows 专属，Android 上会导致 `Main.qml` 加载失败并 SIGABRT，用 `#ifndef Q_OS_ANDROID` 隔离
- 修复 `TextField.clearButtonEnabled`（Qt Widgets 属性，Quick Controls 没有）

## 已知问题

**页面图片加载不出来。** YACReader v2 API 的页面端点 `/v2/library/{lib}/comic/{id}/page/{n}/remote` 返回 404。从服务端源码看，`PageControllerV2` 通过 `x-request-id` header 绑定会话，`/remote` 端点负责创建会话，但按此流程调用仍取不到页面。上游项目的阅读器同样无法显示图片，属于未完成部分。

下一步需要抓官方 Android 客户端的实际请求来确认真实的调用顺序。

## 构建

### 依赖

- Qt 6.8.3（Android arm64 + Windows host）
- Android NDK 26.1.10909125
- JDK 21
- Android SDK：platform 36、build-tools 37.0.0

### 步骤

```powershell
$env:ANDROID_SDK_ROOT = "$env:LOCALAPPDATA\Android\Sdk"
$env:ANDROID_NDK_ROOT = "$env:ANDROID_SDK_ROOT\ndk\26.1.10909125"
$env:JAVA_HOME = "C:\Program Files\Zulu\zulu-21"

cmake -S . -B build -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE="C:/Qt/6.8.3/android_arm64_v8a/lib/cmake/Qt6/qt.toolchain.cmake" `
  -DQT_HOST_PATH="C:/Qt/6.8.3/mingw_64" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-23 `
  -DCMAKE_BUILD_TYPE=Release

cmake --build build
```

产物在 `build/android-build/build/outputs/apk/release/`，未签名，需要 `zipalign` + `apksigner`。

## 配置

在应用里填 YACReader Library Server 的地址，例如 `http://192.168.1.10:8080`，不能带结尾斜杠。

## 许可

GPL-3.0，与上游一致。
