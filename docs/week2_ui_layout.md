# 第二周：Qt 2x2 视频网格初步实现说明

## 1. 本次目标

本次实现对应 [项目规划](project_plan.md) 中“第 2 周：Qt 空界面和多宫格布局”的初步目标：

- 保留 Qt 6 Widgets、CMake、MSVC、C++17 的工程基础。
- 在 `MainWindow` 中显示固定 2x2 视频宫格。
- 每个视频格显示设备名称、连接状态和黑色视频占位区域。
- 支持拖拽一个视频格到另一个视频格，以动画方式交换两个实际控件的位置。
- 不接入 FFmpeg、RTMP、H.264、线程或真实视频帧。

这一步的定位是先稳定 UI 结构。后续接入解码器时，只需要把 `VideoWidget` 的黑色视频区域替换为真实视频渲染逻辑，不需要重写窗口或网格布局。

## 2. 本次目录与文件调整

```text
include/ui/
├── MainWindow.h
├── VideoGridWidget.h             # 新增：管理固定 2x2 网格
└── VideoWidget.h                 # 新增：单路视频格 UI

include/app/
└── StyleLoader.h                  # 应用级 QSS 加载服务

include/core/
└── Singleton.h                    # 通用 CRTP 单例模板

src/ui/
├── MainWindow.cpp                # 修改：中央控件改为 VideoGridWidget
├── VideoGridWidget.cpp           # 新增：创建四个 VideoWidget
└── VideoWidget.cpp               # 新增：设备名、状态和黑色视频占位区

src/app/
└── StyleLoader.cpp                # 外部 QSS 与 QRC 回退加载逻辑

resources/
├── styles/
│   └── app.qss                    # 默认应用样式
└── styles.qrc                     # QRC 资源映射

tests/
└── VideoGridSmokeTest.cpp        # 新增：2x2 网格冒烟测试

CMakeLists.txt                    # 修改：增加 UI 静态库和 CTest 测试目标
```

本次没有修改 `README.md`，也没有修改 RTMP 链路验证脚本及其文档。

## 3. UI 结构与修改逻辑

控件关系如下：

```text
MainWindow
  └── VideoGridWidget
        ├── VideoWidget camera001
        │     ├── QLabel: 设备名称
        │     └── QFrame: 黑色视频占位区域
        │           └── QLabel: 状态文本（未连接）
        ├── VideoWidget camera002
        ├── VideoWidget camera003
        └── VideoWidget camera004
```

### 3.1 `VideoWidget`

`VideoWidget` 继承 `QFrame`，表示单个设备的显示槽位。

职责：

- 顶部显示设备名称。
- 中间提供可扩展的黑色视频区域。
- 在黑色视频区域中央显示状态文本。
- 提供后续播放器可调用的基础接口。
- 识别左键点击、拖拽源和拖拽目标，但不自行决定网格布局交换。

当前公开接口：

```cpp
void setDeviceName(const QString &deviceName);
void setStatusText(const QString &statusText);
QString deviceName() const;
QString statusText() const;
bool isDragEnabled() const noexcept;
```

初始默认值是“未命名设备”和“未连接”。`VideoGridWidget` 创建具体设备格后，将它们设置为 `camera001` 到 `camera004`。

黑色视频区域采用独立的 `QFrame`，其背景、边框和文字颜色由应用启动时加载的 `resources/styles/app.qss` 控制。`VideoWidget` 通过 `styleRole="videoWidget"` 与稳定的控件对象名暴露 QSS 选择器边界。这样后续可以在此位置增加 `QLabel/QImage` 显示、`paintEvent`，或替换为 `QOpenGLWidget`，不会影响设备标题和状态文本的布局。

为了保证鼠标操作由最外层视频格统一接收，设备名称、视频区域和状态标签均设置为 `WA_TransparentForMouseEvents`。视频格内部使用 `application/x-rtmp-monitor-video-widget` MIME 类型标识同进程拖放，只有另一个 `VideoWidget` 才能成为有效目标。

### 3.2 `VideoGridWidget`

`VideoGridWidget` 继承 `QWidget`，内部使用 `QGridLayout` 创建固定的 2 行 2 列布局。

映射关系：

| 行 | 列 | 设备名称 | 初始状态 |
| --- | --- | --- | --- |
| 0 | 0 | `camera001` | 未连接 |
| 0 | 1 | `camera002` | 未连接 |
| 1 | 0 | `camera003` | 未连接 |
| 1 | 1 | `camera004` | 未连接 |

布局对两个行和两个列均设置了相同的 stretch，因此主窗口缩放时四个视频格会同步扩展。`videoWidgetAt(int)` 提供了按索引获取格子的入口，便于下一阶段将播放器实例绑定到指定设备格。

新增的 `swapVideoWidgets(int firstIndex, int secondIndex)` 交换的是 `videoWidgets_` 槽位数组和 `QGridLayout` 中的实际 `VideoWidget` 对象，不是复制设备名称或状态字符串。因此一个视频格内部的 `titleLabel_`、`videoSurface_`、`statusLabel_`，以及未来的解码帧、播放器控制器绑定都会作为同一对象整体移动。动画结束后发出 `videoWidgetsSwapped(int, int)`，供后续设备配置持久化模块订阅。

### 3.3 拖拽交互与交换动画

拖拽流程如下：

