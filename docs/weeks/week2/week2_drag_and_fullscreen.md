# 第二周拖拽换位与单路全屏功能详解

> 文档分类：Week 2 实现记录。

## 1. 文档目的

本文专门讲解第二周新增的两个交互功能：

1. 在 1～16 路动态视频网格中拖拽两个 `VideoWidget`，交换它们的实际位置。
2. 双击某一路视频进入全屏，并通过双击、`Esc` 或悬浮控制栏退出。

当前项目尚未接入 FFmpeg。这里的黑色 `videoSurface` 是未来视频帧或 OpenGL 纹理的唯一渲染目标。两个功能都围绕“移动真实控件，不复制播放对象”设计，后续接入播放器时不需要推翻现有 UI 架构。

## 2. 组件关系与所有权

```text
MainWindow
├── 顶部工具栏                              添加视频窗口，动画或全屏期间禁用
├── VideoGridWidget                         负责动态槽位、添加和交换动画
│   ├── VideoWidget(Camera 01)
│   │   ├── titleLabel_
│   │   ├── videoSurface_                  未来的真实视频渲染目标
│   │   └── statusLabel_
│   ├── VideoWidget(Camera 02)
│   ├── ...
│   └── VideoWidget(Camera 16)              按需创建，不预创建或隐藏
└── FullscreenVideoWindow                  独立顶层全屏窗口
    ├── videoLayout_                       全屏期间临时承载 videoSurface_
    └── FullscreenControlBar               覆盖在视频上方的悬浮控制栏
```

关键所有权规则：

| 对象 | 常态父对象 | 全屏期间 | 设计原因 |
|---|---|---|---|
| `VideoWidget` | `VideoGridWidget` | 不变 | 保持网格槽位、设备状态和拖拽顺序稳定 |
| `titleLabel_` | `VideoWidget` | 不变 | 全屏只显示视频，不显示普通格子的标题栏 |
| `statusLabel_` | `VideoWidget` | 暂时隐藏 | 避免状态栏留在已隐藏的主窗口中造成状态不一致 |
| `videoSurface_` | `VideoWidget` | 临时变为 `FullscreenVideoWindow` 的子控件 | 同一路视频始终只有一个渲染目标 |
| `FullscreenVideoWindow` | `MainWindow` 管理其生命周期 | 顶层显示 | 将全屏状态和恢复逻辑集中在一个组件内 |

## 3. 拖拽换位功能

### 3.1 为什么交换真实 `VideoWidget`

拖拽结束后，代码交换的是两个 `VideoWidget*` 在 `videoWidgets_` 动态列表和 `QGridLayout` 中的位置，而不是只交换设备名称或状态字符串。因此以下内容会整体移动：

- `titleLabel_` 中的设备名称；
- `videoSurface_` 及未来绑定的渲染器；
- `statusLabel_` 和连接状态；
- `VideoWidget` 自身的信号连接和后续播放器绑定。

这保证了 UI 槽位变化不会破坏“一个设备对象对应一路播放器”的关系。

### 3.2 鼠标事件与视觉状态

`VideoWidget` 负责识别鼠标动作，但不直接修改网格。其拖拽视觉状态为：

```text
Idle
  └─ 左键按下 -> Pressed
       ├─ 移动距离不足 -> 松开后回到 Idle
       └─ 超过 QApplication::startDragDistance()
            -> DragSource
            -> 指针进入另一个 VideoWidget 时，目标变为 DragTarget
            -> 松开后发出 swapRequested(source, target)
```

标题、视频区域和状态标签设置了 `WA_TransparentForMouseEvents`，鼠标事件统一交给外层 `VideoWidget`。这样不会出现子控件吃掉事件、同一次拖拽由多个控件重复处理的问题。

拖拽只接受项目内部定义的 MIME 类型，并拒绝以下请求：

- 来源不是另一个 `VideoWidget`；
- 来源和目标相同；
- 索引越界；
- 上一次交换动画仍在进行；
- 当前视频格已暂时禁用拖拽。

### 3.3 信号流和槽位交换

```text
VideoWidget::dropEvent()
  -> emit swapRequested(source, target)
  -> VideoGridWidget::handleSwapRequested(source, target)
  -> 查找 source/target 在 videoWidgets_ 中的槽位索引
  -> swapVideoWidgets(firstIndex, secondIndex)
  -> 动画结束后 emit videoWidgetsSwapped(firstIndex, secondIndex)
```

`VideoGridWidget` 是槽位数据的唯一修改者。未来需要保存布局配置时，可以监听 `videoWidgetsSwapped`，将设备与槽位的对应关系写入配置文件，而不必侵入拖拽事件代码。

### 3.4 为什么使用快照动画

