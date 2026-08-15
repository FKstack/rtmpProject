# 第二周动态视频网格与添加动画详解

> 文档分类：Week 2 实现记录。

## 1. 功能范围

本阶段把原来的固定 2x2 四宫格改为真实的动态视频网格：

- 程序启动时只创建 `Camera 01`。
- 用户点击主窗口顶部“添加视频窗口”逐个创建新控件。
- 最多创建 16 个 `VideoWidget`，最大布局为 4x4。
- 每次添加只创建一个新控件，已有控件不会被销毁或重建。
- 添加、拖拽交换和全屏切换互斥。
- 当前仍不接入 FFmpeg，黑色 `videoSurface` 是未来唯一的视频渲染目标。

## 2. 动态布局规则

| 视频数量 | 行列 |
|---:|:---:|
| 1 | 1x1 |
| 2 | 1x2 |
| 3～4 | 2x2 |
| 5～6 | 2x3 |
| 7～9 | 3x3 |
| 10～12 | 3x4 |
| 13～16 | 4x4 |

`VideoGridWidget::calculateGridDimensions()` 使用整数算法计算列数：从 1 开始增加，直到
`columns * columns >= widgetCount`，且列数不超过 4；行数使用向上取整：

```text
rows = (widgetCount + columns - 1) / columns
```

这套算法没有为 1～16 分别编写布局分支，结果仍与上表完全一致。输入 0 或大于 16 时返回
`{0, 0}`，由调用方视为无效数量。

## 3. 逻辑顺序

`VideoGridWidget` 使用以下容器保存唯一顺序：

```cpp
QVector<VideoWidget *> videoWidgets_;
```

索引同时表示逻辑顺序和视觉顺序：

```text
row = index / columns
column = index % columns
```

`QGridLayout::itemAt()` 只用于测试布局结果，不参与业务顺序推断。拖拽交换修改的是
`videoWidgets_` 中两个真实指针；下一次添加或重排仍会沿用交换后的顺序。

## 4. 控件创建和数量上限

构造函数只调用一次私有创建函数。每次用户添加时执行同一条真实创建路径：

```text
new VideoWidget(this)
  -> 分配 Camera 编号和唯一 objectName
  -> 设置“未连接”状态
  -> 连接 swapRequested
  -> 连接 fullscreenRequested
  -> 追加到 videoWidgets_
  -> 重新布局
```

最大数量只由 `VideoGridWidget::kMaximumVideoWidgetCount` 定义。`MainWindow` 查询
`canAddVideoWidget()` 更新工具栏动作，不维护第二份控件列表或上限。

添加第 16 路后：

- 添加动作被禁用。
- 工具提示显示“已达到最多 16 个视频窗口”。
- 状态栏显示 3 秒上限提示。
- 程序化调用第 17 次添加也会返回 `nullptr`，逻辑列表不会增长。

本阶段没有删除功能。以后加入删除后，只要数量降到 16 以下且状态回到 `Idle`，现有
`canAddVideoWidget()` 和主窗口刷新逻辑就会重新启用添加动作。

## 5. 重新布局

`relayoutVideoWidgets()` 只调整已有控件的布局归属：

1. 从 `QGridLayout` 移除所有 `VideoWidget`，但不删除控件。
2. 将 0～3 行和列的旧 stretch 清零。
3. 根据当前数量计算行列。
4. 按逻辑索引重新加入布局。
5. 为有效行列设置 stretch 1。
6. 调用 `invalidate()` 和 `activate()` 计算新几何。

这避免从 4 列切回较少列数时残留无效 stretch。`VideoWidget` 使用
`QSizePolicy::Expanding` 和 `160x100` 最小尺寸，使普通窗口、最大化窗口和 4x4 布局都能合理扩展。

## 6. 添加动画

### 6.1 为什么不动画真实控件

`QGridLayout` 会持续管理真实控件的 geometry。如果同时对布局内的 `VideoWidget` 创建
`QPropertyAnimation(widget, "geometry")`，布局和动画会竞争同一个属性，产生跳动或瞬移。

项目继续使用拖拽功能已经验证过的快照覆盖层：真实控件先进入最终布局，用户看到的是临时
`QLabel` 快照在其上方移动。

### 6.2 添加流程

```text
Idle
  -> 状态改为 AddingWidget，立即禁用添加和拖拽
  -> 保存所有旧控件的几何与 grab() 快照
  -> 创建一个新 VideoWidget
  -> 重排真实 QGridLayout 并获取目标几何
  -> 创建旧控件和新控件的快照覆盖层
  -> 隐藏真实控件
  -> 旧快照从旧位置移动、缩放到新位置
  -> 新快照从目标中心 85% 尺寸放大到 100%
  -> 新快照透明度从 0 变为 1
  -> 220ms OutCubic 并行动画结束
  -> 删除快照、显示真实控件、恢复 Idle
```

透明效果只附加到临时快照，不会进入真实 `videoSurface`，因此不会影响未来的
`QOpenGLWidget`、原生窗口或 FFmpeg 渲染目标。

动画项通过 `QPointer` 保存控件和快照。动画组以 `VideoGridWidget` 为 parent，并使用
`DeleteWhenStopped` 管理生命周期。网格不可见或几何无效时会安全完成无动画重排，不会留下
隐藏控件或卡在 `AddingWidget`。

## 7. 统一交互状态