```text
左键按下
  -> dragState=pressed，显示短暂蓝色高亮
  -> 鼠标移动超过 QApplication::startDragDistance()
  -> 创建 QDrag 和视频格快照
  -> 拖入另一个 VideoWidget
  -> 目标 dragState=dragTarget，显示蓝色虚线边框
  -> 松开鼠标
  -> VideoGridWidget 接收 swapRequested
  -> 禁用所有视频格拖拽并交换真实控件槽位
  -> 两张交换前快照以 220ms OutCubic 曲线双向移动
  -> 删除快照、显示真实控件、恢复拖拽
```

不能直接对 `QGridLayout` 管理的真实控件做几何动画，因为布局会在事件循环中重设控件位置，导致动画抖动或被覆盖。当前实现使用 `VideoWidget::grab()` 创建两个鼠标穿透的 `QLabel` 快照覆盖层：真实控件先完成布局交换并临时隐藏，覆盖层负责视觉移动，动画结束后才重新显示真实控件。

动画期间 `VideoGridWidget` 拒绝新的交换请求，避免多个拖放同时修改 `videoWidgets_` 映射。当前仅支持固定 2x2 网格中的两个槽位互换，不支持插入排序、跨窗口拖放或 9/16 宫格。

### 3.4 `MainWindow`

`MainWindow` 保持原有标题和初始尺寸 `1280x720`。它不再创建普通空 `QWidget`，而是将 `VideoGridWidget` 设为中央控件：

```text
旧结构：MainWindow -> 空 QWidget
新结构：MainWindow -> VideoGridWidget -> 4 个 VideoWidget
```

## 4. CMake 框架调整

为避免应用程序与测试目标重复编译或各自维护 UI 文件列表，新增静态库 `rtmp_monitor_ui`：

```text
rtmp_monitor_ui
  ├── VideoWidget
  └── VideoGridWidget

rtmp_monitor_app
  └── StyleLoader

rtmp_monitor
  ├── main.cpp
  ├── MainWindow
  ├── 编译 styles.qrc
  └── 链接 rtmp_monitor_ui 和 rtmp_monitor_app

rtmp_monitor_ui_smoke_test
  ├── 编译 styles.qrc
  └── 链接 rtmp_monitor_ui 和 rtmp_monitor_app
```

同时启用 CTest，并新增 `rtmp_monitor_ui_smoke_test`。该测试没有引入 Qt Test 模块，只依赖现有的 Qt Widgets，因此保持当前依赖最小化。构建应用后，CMake 会将 `app.qss` 复制到可执行文件同级的 `styles/` 目录；外部文件缺失时应用自动回退 QRC 内置样式。

`app.qss` 额外定义 `dragState="pressed"`、`dragSource` 和 `dragTarget` 的样式。所有状态都保持 2 像素边框宽度，避免拖入或点击时因边框尺寸变化导致布局抖动。

## 5. 自动化测试内容

`tests/VideoGridSmokeTest.cpp` 会创建 `QApplication` 和 `VideoGridWidget`，然后验证：

1. 网格中恰好有 4 个视频格。
2. `QGridLayout` 中恰好有 4 个控件。
3. 四个设备名称依次是 `camera001` 到 `camera004`。
4. 每个格子的初始状态都是“未连接”。
5. 每个格子都存在名为 `videoSurface` 的视频占位区域，并声明供 QSS 使用的 `styleRole`。
6. `StyleLoader` 保持单例语义，支持外部 QSS 优先、QRC 回退和不可读外部文件回退。
7. 槽位 0 与槽位 3 交换后，实际 `VideoWidget` 指针、标题、状态和 `videoSurface` 子控件都会随对象移动。
8. 相同索引、越界索引和动画进行中的第二个交换请求都会被拒绝。
9. 动画完成后四个视频格保持可见、可再次拖拽，网格布局仍包含四个控件。

## 6. 本次验证结果

本次使用以下环境完成配置、构建和测试：

```text
编译器：MSVC 19.41.34120.0（Visual Studio 2022）
Qt Kit：E:\QT6\6.6.1\msvc2019_64
构建类型：Debug
```

配置命令：

```powershell
cmake -S . -B "$env:TEMP\rtmp-monitor-week2-check" `
    -G "Visual Studio 17 2022" `
    -A x64 `
    -DCMAKE_PREFIX_PATH="E:/QT6/6.6.1/msvc2019_64" `
    -DBUILD_TESTING=ON
```

构建命令：

```powershell
cmake --build "$env:TEMP\rtmp-monitor-week2-check" --config Debug
```

测试命令：

```powershell
ctest --test-dir "$env:TEMP\rtmp-monitor-week2-check" `
    -C Debug `
    --output-on-failure
```

测试结果：

```text
1/1 Test #1: rtmp_monitor_ui_smoke_test ... Passed
100% tests passed, 0 tests failed
```

本次拖拽换位改造在同一 MSVC 和 Qt 环境下重新构建并通过 CTest。手工验收时，可将 `camera001` 拖到 `camera004`：点击应有短暂蓝色高亮，拖入目标应有虚线提示，松开后两张快照交叉移动，动画结束后两个完整视频格互换位置。

## 7. 本次未实现内容

以下内容刻意留到后续阶段：

- FFmpeg 开发库接入。
- RTMP 拉流与 H.264 解码。
- `AVFrame` 到 `QImage` 的转换。
- 视频帧刷新和显示帧率控制。
- `QThread` 解码线程。
- 设备配置文件、动态增删设备和 9/16 宫格。
- OpenGL 渲染、硬件解码和断线重连。

下一步建议按照项目规划实现 `FFmpegPlayer` 前，先保持本次 `VideoWidget` 的接口稳定，并为其增加接收 `QImage` 的显示接口。这样可以先完成一路播放，再扩展到四路，而不破坏已经完成的网格布局。