`QGridLayout` 会主动管理真实控件的几何位置。如果直接对布局中的两个 `VideoWidget` 做 `geometry` 动画，布局可能在动画过程中重新计算尺寸，产生跳动或位置竞争。

当前实现采用双向快照动画：

1. 在交换前调用两个 `VideoWidget::grab()` 获取画面快照。
2. 创建两个鼠标穿透的 `QLabel` 覆盖层，分别显示快照。
3. 暂时隐藏两个真实控件。
4. 交换 `videoWidgets_` 动态列表中的两个指针，并按当前列数重排真实控件。
5. 使用 `QParallelAnimationGroup` 同时移动两张快照。
6. 动画时长为 220ms，缓动曲线为 `OutCubic`。
7. 动画结束后删除快照、显示真实控件并恢复拖拽。

因此动画只是视觉覆盖层在移动，真实控件已由布局稳定管理。动画期间禁用全部视频格的拖拽，避免两个交换事务并发修改同一组槽位。

## 4. 单路全屏功能

### 4.1 类职责

| 类 | 职责 |
|---|---|
| `VideoWidget` | 识别普通模式下的双击，发出 `fullscreenRequested(VideoWidget*)` |
| `VideoGridWidget` | 在没有交换动画时转发全屏请求 |
| `MainWindow` | 创建全屏窗口，协调主窗口隐藏与恢复 |
| `FullscreenVideoWindow` | 转移真实视频区域、保存和恢复布局状态、处理退出事件 |
| `FullscreenControlBar` | 提供设备名、状态、静音、截图和退出按钮 |

`FullscreenVideoWindow` 不复制 `videoSurface_`，也不创建第二个播放器。它只在全屏期间临时改变真实视频区域的父对象和布局归属。

### 4.2 进入全屏的完整顺序

```text
VideoWidget 双击
  -> fullscreenRequested(VideoWidget*)
  -> VideoGridWidget 检查交换动画状态并转发
  -> MainWindow 调用 enterFullscreen(widget)
  -> 状态 Windowed -> Entering
  -> 保存 videoSurface_ 的恢复信息
  -> 从原 QVBoxLayout 移除 videoSurface_
  -> setParent(FullscreenVideoWindow)
  -> 加入零边距 videoLayout_
  -> 恢复扩展尺寸策略并显示 videoSurface_
  -> 更新控制栏设备信息和位置
  -> 激活布局，确认视频区域已填满客户区
  -> 最后调用 showFullScreen()
  -> MainWindow 隐藏
  -> 状态 Entering -> Fullscreen
```

准备工作必须在 `showFullScreen()` 前完成。这样 Windows 第一次合成全屏窗口时就能得到完整的黑色背景和已经进入布局的视频区域，不会先显示一帧白色或透明空窗口。

### 4.3 必须保存和恢复的状态

| 状态 | 保存原因 |
|---|---|
| 原父控件 | 退出时恢复 Qt 父子所有权 |
| 原 `QVBoxLayout` | 把视频区域放回正确布局 |
| 原布局索引 | 保证标题、视频、状态的上下顺序不变 |
| 原 stretch | 保证视频区域恢复后仍占据原来的伸缩比例 |
| 原 `QSizePolicy` | 防止全屏策略污染网格尺寸计算 |
| 原可见状态 | 尊重进入前控件是否可见 |
| 状态标签可见状态 | 退出后精确恢复普通视频格 |

保存数据只在一次全屏会话内有效。`Windowed`、`Entering`、`Fullscreen`、`Exiting` 四态门控会拒绝重复进入和重复退出，避免同一个控件被多次移出布局。

### 4.4 退出全屏的完整顺序

```text
Esc / 全屏双击 / 退出按钮 / 窗口关闭
  -> exitFullscreen()
  -> 状态 Fullscreen -> Exiting
  -> 全屏黑色窗口继续覆盖屏幕
  -> 从 videoLayout_ 移除 videoSurface_
  -> 恢复原 parent、QVBoxLayout、索引、stretch、QSizePolicy 和可见状态
  -> 恢复 statusLabel_ 可见状态并激活原布局
  -> emit fullscreenExited(VideoWidget*)
  -> MainWindow 在全屏窗口后方显示、置顶并完成布局
  -> 最后隐藏 FullscreenVideoWindow
  -> 状态 Exiting -> Windowed
```

这里故意先恢复主窗口、最后隐藏全屏窗口。全屏窗口在整个恢复过程中保持黑色遮挡，避免桌面、普通尺寸窗口或未完成布局的视频格成为肉眼可见的中间帧。

### 4.5 稳定黑色背景

全屏背景由两层共同保证：

1. `FullscreenVideoWindow` 使用黑色 `QPalette`、`setAutoFillBackground(true)`、`Qt::WA_OpaquePaintEvent` 和显式黑色 `paintEvent()`，覆盖完整客户区。
2. `videoSurface_` 使用独立的 `styleRole="videoSurface"` 和黑色背景，不依赖它是否仍是 `VideoWidget` 的后代。

