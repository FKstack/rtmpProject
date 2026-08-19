# 第二周：Qt 动态视频网格实现说明

> 文档分类：Week 2 实现记录。

## 1. 当前目标

第二周 UI 已从最初的固定 2x2 原型扩展为可交互的动态多路监控框架：

- Qt 6 Widgets、CMake 和 C++17 工程保持不变；Windows 使用 MSVC，目录边界同时为 Linux ARM64 预留。
- 启动时创建一个 `Camera 01` 视频格。
- 用户可从主窗口工具栏逐个添加到 16 路。
- 根据数量自动使用 1x1 至 4x4 网格。
- 支持真实视频格拖拽交换和快照动画。
- 支持单路全屏、悬浮控制栏和稳定退出恢复。
- 当前不接入 FFmpeg、RTMP、H.264 或真实视频帧。

专项说明：

- [动态网格与添加动画详解](week2_dynamic_grid.md)
- [拖拽换位与单路全屏详解](week2_drag_and_fullscreen.md)
- [QSS 样式加载说明](../../guides/development/style_loading.md)

## 2. 当前文件结构

```text
include/common/ui/
├── FullscreenControlBar.h
├── FullscreenVideoWindow.h
├── MainWindow.h
├── VideoGridWidget.h
└── VideoWidget.h

src/common/ui/
├── FullscreenControlBar.cpp
├── FullscreenVideoWindow.cpp
├── MainWindow.cpp
├── VideoGridWidget.cpp
└── VideoWidget.cpp

resources/styles/
└── app.qss

tests/
├── VideoGridSmokeTest.cpp
└── VideoGridDynamicTest.cpp
```

## 3. UI 组件职责

### 3.1 `VideoWidget`

单个视频格包含：

```text
VideoWidget
├── deviceNameLabel
└── videoSurface
    └── statusLabel
```

它负责设备名称、状态、黑色视频区域、拖拽事件和双击全屏请求，不负责网格行列，也不持有
FFmpeg 解码器。标题、状态和视频区域继续作为同一个对象整体参与拖拽交换。

### 3.2 `VideoGridWidget`

`VideoGridWidget` 是视频格逻辑顺序和生命周期的唯一拥有者：

- `QVector<VideoWidget *>` 保存 1～16 个真实控件。
- `calculateGridDimensions()` 计算动态行列。
- `addVideoWidget()` 创建一路并启动添加动画。
- `swapVideoWidgets()` 交换逻辑顺序和实际控件。
- 统一状态机协调添加、交换和全屏。
- `QGridLayout` 只负责视觉排列，不作为业务顺序来源。

当前布局表：

| 数量 | 布局 |
|---:|:---:|
| 1 | 1x1 |
| 2 | 1x2 |
| 3～4 | 2x2 |
| 5～6 | 2x3 |
| 7～9 | 3x3 |
| 10～12 | 3x4 |
| 13～16 | 4x4 |

### 3.3 `MainWindow`

主窗口创建不可移动的顶部工具栏和“添加视频窗口”动作。它只调用
`VideoGridWidget::addVideoWidget()` 并根据公开查询和信号更新按钮，不维护第二份视频格列表。

达到 16 路后，按钮禁用、工具提示说明上限，状态栏显示明确反馈。全屏时主窗口整体隐藏，
工具栏不会覆盖全屏画面。

### 3.4 `FullscreenVideoWindow`

全屏窗口继续临时迁移真实 `videoSurface`，并在退出时恢复原父对象、内部纵向布局索引、
stretch、尺寸策略和可见状态。动态网格在全屏期间禁止重排，因此退出后自然回到原逻辑槽位。

## 4. 动画设计

添加和拖拽都不直接动画 `QGridLayout` 管理的真实控件。两者使用临时 `QLabel` 快照：

- 拖拽交换：两张快照在 220ms 内双向移动。
- 添加重排：旧快照移动到新槽位，新快照从 85% 尺寸放大并淡入。
- 缓动曲线统一为 `QEasingCurve::OutCubic`。
- 动画完成后删除快照、显示真实控件并恢复交互。

快照策略避免布局系统与 `geometry` 动画竞争，也为未来原生视频表面保留稳定对象身份。

## 5. 交互互斥

网格状态包括 `Idle`、`AddingWidget`、`SwappingWidgets`、`EnteringFullscreen`、
`Fullscreen` 和 `ExitingFullscreen`。

只有 `Idle` 可以开始添加、交换或全屏。状态切换会同时更新：

- 所有 `VideoWidget` 的拖拽开关。
- 主窗口添加动作的 enabled 状态和工具提示。
- 全屏进入或退出期间的布局保护。

快速点击不会排队；动画期间的重复请求直接被拒绝。

## 6. QSS 边界

样式仍通过 `StyleLoader` 在应用启动时统一加载。新增工具栏选择器限定为
`QToolBar#videoToolBar`，视频格、视频区域、拖拽状态和全屏控制栏继续使用独立 `styleRole`。

修改以下名称视为样式 API 变化，必须同步更新 QSS 和测试：

- `videoToolBar`
- `videoWidget`
- `deviceNameLabel`
- `videoSurface`
- `statusLabel`
- `fullscreenControlBar`

## 7. 构建与测试

在 Visual Studio Developer PowerShell 中执行：

```powershell
cmake --preset Qt-Debug
cmake --build out/build-windows-x64/debug
ctest --test-dir out/build-windows-x64/debug --output-on-failure
```

本次 Debug 构建成功，CTest 结果：

```text
1/2 rtmp_monitor_ui_smoke_test        Passed
2/2 rtmp_monitor_dynamic_grid_test    Passed
100% tests passed, 0 tests failed
```

自动化覆盖布局算法、初始一路、添加至 16 路、上限、唯一指针和名称、视觉位置、状态互斥、
动态拖拽连接、主窗口添加动作禁用、全屏转移恢复、黑色背景和 QSS 回退。

Debug 程序已实际启动并保持响应 5 秒。受当前 Windows 界面自动化权限限制，鼠标拖拽、连续点击
和肉眼动画流畅度没有被标记为自动通过，仍需使用下方人工清单验证。

肉眼动画流畅度、不同显示器合成和真实鼠标拖拽仍需按
[动态网格手工验收清单](week2_dynamic_grid.md#11-手工验收清单)执行。

## 8. 本阶段未实现

- FFmpeg 拉流、H.264 解码和真实帧渲染。
- 删除视频窗口和布局持久化。
- 静音、截图的实际业务逻辑。
- OpenGL 或硬件解码。
- 设备列表、离线管理和 RTMP URL 配置。
