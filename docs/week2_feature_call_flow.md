# 第二周功能调用链详解：拖拽、全屏与动态添加

> 适用对象：第一次阅读本项目 Qt Widgets 代码的开发者。
> 本文对应当前 `master` 分支实现，重点解释拖拽交换、单路全屏和动态添加视频窗口三个功能是如何从用户操作一步步执行到结束的。

## 1. 阅读目标

读完本文后，应当能够回答以下问题：

1. 用户点击、拖拽或双击后，最先执行哪个函数？
2. `VideoWidget` 发出的信号在哪里连接，接收者是谁？
3. 为什么真正修改网格的是 `VideoGridWidget`，而不是单个 `VideoWidget`？
4. 为什么动画移动的是快照，而不是真实视频控件？
5. 全屏时究竟移动了哪个控件，退出时如何恢复？
6. 添加、交换和全屏为什么不会同时修改布局？

本文只解释第二周 UI 功能。当前项目尚未接入 FFmpeg，因此文中的 `videoSurface_` 仍是黑色视频占位区域。

## 2. 先认识五个核心类

| 类 | 所在文件 | 主要职责 | 不负责的事情 |
| --- | --- | --- | --- |
| `MainWindow` | `include/common/ui/MainWindow.h`、`src/common/ui/MainWindow.cpp` | 创建工具栏和网格，协调全屏窗口，更新“添加视频窗口”按钮 | 不计算网格位置，不直接交换视频格 |
| `VideoGridWidget` | `include/common/ui/VideoGridWidget.h`、`src/common/ui/VideoGridWidget.cpp` | 拥有全部 `VideoWidget`，维护逻辑顺序，计算布局，执行添加和交换动画，管理交互状态 | 不直接处理鼠标拖放细节，不显示全屏顶层窗口 |
| `VideoWidget` | `include/common/ui/VideoWidget.h`、`src/common/ui/VideoWidget.cpp` | 显示单个视频格，处理按下、拖拽、放下和双击，发出操作请求 | 不决定自己在第几行第几列，不自行进入全屏 |
| `FullscreenVideoWindow` | `include/common/ui/FullscreenVideoWindow.h`、`src/common/ui/FullscreenVideoWindow.cpp` | 临时承载真实 `videoSurface_`，控制全屏进入和退出，管理黑色背景、控制栏与自动隐藏 | 不创建第二份视频渲染对象，不修改网格逻辑顺序 |
| `FullscreenControlBar` | `include/common/ui/FullscreenControlBar.h`、`src/common/ui/FullscreenControlBar.cpp` | 显示设备信息、退出、静音和截图按钮，发出按钮和鼠标进出信号 | 不直接操作播放器或全屏窗口状态 |

可以先记住下面这条职责边界：

```text
VideoWidget 识别用户意图
    -> VideoGridWidget 判断当前是否允许执行
        -> MainWindow 或网格动画执行实际操作
            -> 完成后回到 Idle 状态
```

## 3. 应用启动时发生了什么

三个功能都依赖同一套对象关系，因此先从程序启动看起。

### 3.1 创建对象

1. `src/main.cpp` 创建 `QApplication`。
2. `main.cpp` 创建 `MainWindow mainWindow`。
3. `MainWindow::MainWindow()` 创建顶部 `QToolBar`。
4. 工具栏通过 `addAction()` 创建 `addVideoAction_`，文字为“添加视频窗口”。
5. `MainWindow::MainWindow()` 创建 `VideoGridWidget`，并通过 `setCentralWidget()` 将它设为主窗口中心控件。
6. `VideoGridWidget::VideoGridWidget()` 创建 `QGridLayout`。
7. 构造函数调用 `createVideoWidget()`，真实创建第一个 `VideoWidget`。
8. 第一个视频格被加入 `videoWidgets_`，然后调用 `relayoutVideoWidgets()`，形成 `1 x 1` 布局。
9. `MainWindow::MainWindow()` 创建一个 `FullscreenVideoWindow`，但此时不显示。
10. `MainWindow` 建立添加、全屏退出和界面状态更新所需的信号槽连接。

### 3.2 谁拥有谁

```text
MainWindow
├── VideoGridWidget                    主窗口中心控件
│   ├── QGridLayout
│   └── QVector<VideoWidget *>         逻辑顺序和视觉顺序的唯一来源
│       └── VideoWidget
│           ├── titleLabel_
│           └── videoSurface_
│               └── statusLabel_
└── FullscreenVideoWindow              独立顶层窗口，由 MainWindow 管理生命周期
    └── FullscreenControlBar
```

正常状态下，`VideoWidget` 的父对象是 `VideoGridWidget`。全屏时只有内部的 `videoSurface_` 会临时改为 `FullscreenVideoWindow` 的子控件，外层 `VideoWidget` 仍留在网格中。

## 4. 统一交互状态机

`VideoGridWidget::GridInteractionState` 是三个功能之间的互斥开关。

| 状态 | 含义 | 是否允许添加 | 是否允许拖拽 | 是否允许进入全屏 |
| --- | --- | ---: | ---: | ---: |
| `Idle` | 网格空闲 | 是，未达到 16 路时 | 是 | 是 |
| `AddingWidget` | 正在添加并播放布局动画 | 否 | 否 | 否 |
| `SwappingWidgets` | 正在交换两个视频格 | 否 | 否 | 否 |
| `EnteringFullscreen` | 已接受全屏请求，正在等待进入结果 | 否 | 否 | 否 |
| `Fullscreen` | 某一路正在全屏 | 否 | 否 | 否 |
| `ExitingFullscreen` | 正在恢复视频区域父子关系 | 否 | 否 | 否 |

所有状态变化都经过 `VideoGridWidget::setInteractionState()`：