QSS 不能写成只在 `VideoWidget` 后代关系下才生效的规则，否则 `setParent()` 后选择器不再匹配，视频区域会失去黑色背景。全屏视频布局的 margin 和 spacing 均为 0，视频区域会完整铺满窗口。

### 4.6 Overlay 控制栏

`FullscreenControlBar` 是全屏窗口的直接子控件，但不加入 `videoLayout_`，因此不会占用或压缩视频渲染区域。`resizeEvent()` 中按以下公式定位：

```text
x = (windowWidth - controlBarWidth) / 2
y = windowHeight - controlBarHeight - 20px
```

定位完成后调用 `raise()`，确保控制栏显示在视频区域上方。QSS 为控制栏提供深色半透明背景和圆角，按钮仍能正常接收鼠标点击。

控制栏自动隐藏规则：

- 进入全屏时立即显示并启动 2500ms 单次定时器；
- 鼠标在窗口内移动会恢复光标；
- 鼠标进入底部 120px 触发区时显示控制栏并重置定时器；
- 鼠标位于控制栏内部时暂停自动隐藏；
- 鼠标离开控制栏后重新开始计时；
- 超时后隐藏控制栏，并把光标设置为 `Qt::BlankCursor`。

静音和截图按钮当前只发出信号，不执行音频控制或文件写入。它们是后续播放器控制层的接口预留。

### 4.7 双击事件为何不会重复触发

当前事件路径有意保持单一：

- 普通模式双击只由 `VideoWidget::mouseDoubleClickEvent()` 处理；
- 全屏双击只由 `FullscreenVideoWindow::mouseDoubleClickEvent()` 处理；
- `VideoWidget` 和 `videoSurface_` 没有安装处理双击的 `eventFilter`；
- 全屏窗口接受双击事件后直接退出，不再调用基类实现；
- 每层信号只连接一次；
- 状态机拒绝 `Entering` 或 `Exiting` 期间的重复调用。

Debug 构建还会输出事件时间戳、接收对象、事件类型和全屏状态。再次出现切换问题时，可以把日志时间与录屏帧对齐，判断是否存在重复信号或事件传播。

## 5. 两个功能如何协同

拖拽和全屏都可能改变控件的显示位置，因此通过明确边界避免冲突：

| 场景 | 处理方式 |
|---|---|
| 交换动画期间双击 | `VideoGridWidget` 拒绝转发全屏请求 |
| 添加动画期间拖拽或双击 | 统一状态保持 `AddingWidget`，请求被拒绝 |
| 全屏期间拖拽 | 主窗口隐藏，普通视频格无法接收拖拽 |
| 全屏期间添加 | 工具栏随主窗口隐藏，网格状态同时禁止添加 |
| 拖拽后进入全屏 | 使用交换后的真实 `VideoWidget`，设备和渲染目标保持一致 |
| 全屏退出 | `VideoWidget` 从未离开网格，因此恢复到进入前的槽位 |
| 未来绑定 FFmpeg 播放器 | 播放器继续绑定同一个 `VideoWidget`/`videoSurface_`，无需重建解码线程 |

核心不变量是：添加只创建一个新对象，拖拽移动整个 `VideoWidget`，全屏只临时移动其中唯一的 `videoSurface_`。三者修改的层级不同，并通过 `GridInteractionState` 串行执行。

## 6. 主要信号与槽

| 发送方 | 信号 | 接收方/用途 |
|---|---|---|
| `VideoWidget` | `swapRequested(source, target)` | `VideoGridWidget` 执行槽位交换 |
| `VideoGridWidget` | `videoWidgetAdded(widget)` | 添加动画完成并恢复交互 |
| `VideoGridWidget` | `gridInteractionStateChanged(state)` | `MainWindow` 更新添加动作 |
| `VideoGridWidget` | `videoWidgetsSwapped(first, second)` | 通知未来的布局持久化模块 |
| `VideoWidget` | `fullscreenRequested(widget)` | `VideoGridWidget` 检查后转发 |
| `VideoGridWidget` | 全屏请求转发信号 | `MainWindow` 进入全屏 |
| `FullscreenControlBar` | `exitRequested()` | `FullscreenVideoWindow` 退出全屏 |
| `FullscreenControlBar` | `muteRequested()` | 预留给播放器音频控制 |
| `FullscreenControlBar` | `screenshotRequested()` | 预留给截图服务 |
| `FullscreenVideoWindow` | `fullscreenExitStarted(widget)` | 网格进入 `ExitingFullscreen` |
| `FullscreenVideoWindow` | `fullscreenExited(widget)` | `MainWindow` 恢复主窗口 |

