# 第二周：Qt 2x2 视频网格初步实现说明

## 1. 本次目标

本次实现对应 [项目规划](project_plan.md) 中“第 2 周：Qt 空界面和多宫格布局”的初步目标：

- 保留 Qt 6 Widgets、CMake、MSVC、C++17 的工程基础。
- 在 `MainWindow` 中显示固定 2x2 视频宫格。
- 每个视频格显示设备名称、连接状态和黑色视频占位区域。
- 不接入 FFmpeg、RTMP、H.264、线程或真实视频帧。

这一步的定位是先稳定 UI 结构。后续接入解码器时，只需要把 `VideoWidget` 的黑色视频区域替换为真实视频渲染逻辑，不需要重写窗口或网格布局。

## 2. 本次目录与文件调整

```text
include/ui/
├── MainWindow.h
├── VideoGridWidget.h             # 新增：管理固定 2x2 网格
└── VideoWidget.h                 # 新增：单路视频格 UI

src/ui/
├── MainWindow.cpp                # 修改：中央控件改为 VideoGridWidget
├── VideoGridWidget.cpp           # 新增：创建四个 VideoWidget
└── VideoWidget.cpp               # 新增：设备名、状态和黑色视频占位区

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

当前公开接口：

```cpp
void setDeviceName(const QString &deviceName);
void setStatusText(const QString &statusText);
QString deviceName() const;
QString statusText() const;
```

初始默认值是“未命名设备”和“未连接”。`VideoGridWidget` 创建具体设备格后，将它们设置为 `camera001` 到 `camera004`。

黑色视频区域采用独立的 `QFrame`，并使用 `QPalette::Window` 设置为黑色。这样后续可以在此位置增加 `QLabel/QImage` 显示、`paintEvent`，或替换为 `QOpenGLWidget`，不会影响设备标题和状态文本的布局。

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

### 3.3 `MainWindow`

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

rtmp_monitor
  ├── main.cpp
  ├── MainWindow
  └── 链接 rtmp_monitor_ui

rtmp_monitor_ui_smoke_test
  └── 链接 rtmp_monitor_ui
```

同时启用 CTest，并新增 `rtmp_monitor_ui_smoke_test`。该测试没有引入 Qt Test 模块，只依赖现有的 Qt Widgets，因此保持当前依赖最小化。

## 5. 自动化测试内容

`tests/VideoGridSmokeTest.cpp` 会创建 `QApplication` 和 `VideoGridWidget`，然后验证：

1. 网格中恰好有 4 个视频格。
2. `QGridLayout` 中恰好有 4 个控件。
3. 四个设备名称依次是 `camera001` 到 `camera004`。
4. 每个格子的初始状态都是“未连接”。
5. 每个格子都存在名为 `videoSurface` 的视频占位区域，且背景色为黑色。

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