```cpp
enum class GridInteractionState {
    Idle,
    AddingWidget,
    SwappingWidgets,
    EnteringFullscreen,
    Fullscreen,
    ExitingFullscreen
};
```

| 状态 | 添加 | 拖拽 | 新全屏请求 |
|---|:---:|:---:|:---:|
| `Idle` | 允许 | 允许 | 允许 |
| `AddingWidget` | 禁止 | 禁止 | 禁止 |
| `SwappingWidgets` | 禁止 | 禁止 | 禁止 |
| `EnteringFullscreen` | 禁止 | 禁止 | 禁止 |
| `Fullscreen` | 禁止 | 禁止 | 禁止 |
| `ExitingFullscreen` | 禁止 | 禁止 | 禁止 |

状态变化时 `VideoGridWidget` 统一更新所有视频格的 drag enabled 状态并通知 `MainWindow`。
工具栏动作因此会在动画启动的同一事件处理中立即禁用，快速连续点击不会排队或创建重复控件。

## 8. 与拖拽交换协作

拖拽交互没有改变：来源和目标仍由 `VideoWidget` 识别，槽位交换仍由 `VideoGridWidget` 执行。
变化仅包括：

- 索引范围来自 `videoWidgets_.size()`。
- 交换后使用当前动态列数重新布局。
- 交换的是 `videoWidgets_` 中的真实指针。
- 交换与添加共用快照创建和 220ms 几何动画约定。
- `SwappingWidgets` 状态期间添加动作和其他拖拽都会被拒绝。

因此 2、3、5、10 或 16 个视频格时，拖拽后的视觉槽位和逻辑顺序仍保持一致。

## 9. 与全屏协作

全屏窗口依然只临时移动所选 `VideoWidget` 内的真实 `videoSurface`。外层 `VideoWidget` 留在
动态网格中，`videoWidgets_` 的顺序不变。

```text
VideoWidget 双击
  -> 网格进入 EnteringFullscreen
  -> MainWindow 调用 FullscreenVideoWindow::enterFullscreen()
  -> 成功后网格进入 Fullscreen，主窗口及添加工具栏隐藏

退出开始
  -> FullscreenVideoWindow 发出 fullscreenExitStarted
  -> 网格进入 ExitingFullscreen
  -> videoSurface 恢复原 VideoWidget 内部布局
  -> fullscreenExited
  -> 网格回到 Idle，主窗口显示
```

全屏期间数量和逻辑顺序不能变化，因此退出时不需要保存固定 row/column，也不会恢复到错误槽位。
原有纯黑背景、零边距、双击防重复和最后隐藏全屏窗口的防闪烁顺序保持不变。

## 10. 自动化测试

项目现有两个 CTest 目标：

| 测试 | 覆盖内容 |
|---|---|
| `rtmp_monitor_ui_smoke_test` | QSS、动态添加到 4 路、拖拽对象交换、全屏转移和恢复 |
| `rtmp_monitor_dynamic_grid_test` | 1～16 行列、数量上限、唯一名称、布局位置、状态互斥和新增控件信号 |

实际验证命令：

```powershell
cmake --preset Qt-Debug
cmake --build out/build-windows-x64/debug
ctest --test-dir out/build-windows-x64/debug --output-on-failure
```

本次结果：

```text
2/2 tests passed
100% tests passed, 0 tests failed
```

直接在未初始化的普通 PowerShell 中运行 Ninja 构建时，MSVC 可能找不到标准库头。应先进入
Visual Studio Developer PowerShell，或执行 `VsDevCmd.bat` 初始化编译环境；这不是项目包含路径问题。

## 11. 手工验收清单

本次已完成的运行级检查：Debug 可执行程序成功创建标题为“PC 端多路 RTMP 视频显示”的主窗口，
持续运行 5 秒期间进程未退出且 Windows 报告窗口保持响应。由于当前自动化环境无法连接 Windows
界面控制服务，下列鼠标操作和肉眼动画项目未冒充已完成结果，仍需在本机按清单验收。

1. 启动时确认只有 `Camera 01`，布局为 1x1。
2. 逐次添加并观察 2、3、4、5、6、7、9、10、12、13、16 路布局。
3. 确认旧视频格平滑移动，新视频格轻微放大并淡入。
4. 快速连续点击添加，确认动画期间按钮禁用且数量只增加一次。
5. 在 3、5、7、10、16 路分别拖拽交换。
6. 在 1、3、5、10、16 路分别通过双击、Esc 和控制栏退出全屏。
7. 连续执行添加、拖拽、全屏、退出、再次添加。
8. 调整窗口尺寸并最大化，确认格子均匀扩展。
9. 到达 16 路后确认按钮禁用、工具提示和状态栏反馈正确。

自动测试能确认对象身份、状态、布局位置和信号次数，但不能替代肉眼判断动画流畅度和显示器
合成闪烁。视觉结果必须按本清单单独验收。

## 12. 后续扩展注意事项

- 添加删除功能时，应从 `videoWidgets_` 移除指定对象后重排，不要重建其他视频格。
- 设备 ID 应与显示名称分离；当前 Camera 编号只用于 UI 原型。
- FFmpeg 解码器应绑定稳定的视频格或渲染目标，不应绑定行列坐标。
- `QOpenGLWidget` 或原生窗口跨顶层窗口 reparent 可能涉及图形上下文重建，需要单独验证。
- 大量真实视频快照可能增加瞬时 GPU/内存开销；16 路高分辨率接入后应做性能测量。