## 7. 建议的代码阅读顺序

1. `include/common/ui/VideoWidget.h` 与 `src/common/ui/VideoWidget.cpp`：理解单个格子的结构、拖拽状态和双击入口。
2. `include/common/ui/VideoGridWidget.h` 与 `src/common/ui/VideoGridWidget.cpp`：理解动态列表、布局算法、快照动画和统一状态。
3. `include/common/ui/FullscreenControlBar.h` 与 `src/common/ui/FullscreenControlBar.cpp`：理解 Overlay 的按钮接口。
4. `include/common/ui/FullscreenVideoWindow.h` 与 `src/common/ui/FullscreenVideoWindow.cpp`：重点阅读状态保存、父对象切换和恢复顺序。
5. `src/common/ui/MainWindow.cpp`：理解普通窗口和全屏顶层窗口的协调。
6. `resources/styles/app.qss`：查看拖拽状态、视频区域和控制栏的样式边界。
7. `tests/VideoGridSmokeTest.cpp`：查看对象身份、布局恢复和事件门控的自动验证方法。
8. `tests/VideoGridDynamicTest.cpp`：查看 1～16 路布局和交互互斥的数据驱动验证。

## 8. 自动测试覆盖

当前 UI 冒烟测试重点验证：

- 交换后两个槽位中的 `VideoWidget*` 地址真正互换；
- 设备名、状态和 `videoSurface_` 子控件随对象移动；
- 相同索引、越界索引和动画期间的重复交换会被拒绝；
- 动画结束后参与交换的视频格可见且全部视频格重新允许拖拽；
- 1～16 路的动态行列和逻辑索引位置正确；
- 新创建的视频格具备与已有控件相同的拖拽和全屏连接；
- 全屏使用原来的同一个 `videoSurface_`；
- 退出后父对象、布局索引、尺寸策略和可见状态完整恢复；
- 全屏控制栏位于窗口底部中央；
- 交换动画期间不会进入全屏；
- 双击退出只触发一次恢复，不会再次进入全屏；
- 全屏窗口保持不透明黑色，视频布局为零边距。

## 9. 手工测试步骤

### 9.1 拖拽换位

1. 启动程序并添加到至少四路，确认显示 `Camera 01` 至 `Camera 04`。
2. 按下 `Camera 01`，确认边框出现按下高亮。
3. 拖动到 `Camera 04`，确认来源弱化、目标出现拖放提示。
4. 松开鼠标，确认两张快照同时向对方位置移动。
5. 动画结束后确认两个完整视频格互换，另外两个格子不变。
6. 立即执行第二次交换，确认拖拽已恢复可用。

### 9.2 单路全屏

1. 双击任一视频格，确认其所在显示器立即出现纯黑全屏画面。
2. 确认视频区域铺满客户区，没有白边、灰底或布局间隙。
3. 确认底部中央控制栏覆盖在画面上方，没有压缩视频区域。
4. 静止约 2.5 秒，确认控制栏与鼠标光标隐藏。
5. 移动到屏幕底部，确认控制栏与光标恢复。
6. 把鼠标停在控制栏内，确认控制栏不会自动消失。
7. 分别使用全屏双击、`Esc` 和退出按钮退出。
8. 每次退出后确认当前动态网格只恢复一次，没有闪白、跳变或再次进入全屏。
9. 先拖拽交换两个格子，再进入并退出其中一路，确认交换后的顺序保持不变。

## 10. 后续接入 FFmpeg 时的注意事项

- 解码线程不能直接操作 `QWidget`；应通过信号把帧交给 UI 线程。
- 播放器或渲染器应绑定稳定的 `VideoWidget`/`videoSurface_` 对象，不应绑定固定网格坐标。
- 全屏切换只改变渲染控件的父对象，不应停止和重建解码器。
- 若将 `videoSurface_` 替换为 `QOpenGLWidget`，需要重新验证跨顶层窗口 `setParent()` 的 OpenGL 上下文行为；必要时改为共享上下文或独立渲染承载层。
- 退出全屏和程序关闭必须在 UI 线程执行，并保证视频区域先恢复父对象，再销毁全屏窗口。
- 截图应从解码帧或渲染器读取图像，不建议直接截取包含控制栏的全屏窗口。

## 11. 本阶段边界

本阶段已经完成 1～16 路动态布局、添加动画、拖拽交换、单路全屏、Overlay 控制栏、自动隐藏、稳定黑色背景和布局恢复。以下内容仍属于后续阶段：

- FFmpeg 拉流和 H.264 解码；
- 真实视频帧渲染；
- 静音和截图业务逻辑；
- OpenGL 和硬件解码；
- 删除视频窗口和布局顺序持久化；
- 布局顺序持久化；
- 多屏全屏记忆和运行时主题切换。
