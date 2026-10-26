# yacbetterviewer

YACReader Library Server 的 Android 客户端。

基于 [BLumia/pineapple-comic-reader](https://github.com/BLumia/pineapple-comic-reader) 改造，补齐了日常使用中缺失的功能。

## 功能

**阅读**
- 纵向连续滚动，第 1 页在上、第 2 页在下
- 阅读进度本地记忆，关掉再开回到原页
- 底部居中页码，点开呼出操作面板：继续阅读 / 下一本 / 已看完归档 / 加入收藏 / 删除本子 / 关闭
- 读到最后一页自动归档到「已看过」
- 下一本优先切同作者的作品

**列表**
- 搜索筛选，实时过滤
- 六种排序：添加时间、名称、作者、文件大小、页数、随机洗牌
- 列数可切 1 到 4 列
- 封面下显示文件名与进度角标
- 下拉刷新
- 点开漫画返回后停在原处，只有主动重新排序才回到顶部

**记忆与历史**
- 排序方式、列数、滚动位置全部持久化
- 随机排序的种子持久化，重启后顺序不变
- 浏览历史面板，按时间倒序，点击直达

**其他**
- 封面磁盘缓存，命中率 100%，容量上限 120 MB
- 点作者名筛选该作者的全部作品
- 全面屏手势按层级返回

## 相对上游的技术改动

### 纵向阅读

阅读器从左右翻页（`SwipeView`）改为纵向连续滚动（垂直 `ListView`）。

页面按视口范围懒加载，只有滚动到附近才发请求。这一点很关键：YACReader 服务端对同一本漫画的并发页面请求有限制，一次性请求全部页面会导致后续请求全部返回 404。实测一次性初始化 44 页请求时图片全白。

### 封面缓存

封面走 `QQuickAsyncImageProvider`，内部先查本地缓存，未命中才下载并落盘，一次请求一次下载。

之前 QML 的 `Image` 直连网络会导致同一张封面下载两次（Image 自己拉一次，后台缓存再存一次）。改用 ImageProvider 后路径唯一。

### 列表位置保持

`Main.qml` 用 `StackLayout` 让列表页常驻，从详情返回时页面不销毁重建。滚动位置、排序方式、列数通过 `QSettings` 持久化。

只有主动切换排序、随机洗牌、切换列数时位置归零回顶部，因为重新排列后旧行号已无意义。

### 修复标题显示

`LibrarySelectionPage.qml` 的 `Connections` 里直接引用了不存在的 `model`，导致 `ReferenceError: model is not defined`。改为通过 `AppController.librariesModel` 访问。

## 构建

需要 Qt 6.8.3（含 `qtimageformats` 模块，webp 支持在里面）、NDK r26b、JDK 21、CMake、Ninja。

```bash
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=$QT_ANDROID/lib/cmake/Qt6/qt.toolchain.cmake \
  -DQT_HOST_PATH=$QT_HOST \
  -DANDROID_SDK_ROOT=$SDK -DANDROID_NDK_ROOT=$NDK
cmake --build build
```

产物在 `build/android-build/build/outputs/apk/release/`，未签名，需要用 `apksigner` 签名后才能安装。

`NDK` 必须用 r26b。r24 的 libc++ 缺 `std::pmr` 符号，`libQt6Core` 会加载失败。

## 已知限制

- 只构建了 arm64-v8a
- 收藏与归档是本地标记，不写入 YACReader 服务端（服务端 v2 API 没有对应的写入字段）
- **不支持在 App 内删除文件**。YACReader 的 v2 API 没有 DELETE 路由，手机也拿不到电脑上的写权限。要删文件请在电脑上操作，或用支持 SMB 的文件管理器挂载共享目录

### 关于可选删除功能

代码里保留了删除接口，但它需要一个跑在电脑上的辅助服务，未配置时相关按钮完全不显示，不影响任何其他功能。

默认构建不包含这部分（`CMakeLists.txt` 会尝试读 `delete-service/config.json` 注入 token，文件不存在则跳过），所以无需关心。

## License

本项目基于 [BLumia/pineapple-comic-reader](https://github.com/BLumia/pineapple-comic-reader) 修改，遵循上游授权：

**GNU General Public License v3.0 only**

原始代码版权归 BLumia 及上游贡献者所有，修改部分同样以 GPL-3.0 授权。完整条款见 [LICENSE](LICENSE)。

```
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
```