1. 更新 `interactionState_`。
2. 调用 `setDragEnabledForAll()`。
3. 只有状态为 `Idle` 时，所有 `VideoWidget` 才重新允许拖拽和双击全屏。
4. 发出 `gridInteractionStateChanged()`。
5. `MainWindow` 收到该信号后调用 `updateAddVideoAction()`，同步更新添加按钮的启用状态。

这套状态机是理解三个功能不会互相打架的关键。

## 5. 功能一：拖拽交换视频窗口

### 5.1 功能入口在哪里

拖拽从 `VideoWidget` 的鼠标事件开始，核心入口依次是：

```text
mousePressEvent()
    -> mouseMoveEvent()
        -> startDrag()
            -> 目标 VideoWidget::dropEvent()
```

标题、视频区域和状态标签都设置了 `Qt::WA_TransparentForMouseEvents`。因此用户在视频格内部任意位置按下鼠标时，事件最终由外层 `VideoWidget` 处理，不会被内部标签截断。

### 5.2 第一步：按下鼠标

执行 `VideoWidget::mousePressEvent()`：

1. 检查 `dragEnabled_` 是否为 `true`。
2. 检查是否为鼠标左键。
3. 将按下位置保存到 `dragStartPosition_`。
4. 将 `mousePressed_` 设为 `true`。
5. 调用 `setDragState(DragState::Pressed)`。
6. QSS 动态属性 `dragState="pressed"` 生效，视频格显示点击高亮。

如果只是点击，没有拖动到足够距离，`mouseReleaseEvent()` 会启动一个 140ms 的单次回调，随后把状态恢复为 `Idle`。

### 5.3 第二步：判断是否真正开始拖拽

执行 `VideoWidget::mouseMoveEvent()`：

1. 检查拖拽是否启用、鼠标是否仍按下、左键是否仍处于按下状态。
2. 使用当前位置减去 `dragStartPosition_`。
3. 通过 `manhattanLength()` 计算移动距离。
4. 将移动距离与 `QApplication::startDragDistance()` 比较。
5. 只有超过系统拖拽阈值才调用 `startDrag()`，这样普通点击不会被误判为拖拽。

### 5.4 第三步：创建 Qt 拖放对象

执行 `VideoWidget::startDrag()`：

1. 创建 `QDrag`，父对象为当前源 `VideoWidget`。
2. 创建 `QMimeData`。
3. 写入项目自定义 MIME 类型 `application/x-rtmp-monitor-video-widget`。
4. 调用当前视频格的 `grab()` 生成拖拽图片。
5. 使用 `dragStartPosition_` 设置热点位置，避免拖拽图片突然跳动。
6. 调用 `setDragState(DragState::DragSource)`，QSS 将拖拽源弱化。
7. 调用 `drag->exec(Qt::MoveAction)`，进入 Qt 的拖放事件循环。

`QDrag::exec()` 返回时，说明拖放已经成功、取消或失败。失败时源视频格会恢复 `Idle` 样式。

### 5.5 第四步：目标视频格接受拖入

鼠标进入另一个 `VideoWidget` 后，目标控件依次处理：

1. `dragEnterEvent()` 调用 `isVideoWidgetDrag()`。
2. `isVideoWidgetDrag()` 检查事件来源是不是另一个 `VideoWidget`。
3. 检查 MIME 类型是否正确。
4. 检查源和目标不是同一个对象。
5. 条件满足后，目标调用 `setDragState(DragState::DragTarget)`。
6. QSS 的 `dragState="dragTarget"` 选择器显示蓝色虚线目标边框。
7. `dragMoveEvent()` 在移动过程中持续维持目标状态。
8. 如果鼠标离开，`dragLeaveEvent()` 把目标恢复为 `Idle`。

### 5.6 第五步：松开鼠标并发出交换请求

在目标格上松开鼠标时执行 `VideoWidget::dropEvent()`：

1. 从 `event->source()` 取得源 `VideoWidget*`。
2. 再次检查源、目标、拖拽启用状态和 MIME 类型。
3. 接受 `MoveAction`。
4. 发出信号：

```cpp
emit swapRequested(source, this);
```

这里的 `this` 是目标视频格。`VideoWidget` 到此只表达“请交换这两个对象”，它不直接修改网格。

### 5.7 信号在哪里连接

每个视频格都由 `VideoGridWidget::createVideoWidget()` 创建。创建过程中会调用 `connectVideoWidgetSignals()`，其中建立连接：

```cpp
connect(videoWidget, &VideoWidget::swapRequested,
        this, &VideoGridWidget::handleSwapRequested,
        Qt::UniqueConnection);
```

因此信号槽关系为：

```text
VideoWidget::swapRequested(source, target)
    -> VideoGridWidget::handleSwapRequested(source, target)
```

使用 `Qt::UniqueConnection` 可以避免同一个视频格被重复连接。

### 5.8 第六步：把对象指针转换为逻辑索引

执行 `VideoGridWidget::handleSwapRequested()`：

1. 调用 `indexOf(source)`，在 `videoWidgets_` 中查找源对象索引。
2. 调用 `indexOf(target)`，查找目标对象索引。
3. 两个索引都有效时，调用 `swapVideoWidgets(sourceIndex, targetIndex)`。

这里不能只使用 `QGridLayout::itemAt()` 推断顺序。项目明确把 `videoWidgets_` 当作唯一逻辑顺序，布局只是这一顺序的显示结果。

### 5.9 第七步：交换逻辑顺序并播放动画

执行 `VideoGridWidget::swapVideoWidgets()`：

