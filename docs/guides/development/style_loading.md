# QSS 样式加载与主题扩展说明

> 文档分类：开发指南。

## 1. 目标

项目的 QSS 不再写在 `VideoWidget` 等 C++ 构造函数中，而是集中存放在 `resources/styles/`。应用启动时由 `StyleLoader` 统一读取并应用样式，从而将界面结构与视觉样式分离。

当前方案同时支持部署后调整样式和可靠回退：

```text
exe 同级 styles/app.qss
  -> 优先加载，修改后重启应用即可生效
  -> 缺失或不可读时回退
:/styles/app.qss
  -> 编译进 QRC 的默认主题
```

本版本不监听文件变化，也不支持运行中热重载。动态加载仅指应用启动时从外部文件读取 QSS。

## 2. 目录结构

```text
include/common/
├── app/
│   └── StyleLoader.h              # 应用级 QSS 服务
└── core/
    └── Singleton.h                # 通用 CRTP 单例模板

resources/
├── styles/
│   └── app.qss                    # 默认应用样式源文件
└── styles.qrc                     # 编译内置资源的映射

src/common/app/
└── StyleLoader.cpp
```

`app.qss` 的 QRC 路径固定为：

```text
:/styles/app.qss
```

## 3. StyleLoader 与 Singleton

`StyleLoader` 继承 `Singleton<StyleLoader>`，因此整个进程只有一个样式服务实例：

```cpp
StyleLoader::instance().applyApplicationStyle(app);
```

单例模板使用 C++17 函数内静态对象，首次创建实例时由语言保证线程安全。`StyleLoader` 不继承 `QObject`、不保存 `QWidget` 指针，也不管理窗口生命周期；它只读取 QSS 并调用 `QApplication::setStyleSheet()`。

单例不应被当作通用对象创建方式。多路播放器、设备管理器和视频控件都需要独立实例，不能使用该模板。只有进程级、无 UI 所有权且生命周期明确的服务才需要单独评估是否适合使用单例。

## 4. 加载流程

`StyleLoader::applyApplicationStyle()` 接收 `StyleLoadOptions`，默认样式文件名为 `app.qss`。

```text
开始
  -> 校验文件名仅为单个 .qss 文件名
  -> 确定外部样式目录
       默认：<应用程序目录>/styles
       测试：可显式传入临时目录
  -> 外部 app.qss 存在且可读？
       是：应用外部 QSS，返回 ExternalFile
       否：记录警告并读取 :/styles/app.qss
  -> QRC 可读？
       是：应用内置 QSS，返回 QtResource
       否：保留当前 QApplication 样式，返回失败结果
```

样式文件名不允许包含路径分隔符，且必须以 `.qss` 结尾。该限制避免未来从配置文件选择主题时出现路径穿越。

## 5. QSS 选择器契约

`VideoWidget` 使用动态属性和对象名作为 QSS 的稳定选择器：

| 控件 | 选择器 | 用途 |
| --- | --- | --- |
| 视频格根控件 | `QFrame[styleRole="videoWidget"]` | 背景和边框。 |
| 设备名称 | `QLabel#deviceNameLabel` | 标题颜色和字重。 |
| 视频占位区 | `QFrame[styleRole="videoSurface"]` | 黑色视频背景，换 parent 后仍然生效。 |
| 状态文本 | `QLabel#statusLabel` | 状态文字颜色。 |
| 主工具栏 | `QToolBar#videoToolBar` | 添加动作及其禁用反馈。 |

修改这些 `objectName` 或 `styleRole` 等同于修改 UI 样式 API，必须同步更新 QSS、测试和相关文档。新组件应使用自己的 `styleRole`，不要在 `app.qss` 中使用无范围的 `QFrame`、`QLabel` 全局选择器。

## 6. 构建、部署与安装

CMake 会执行三项操作：

1. 将 `resources/styles.qrc` 分别编译进应用和测试可执行目标。
2. 每次构建 `rtmp_monitor` 后，把源文件复制到：

   ```text
   <可执行文件目录>/styles/app.qss
   ```

3. 执行安装时，把 `app.qss` 安装到：

   ```text
   <安装目录>/bin/styles/app.qss
   ```

部署时建议保留外部 `styles/app.qss`，便于在不重新编译程序的情况下微调颜色、间距和字体。删除该文件不会导致程序无法启动，应用会自动使用 QRC 默认主题。

## 7. 后续扩展方式

当样式规模扩大时，可继续在 `resources/styles/` 中新增文件，例如：

```text
main_window.qss
video_widget.qss
status_panel.qss
```

扩展时应在 `StyleLoader` 中明确固定加载顺序，并在 QRC、构建复制规则和安装规则中同步增加文件。不要在各个控件构造函数中重新引入 `setStyleSheet()`，否则会破坏统一主题的优先级和可维护性。

未来若需要用户切换深色、浅色主题，可在配置模块中保存主题文件名，并显式再次调用 `StyleLoader`。文件监听和热重载属于后续功能，需单独设计线程与 UI 刷新策略。

## 8. 验证结果

本次在 MSVC 19.41、Qt 6.6.1 `msvc2019_64` 环境下完成配置与 Debug 构建，并通过 CTest：

```text
1/2 Test #1: rtmp_monitor_ui_smoke_test ... Passed
2/2 Test #2: rtmp_monitor_dynamic_grid_test ... Passed
100% tests passed, 0 tests failed
```

自动化测试覆盖：

- `StyleLoader` 不可复制、不可移动，且 `instance()` 返回同一对象。
- 外部样式缺失时从 QRC 回退。
- 外部样式存在时优先加载。
- 外部样式不可读时回退 QRC。
- 1～16 路动态视频网格、设备名称、工具栏禁用状态和 `styleRole` 保持正确。
- 拖拽状态、全屏控制栏和黑色视频区域的样式作用域保持隔离。