1. 确认当前状态是 `Idle`。
2. 检查两个索引没有越界，也不是同一个索引。
3. 保存两个真实控件当前的 `geometry()`。
4. 分别调用 `grab()`，通过 `createSnapshotOverlay()` 创建两个快照 `QLabel`。
5. 调用 `setInteractionState(SwappingWidgets)`，全局关闭拖拽和添加。
6. 隐藏两个真实 `VideoWidget`。
7. 使用 `std::swap(videoWidgets_[firstIndex], videoWidgets_[secondIndex])` 交换逻辑顺序中的真实对象指针。
8. 调用 `relayoutVideoWidgets()`，按新顺序更新真实 `QGridLayout`。
9. 创建 `QParallelAnimationGroup`。
10. 第一张快照从第一个旧位置移动到第二个旧位置。
11. 第二张快照从第二个旧位置移动到第一个旧位置。
12. 两个动画均使用 220ms 和 `QEasingCurve::OutCubic`。

为什么不直接动画真实 `VideoWidget` 的 `geometry`？因为真实控件仍由 `QGridLayout` 管理，布局会在事件循环中反复重设几何位置，直接动画会出现跳动或被布局覆盖。快照是普通覆盖层，不参与网格布局，所以能稳定移动。

### 5.10 第八步：动画结束并恢复交互

`QParallelAnimationGroup::finished` 连接到 `swapVideoWidgets()` 内部的 Lambda：

1. 检查完成的动画仍是当前 `interactionAnimation_`。
2. 对两张快照调用 `deleteLater()`。
3. 重新显示两个真实 `VideoWidget`。
4. 激活 `gridLayout_`。
5. 清空 `interactionAnimation_`。
6. 调用 `setInteractionState(Idle)`，恢复所有视频格的拖拽和双击功能。
7. 发出 `videoWidgetsSwapped(firstIndex, secondIndex)`，供测试或未来配置持久化模块监听。

最终交换的是整个 `VideoWidget` 对象，因此它内部的 `titleLabel_`、`videoSurface_`、`statusLabel_` 以及未来播放器绑定都会一起移动。

### 5.11 拖拽完整调用链

```text
用户按住并移动 VideoWidget
  -> VideoWidget::mousePressEvent()
  -> VideoWidget::mouseMoveEvent()
  -> VideoWidget::startDrag()
  -> 目标 VideoWidget::dragEnterEvent()/dragMoveEvent()
  -> 目标 VideoWidget::dropEvent()
  -> emit VideoWidget::swapRequested(source, target)
  -> VideoGridWidget::handleSwapRequested()
  -> VideoGridWidget::indexOf()
  -> VideoGridWidget::swapVideoWidgets()
  -> std::swap(videoWidgets_[...])
  -> VideoGridWidget::relayoutVideoWidgets()
  -> 双快照 QParallelAnimationGroup
  -> finished Lambda
  -> setInteractionState(Idle)
  -> emit VideoGridWidget::videoWidgetsSwapped()
```

## 6. 功能二：单路视频全屏

### 6.1 功能入口在哪里

进入全屏的源头是用户在普通网格中双击某个 `VideoWidget`：

```text
VideoWidget::mouseDoubleClickEvent()
    -> VideoWidget::fullscreenRequested(this)
```

退出全屏有四个入口，但最终都调用同一个函数 `FullscreenVideoWindow::exitFullscreen()`：

| 用户操作 | 最先处理的函数 | 最终调用 |
| --- | --- | --- |
| 双击全屏画面 | `FullscreenVideoWindow::mouseDoubleClickEvent()` | `exitFullscreen()` |
| 按 `Esc` | `FullscreenVideoWindow::keyPressEvent()` | `exitFullscreen()` |
| 点击“退出全屏” | `FullscreenControlBar::exitRequested()` | `exitFullscreen()` |
| 关闭全屏窗口 | `FullscreenVideoWindow::closeEvent()` | `exitFullscreen()` |

所有退出入口收敛到一个函数，可以防止不同路径产生不同的恢复顺序。

### 6.2 第一步：VideoWidget 发出全屏请求

执行 `VideoWidget::mouseDoubleClickEvent()`：

1. Debug 构建下调用 `qDebug()` 输出事件时间、对象、事件类型和交互状态。
2. 检查 `dragEnabled_`。添加动画、交换动画和全屏期间该值均为 `false`。
3. 检查是否为左键双击。
4. 清除单击和拖拽视觉状态。
5. 发出 `fullscreenRequested(this)`。
6. 调用 `event->accept()` 并直接返回，避免同一次双击继续走基类处理路径。

### 6.3 信号如何从 VideoWidget 到达 VideoGridWidget

连接仍然建立在 `VideoGridWidget::connectVideoWidgetSignals()`：

```cpp
connect(videoWidget, &VideoWidget::fullscreenRequested,
        this, &VideoGridWidget::handleFullscreenRequested,
        Qt::UniqueConnection);
```

执行 `VideoGridWidget::handleFullscreenRequested()`：

1. 检查当前网格状态必须为 `Idle`。
2. 使用 `indexOf(videoWidget)` 确认请求者确实属于当前网格。
3. 保存 `fullscreenVideoWidget_ = videoWidget`。
4. 调用 `setInteractionState(EnteringFullscreen)`，立即禁止添加和拖拽。
5. 转发 `VideoGridWidget::fullscreenRequested(videoWidget)`。

### 6.4 信号如何从 VideoGridWidget 到达 MainWindow

连接建立在 `MainWindow::MainWindow()`：

```cpp
connect(videoGrid_, &VideoGridWidget::fullscreenRequested,
        this, &MainWindow::handleFullscreenRequest);
```

执行 `MainWindow::handleFullscreenRequest()`：

1. 再次确认网格处于 `EnteringFullscreen`。
2. 确认 `FullscreenVideoWindow` 尚未承载其他视频。
3. 保存主窗口进入全屏前是否可见到 `wasVisibleBeforeFullscreen_`。
4. 调用 `fullscreenVideoWindow_->enterFullscreen(videoWidget)`。
5. 将返回结果交给 `videoGrid_->notifyFullscreenEntryResult(videoWidget, entered)`。
6. 进入成功后隐藏 `MainWindow`，使普通网格和其他控件暂时不可见。

如果进入失败，`notifyFullscreenEntryResult(..., false)` 会清空全屏对象指针并把网格恢复为 `Idle`。

### 6.5 第三步：保存恢复现场

执行 `FullscreenVideoWindow::enterFullscreen()`：

1. 检查窗口状态必须为 `TransitionState::Windowed`。
2. 检查源 `VideoWidget` 有效且可见。
3. 将窗口状态改为 `Entering`。
4. 取得源 `VideoWidget` 的 `QVBoxLayout`。
5. 通过受控私有接口 `videoSurfaceForFullscreen()` 取得真实 `videoSurface_`。
6. 查找 `videoSurface_` 在源布局中的索引。
7. 通过 `screenForVideoWidget()` 计算该视频格所在显示器。
8. 将恢复所需信息保存到 `VideoSurfaceRestoreState`。

保存内容包括：

| 字段 | 恢复时的用途 |
| --- | --- |
| `videoWidget` | 确认原视频格对象仍然存在 |
| `originalParent` | 把 `videoSurface_` 的父对象恢复为原 `VideoWidget` |
| `originalLayout` | 把视频区域重新插入原内部布局 |
| `layoutIndex` | 恢复到标题下方的原位置 |
| `layoutStretch` | 恢复原布局伸缩比例 |
| `sizePolicy` | 恢复原尺寸策略 |
| `surfaceWasVisible` | 恢复进入前的可见状态 |
| `statusLabelWasVisible` | 恢复状态标签是否显示 |

`QPointer` 用于保存可能被 Qt 销毁的对象。对象销毁后 `QPointer` 会自动变为 `nullptr`，比长期保存裸指针更安全。

### 6.6 第四步：移动唯一的视频渲染区域

进入全屏时不复制整个 `VideoWidget`，也不创建第二个渲染表面，而是执行：

1. `sourceLayout->removeWidget(videoSurface)`。
2. 调用 `videoWidget->setFullscreenSurfaceMode(true)`，隐藏位于视频区域内的状态标签。
3. `videoSurface->setParent(this)`，临时把父对象改为全屏窗口。
4. 设置 `QSizePolicy::Expanding`。
5. `videoLayout_->addWidget(videoSurface)`。
6. 将指针保存为 `activeVideoSurface_`。

外层 `VideoWidget` 仍然留在 `VideoGridWidget` 的逻辑列表和网格槽位中，因此动态布局的顺序没有改变。

### 6.7 第五步：准备完成后才显示全屏窗口

`FullscreenVideoWindow` 构造时已经完成以下基础准备：

1. 设置独立顶层窗口和无边框标志。
2. 调用 `setAutoFillBackground(true)`。
3. 设置 `Qt::WA_OpaquePaintEvent`。
4. 使用 `QPalette` 将窗口背景设为纯黑。
5. 创建边距和间距均为 0 的 `videoLayout_`。
6. `paintEvent()` 还会显式用黑色覆盖整个窗口。

`enterFullscreen()` 在调用 `showFullScreen()` 之前继续完成：

1. 更新控制栏的设备名称和状态信息。
2. 选择目标显示器并设置窗口几何。
3. 显示 `videoSurface_`。
4. 激活视频布局。
5. 显示并定位控制栏。
6. 最后调用 `showFullScreen()`。

这样不会先显示一个空白全屏窗口，再把视频控件放进去。

全屏显示完成后：

1. 状态变为 `TransitionState::Fullscreen`。
2. 窗口置顶、激活并取得键盘焦点。
3. 调用 `showControlBar(true)` 启动自动隐藏计时。
4. 发出 `fullscreenEntered(videoWidget)`。
5. `enterFullscreen()` 返回 `true`。
6. `MainWindow` 调用 `VideoGridWidget::notifyFullscreenEntryResult(..., true)`。
7. 网格状态从 `EnteringFullscreen` 变为 `Fullscreen`。

### 6.8 悬浮控制栏的信号连接

`FullscreenControlBar` 构造函数建立按钮连接：

```text
退出按钮 clicked       -> FullscreenControlBar::exitRequested
静音按钮 clicked       -> FullscreenControlBar::muteRequested
截图按钮 clicked       -> FullscreenControlBar::screenshotRequested
鼠标进入 enterEvent    -> FullscreenControlBar::pointerEntered
鼠标离开 leaveEvent    -> FullscreenControlBar::pointerLeft
```

`FullscreenVideoWindow` 构造函数继续连接：

```text
exitRequested          -> FullscreenVideoWindow::exitFullscreen
pointerEntered         -> 停止隐藏计时器并显示光标
pointerLeft            -> scheduleControlBarHide
muteRequested          -> 转发带 VideoWidget* 的 muteRequested
screenshotRequested    -> 转发带 VideoWidget* 的 screenshotRequested
QTimer::timeout        -> hideControlBar
```

静音和截图当前只有接口，没有播放器业务实现。

### 6.9 控制栏为什么是 Overlay

`controlBar_` 的父对象是 `FullscreenVideoWindow`，但它没有加入 `videoLayout_`。它的位置由 `positionControlBar()` 手动计算：

```text
x = (全屏窗口宽度 - 控制栏宽度) / 2
y = 全屏窗口高度 - 控制栏高度 - 20
```

`resizeEvent()` 每次都重新调用 `positionControlBar()`。`showControlBar()` 还会调用 `raise()`，保证控制栏位于视频区域上方，但不占用视频布局空间。

自动隐藏流程为：

1. 鼠标在窗口中移动时进入 `mouseMoveEvent()`。
2. 如果控制栏已经显示，或者鼠标进入底部 120px 区域，则调用 `showControlBar(true)`。
3. 控制栏显示后启动 2500ms 单次 `QTimer`。
4. 鼠标进入控制栏时发出 `pointerEntered()`，停止计时。
5. 鼠标离开控制栏时发出 `pointerLeft()`，重新计时。
6. 定时器超时后执行 `hideControlBar()`。
7. 控制栏隐藏后调用 `setCursor(Qt::BlankCursor)` 隐藏光标。

### 6.10 第六步：退出全屏并恢复现场

无论退出源头是双击、`Esc`、按钮还是关闭事件，最终都执行 `FullscreenVideoWindow::exitFullscreen()`：

1. 检查窗口必须处于 `Fullscreen`，并且确实存在 `activeVideoSurface_`。
2. 将状态改为 `Exiting`，重复退出请求会被拒绝。
3. 复制当前 `VideoSurfaceRestoreState`，防止恢复过程中成员被清空。
4. 发出 `fullscreenExitStarted(videoWidget)`。
5. `MainWindow` 构造时已把这个信号连接到 `VideoGridWidget::notifyFullscreenExitStarted()`。
6. 网格状态从 `Fullscreen` 变为 `ExitingFullscreen`，继续禁止添加和拖拽。
7. 停止自动隐藏计时器，隐藏控制栏并恢复光标状态。
8. 隐藏 `videoSurface_` 并从全屏布局移除。
9. 使用 `setParent(originalParent)` 恢复原父对象。
10. 使用 `insertWidget(layoutIndex, ...)` 恢复原内部布局位置和 stretch。
11. 恢复原 `QSizePolicy`。
12. 调用 `setFullscreenSurfaceMode(false, ...)` 恢复状态标签。
13. 恢复 `videoSurface_` 进入前的可见状态。
14. 激活原布局并调用 `videoWidget->updateGeometry()`。
15. 清空 `activeVideoSurface_` 和恢复信息。
16. 在黑色全屏窗口仍覆盖屏幕时先发出 `fullscreenExited(videoWidget)`。

`MainWindow` 对 `fullscreenExited` 的连接会按顺序执行：

1. 调用 `videoGrid_->notifyFullscreenExited(videoWidget)`。
2. 网格清空 `fullscreenVideoWidget_` 并恢复 `Idle`。
3. 调用 `restoreAfterFullscreen()`。
4. 重新显示、置顶并激活 `MainWindow`。

最后 `FullscreenVideoWindow::exitFullscreen()` 才调用 `hide()` 隐藏黑色顶层窗口，并把自己的状态恢复为 `Windowed`。先恢复主窗口、后隐藏全屏窗口，是避免退出中间帧闪烁的关键顺序。

### 6.11 全屏完整调用链

```text
用户双击普通 VideoWidget
  -> VideoWidget::mouseDoubleClickEvent()
  -> emit VideoWidget::fullscreenRequested(this)
  -> VideoGridWidget::handleFullscreenRequested()
  -> setInteractionState(EnteringFullscreen)
  -> emit VideoGridWidget::fullscreenRequested(videoWidget)
  -> MainWindow::handleFullscreenRequest()
  -> FullscreenVideoWindow::enterFullscreen()
  -> 保存 VideoSurfaceRestoreState
  -> 将唯一 videoSurface_ 移入全屏布局
  -> showFullScreen()
  -> VideoGridWidget::notifyFullscreenEntryResult(..., true)
  -> setInteractionState(Fullscreen)

用户发起退出
  -> FullscreenVideoWindow::exitFullscreen()
  -> emit fullscreenExitStarted(videoWidget)
  -> VideoGridWidget::notifyFullscreenExitStarted()
  -> setInteractionState(ExitingFullscreen)
  -> videoSurface_ 恢复原 parent、布局索引和尺寸策略
  -> emit fullscreenExited(videoWidget)
  -> VideoGridWidget::notifyFullscreenExited()
  -> setInteractionState(Idle)
  -> MainWindow::restoreAfterFullscreen()
  -> 隐藏 FullscreenVideoWindow
```

## 7. 功能三：动态添加视频窗口

### 7.1 功能入口在哪里

动态添加从 `MainWindow` 顶部工具栏的 `QAction` 开始。

连接建立在 `MainWindow::MainWindow()`：

```cpp
connect(addVideoAction_, &QAction::triggered,
        this, &MainWindow::addVideoWidget);
```

因此入口调用链是：

```text
用户点击“添加视频窗口”
  -> QAction::triggered
  -> MainWindow::addVideoWidget()
  -> VideoGridWidget::addVideoWidget()
```

`MainWindow` 只转发操作，不创建 `VideoWidget`，也不维护第二份视频格列表。

### 7.2 第一步：检查是否允许添加

`VideoGridWidget::addVideoWidget()` 首先调用 `canAddVideoWidget()`。只有同时满足以下条件才允许继续：

1. `interactionState_ == GridInteractionState::Idle`。
2. `videoWidgets_.size() < kMaximumVideoWidgetCount`。
3. 最大数量常量 `kMaximumVideoWidgetCount` 为 16。

如果已经达到 16 路：

1. 发出 `maximumVideoWidgetCountReached()`。
2. 返回 `nullptr`。
3. 不会创建第 17 个控件。

如果是动画或全屏期间，则直接返回 `nullptr`，避免布局操作重入。

### 7.3 第二步：进入添加状态并保存旧画面

允许添加后执行：

1. 调用 `setInteractionState(AddingWidget)`。
2. 所有现有 `VideoWidget` 的 `dragEnabled_` 被关闭。
3. `MainWindow` 收到 `gridInteractionStateChanged()`，禁用添加按钮。
4. 遍历当前 `videoWidgets_`。
5. 保存每个旧控件的指针、旧 `geometry()` 和 `grab()` 快照。
6. 如果网格不可见或任一几何为空，将 `canAnimate` 设为 `false`，后续走安全的无动画路径。

先保存旧位置非常重要。布局重排之后，真实控件的 `geometry()` 已经是新位置，旧位置无法再从布局中获得。

### 7.4 第三步：真实创建一个新控件

`addVideoWidget()` 调用 `createVideoWidget()`：

1. 执行 `new VideoWidget(this)`，真正创建一个控件。
2. 从 `nextCameraNumber_` 取得递增编号。
3. 设置唯一对象名，例如 `videoWidget02`。
4. 设置显示名称，例如 `Camera 02`。
5. 设置状态为“未连接”。
6. 根据当前状态设置拖拽是否启用。此时处于 `AddingWidget`，所以新控件暂时不可拖拽。
7. 调用 `connectVideoWidgetSignals(videoWidget)`。

新控件会获得与旧控件完全相同的两条连接：

```text
VideoWidget::swapRequested
  -> VideoGridWidget::handleSwapRequested

VideoWidget::fullscreenRequested
  -> VideoGridWidget::handleFullscreenRequested
```

因此动态创建的视频格天然支持拖拽和全屏，不需要 `MainWindow` 再单独连接一次。

### 7.5 第四步：加入逻辑列表并通知界面

回到 `addVideoWidget()`：

1. 执行 `videoWidgets_.append(newVideoWidget)`。
2. 发出 `videoWidgetCountChanged(videoWidgets_.size())`。
3. `MainWindow` 收到数量变化后调用 `updateAddVideoAction()`。
4. 如果数量刚好达到 16，再发出 `maximumVideoWidgetCountReached()`。
5. `MainWindow` 在状态栏显示“已达到最多 16 个视频窗口”，并禁用添加按钮。

`videoWidgets_` 的索引既是逻辑顺序，也是布局使用的视觉顺序。后续设备管理和播放器绑定也应依赖这个稳定对象，而不是复制一份控件列表。

### 7.6 第五步：计算动态网格尺寸

`relayoutVideoWidgets()` 内部调用 `gridDimensions()`，后者调用静态函数 `calculateGridDimensions(widgetCount)`。

算法步骤为：

1. 输入小于 1 或大于 16 时返回 `{0, 0}`。
2. `columns` 从 1 开始。
3. 当 `columns * columns < widgetCount` 时增加列数。
4. 列数最多为 4。
5. 使用向上取整公式计算行数：`(widgetCount + columns - 1) / columns`。

实际结果为：

| 视频数量 | 行 x 列 |
| ---: | :---: |
| 1 | 1 x 1 |
| 2 | 1 x 2 |
| 3～4 | 2 x 2 |
| 5～6 | 2 x 3 |
| 7～9 | 3 x 3 |
| 10～12 | 3 x 4 |
| 13～16 | 4 x 4 |

### 7.7 第六步：按逻辑顺序重新布局

执行 `VideoGridWidget::relayoutVideoWidgets()`：

1. 从 `QGridLayout` 移除所有现有 `VideoWidget`，但不销毁对象。
2. 清空最多四行、四列的旧 stretch。
3. 根据当前数量计算 `GridDimensions`。
4. 按 `videoWidgets_` 的索引重新加入布局。
5. 行号使用 `index / dimensions.columns`。
6. 列号使用 `index % dimensions.columns`。
7. 当前有效的每一行和每一列都设置 stretch 为 1。
8. 调用 `invalidate()` 和 `activate()`，让布局立即计算新几何。

这一步只创建本次新增的一个控件，旧控件全部复用，不会为了重排而批量销毁和重建。

### 7.8 第七步：创建添加动画快照

真实布局已经完成后，`addVideoWidget()` 获取新控件的目标几何，并验证动画条件。

如果可以动画：

1. 为每个旧控件创建覆盖层快照。
2. 旧快照的起点是添加前的旧几何。
3. 旧快照的终点是重新布局后的新几何。
4. 对新控件调用 `grab()` 创建新快照。
5. 使用 `centeredScaledRect()` 计算目标矩形中心 85% 大小的起始矩形。
6. 新快照从 85% 大小放大到 100%。
7. 新快照同时通过 `QGraphicsOpacityEffect` 从透明度 0 变为 1。
8. 隐藏所有真实 `VideoWidget`。
9. 创建 `QParallelAnimationGroup`，同时播放所有几何动画和新控件淡入动画。
10. 动画时长为 220ms，缓动曲线为 `OutCubic`。

透明效果只附加在临时快照上，不附加到真实 `videoSurface_`。未来把视频区域替换为 `QOpenGLWidget` 或原生窗口时，这种做法更安全。

### 7.9 第八步：动画完成并恢复按钮、拖拽和全屏

`QParallelAnimationGroup::finished` 的 Lambda 执行：

1. 检查当前动画对象仍然有效。
2. 删除所有临时快照。
3. 重新显示全部真实 `VideoWidget`。
4. 激活网格布局。
5. 清空 `interactionAnimation_`。
6. 调用 `setInteractionState(Idle)`。
7. 所有视频格恢复拖拽和双击全屏能力。
8. `MainWindow` 收到状态变化，重新计算添加按钮状态。
9. 发出 `videoWidgetAdded(newVideoWidget)`，表示添加流程完整结束。

如果动画条件不成立，代码会直接显示真实控件、恢复 `Idle` 并发出 `videoWidgetAdded()`。无动画路径仍然保证状态完整收尾。

### 7.10 添加按钮如何保持正确状态

`MainWindow::updateAddVideoAction()` 统一更新按钮：

1. 读取 `videoGrid_->videoWidgetCount()`。
2. 使用 `videoGrid_->canAddVideoWidget()` 决定是否启用。
3. 达到 16 路时，工具提示显示已达到上限。
4. 非 `Idle` 状态时，工具提示说明动画或全屏期间暂不可添加。
5. 空闲且未满时，显示当前数量，例如“当前 5/16”。

它由两个信号触发：

```text
VideoGridWidget::videoWidgetCountChanged
  -> MainWindow::updateAddVideoAction

VideoGridWidget::gridInteractionStateChanged
  -> MainWindow::updateAddVideoAction
```

因此按钮既跟随数量，也跟随动画和全屏状态。

### 7.11 动态添加完整调用链

```text
用户点击工具栏“添加视频窗口”
  -> QAction::triggered
  -> MainWindow::addVideoWidget()
  -> VideoGridWidget::addVideoWidget()
  -> canAddVideoWidget()
  -> setInteractionState(AddingWidget)
  -> 保存旧控件几何和快照
  -> createVideoWidget()
       -> new VideoWidget(this)
       -> 设置 Camera 编号
       -> connectVideoWidgetSignals()
  -> videoWidgets_.append(newVideoWidget)
  -> emit videoWidgetCountChanged()
  -> relayoutVideoWidgets()
       -> calculateGridDimensions()
       -> index / columns、index % columns
  -> 创建旧控件移动快照和新控件缩放淡入快照
  -> QParallelAnimationGroup
  -> finished Lambda
  -> 显示真实控件并删除快照
  -> setInteractionState(Idle)
  -> emit videoWidgetAdded(newVideoWidget)
```

## 8. 三个功能如何协同

### 8.1 添加期间为什么不能拖拽或全屏

`addVideoWidget()` 一开始就把状态设为 `AddingWidget`。`setInteractionState()` 会关闭全部视频格的 `dragEnabled_`：

- `mousePressEvent()` 不会开始拖拽。
- `mouseDoubleClickEvent()` 不会发出全屏请求。
- `canAddVideoWidget()` 返回 `false`，添加按钮被禁用。

### 8.2 交换期间为什么不能继续添加

`swapVideoWidgets()` 在隐藏真实控件前把状态设为 `SwappingWidgets`：

- `canAddVideoWidget()` 返回 `false`。
- `MainWindow` 禁用添加按钮。
- 其他视频格也不能再次开始拖拽。

### 8.3 全屏期间为什么布局不会变化

全屏请求被接受后，网格依次经过：

```text
Idle -> EnteringFullscreen -> Fullscreen -> ExitingFullscreen -> Idle
```

只有最后恢复到 `Idle` 后，添加和交换才重新开放。因此 `videoWidgets_` 顺序在全屏期间不会变化，退出时只需要恢复 `videoSurface_` 的内部父子关系，不需要猜测固定的网格行列。

### 8.4 为什么动态创建的窗口自动支持另外两个功能

所有视频格都只能通过 `VideoGridWidget::createVideoWidget()` 创建，而该函数总会调用 `connectVideoWidgetSignals()`。因此无论是启动时的 `Camera 01`，还是后来添加的 `Camera 16`，都会建立完全相同的拖拽和全屏连接。

## 9. 信号与槽总表

| 信号 | 连接位置 | 接收函数或 Lambda | 作用 |
| --- | --- | --- | --- |
| `QAction::triggered` | `MainWindow::MainWindow()` | `MainWindow::addVideoWidget()` | 添加功能入口 |
| `VideoWidget::swapRequested` | `VideoGridWidget::connectVideoWidgetSignals()` | `VideoGridWidget::handleSwapRequested()` | 将拖放请求交给网格 |
| `VideoWidget::fullscreenRequested` | `VideoGridWidget::connectVideoWidgetSignals()` | `VideoGridWidget::handleFullscreenRequested()` | 将双击请求交给网格状态机 |
| `VideoGridWidget::fullscreenRequested` | `MainWindow::MainWindow()` | `MainWindow::handleFullscreenRequest()` | 进入独立全屏窗口 |
| `FullscreenControlBar::exitRequested` | `FullscreenVideoWindow` 构造函数 | `FullscreenVideoWindow::exitFullscreen()` | 点击按钮退出全屏 |
| `FullscreenVideoWindow::fullscreenExitStarted` | `MainWindow::MainWindow()` | `VideoGridWidget::notifyFullscreenExitStarted()` | 在恢复 parent 前锁定网格 |
| `FullscreenVideoWindow::fullscreenExited` | `MainWindow::MainWindow()` | Lambda | 恢复网格状态和主窗口 |
| `VideoGridWidget::videoWidgetCountChanged` | `MainWindow::MainWindow()` | Lambda | 更新添加按钮 |
| `VideoGridWidget::gridInteractionStateChanged` | `MainWindow::MainWindow()` | Lambda | 动画或全屏期间禁用添加按钮 |
| `VideoGridWidget::maximumVideoWidgetCountReached` | `MainWindow::MainWindow()` | Lambda | 状态栏显示 16 路上限 |
| `QParallelAnimationGroup::finished` | `addVideoWidget()` 或 `swapVideoWidgets()` | 各自的完成 Lambda | 删除快照、显示真实控件、恢复 `Idle` |
| `QTimer::timeout` | `FullscreenVideoWindow` 构造函数 | `hideControlBar()` | 自动隐藏全屏控制栏 |

## 10. 建议的源码阅读顺序

第一次阅读时不要直接从最长的动画函数开始，建议按下面顺序：

1. 阅读 `include/common/ui/VideoWidget.h`，先认识两个请求信号。
2. 阅读 `include/common/ui/VideoGridWidget.h`，理解 `videoWidgets_` 和 `GridInteractionState`。
3. 阅读 `src/common/ui/MainWindow.cpp` 构造函数，找出跨组件连接。
4. 阅读 `VideoGridWidget::createVideoWidget()` 和 `connectVideoWidgetSignals()`，理解动态对象如何获得完整功能。
5. 阅读 `VideoWidget` 的鼠标与拖放事件，理解拖拽和双击的入口。
6. 阅读 `VideoGridWidget::handleSwapRequested()` 与 `swapVideoWidgets()`。
7. 阅读 `VideoGridWidget::handleFullscreenRequested()`，再进入 `FullscreenVideoWindow::enterFullscreen()` 和 `exitFullscreen()`。
8. 最后阅读 `VideoGridWidget::addVideoWidget()` 和 `relayoutVideoWidgets()`，把动态布局与已有功能联系起来。
9. 对照 `resources/styles/app.qss`，观察 `dragState` 和全屏控制栏的视觉选择器。
10. 阅读两个测试文件，理解项目如何验证对象身份、布局顺序、状态互斥和全屏恢复。

## 11. 调试时应该观察什么

### 11.1 拖拽问题

按顺序检查：

1. `mouseMoveEvent()` 是否超过 `QApplication::startDragDistance()`。
2. MIME 类型是否为 `application/x-rtmp-monitor-video-widget`。
3. 目标是否进入 `dragEnterEvent()` 和 `dropEvent()`。
4. `swapRequested` 是否只发出一次。
5. `handleSwapRequested()` 能否在 `videoWidgets_` 中找到两个索引。
6. 状态是否按 `Idle -> SwappingWidgets -> Idle` 变化。
7. 动画结束后快照是否删除、真实控件是否重新显示。

### 11.2 全屏问题

Debug 构建已经在 `VideoWidget::mouseDoubleClickEvent()`、`enterFullscreen()`、`exitFullscreen()` 和关闭事件附近输出 `fullscreen event` 日志。重点观察：

1. 同一次双击是否只产生一次进入或退出调用。
2. 网格状态是否按规定变化。
3. `videoSurface_` 是否始终是同一个对象地址。
4. 进入前是否已经保存 `originalParent`、`originalLayout` 和 `layoutIndex`。
5. 退出时是否先恢复主窗口，再隐藏全屏窗口。

### 11.3 动态添加问题

按顺序检查：

1. 点击前 `canAddVideoWidget()` 是否为 `true`。
2. `nextCameraNumber_` 是否生成唯一名称。
3. 新对象是否已追加到 `videoWidgets_`。
4. `calculateGridDimensions()` 的结果是否符合数量表。
5. `relayoutVideoWidgets()` 是否按逻辑索引排列。
6. 添加期间按钮和拖拽是否被禁用。
7. 动画完成后是否发出 `videoWidgetAdded()` 并恢复 `Idle`。

## 12. 自动化测试对应关系

| 测试文件 | 主要覆盖内容 |
| --- | --- |
| `tests/VideoGridDynamicTest.cpp` | 1～16 路布局算法、数量上限、名称唯一性、逻辑位置、添加和交换重入保护、新控件拖拽与全屏连接、工具栏按钮状态 |
| `tests/VideoGridSmokeTest.cpp` | 动态添加动画完成、真实对象交换、快照交换互斥、全屏黑色背景、零边距布局、同一 `videoSurface_` 转移、退出恢复和重复退出保护 |

执行验证：

```powershell
cmake --preset Qt-Debug
cmake --build out/build-windows-x64/debug
ctest --test-dir out/build-windows-x64/debug --output-on-failure
```

自动化测试可以验证对象、信号、状态和布局关系，但无法完全判断动画是否肉眼流畅。动画闪烁、拖动手感、控制栏视觉位置仍需运行程序进行人工检查。

## 13. 新人常见误区

### 13.1 `videoWidgets_` 只是缓存吗

不是。它是视频格逻辑顺序的唯一来源。拖拽交换先交换这个容器中的指针，布局再按该顺序显示。

### 13.2 为什么全屏不移动整个 VideoWidget

外层 `VideoWidget` 还包含标题、边框和状态信息。当前需求只让真实视频画面铺满屏幕，因此仅移动 `videoSurface_`，并让外层对象继续留在网格槽位中。

### 13.3 为什么快照动画后还要显示真实控件

快照只是动画期间的一张静态图片，不具备后续视频渲染、拖拽或双击能力。动画结束必须删除快照并显示真实对象。

### 13.4 为什么不能在全屏时添加窗口

全屏退出需要恢复 `videoSurface_` 的父子关系和内部布局。此时同时重排外部网格会扩大生命周期和布局冲突风险，所以状态机明确禁止并发执行。

### 13.5 将来接入 FFmpeg 后调用链会全部重写吗

不会。FFmpeg 播放器应绑定到具体 `VideoWidget` 或其渲染表面。拖拽移动整个 `VideoWidget`，全屏移动同一个 `videoSurface_`，因此播放器和设备身份可以继续保持不变。需要新增的是帧解码、线程传递和渲染逻辑，而不是重写这三条 UI 调用链。

## 14. 总结

三个功能虽然入口不同，但遵循相同的工程原则：

1. `VideoWidget` 只识别鼠标操作并发出请求。
2. `VideoGridWidget` 维护唯一逻辑顺序，并用状态机决定请求能否执行。
3. `MainWindow` 只负责顶层组件协调和工具栏反馈。
4. 布局动画移动快照，真实控件只做最终布局和显示。
5. 全屏只转移唯一的 `videoSurface_`，退出时完整恢复父子关系。
6. 所有异步动画或全屏流程结束后都必须回到 `Idle`，下一次交互才能安全开始。

沿着“事件入口、请求信号、容器状态、实际操作、完成信号”这五个节点阅读代码，就能快速定位三个功能中的大多数问题。
