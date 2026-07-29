# RtmpMonitor 项目代码规范

> 文档分类：开发规范。

## 1. 目的与适用范围

本文定义 RtmpMonitor 项目的 C++17、Qt 6、FFmpeg、CMake、QSS 和测试代码规范，目标是让多人开发及 AI 生成代码保持一致，并降低后续接入多路解码、跨线程传帧和硬件渲染时的维护成本。同一套程序必须能够面向 Windows x86_64 PC 和 Linux ARM64 嵌入式硬件盒子构建，平台差异不得破坏监控、动态网格、拖拽、全屏和视频播放等公共行为。

适用目录：

```text
include/common/ 两个平台共享的公共和模块内部头文件
src/common/     两个平台共享的 C++ 实现
src/platform/   Windows 与 Linux ARM64 平台实现
cmake/          公共构建模块与交叉编译工具链
tests/          自动化测试
resources/      QSS、QRC 等资源
scripts/        开发和验证脚本
```

注释的详细要求以 [《Comment Style Guide》](comment_style_guide.md) 为准；本文重点约束命名、格式、接口、所有权、线程、Qt/FFmpeg 使用和测试方式。两份规范冲突时，针对注释内容优先遵循注释规范。

## 2. 当前工程风格基线

项目统一遵循以下基础约定：

- C++17，关闭编译器语言扩展。
- Qt 6 Widgets 与 FFmpeg，源文件统一使用 UTF-8。
- Windows x86_64 使用 MSVC；Linux ARM64 使用 GCC 或 Clang 交叉编译或原生编译。
- 类、结构体和枚举使用 PascalCase。
- 函数、局部变量和参数使用 lowerCamelCase。
- 私有成员变量使用 `_` 后缀。
- 编译期常量使用 `k` 前缀。
- Qt 控件由父对象管理，异步生命周期使用 `QPointer` 保护。
- 公共类和公共函数使用中文 Doxygen。
- CMake 使用 target-based 写法，不使用全局 include/link 配置污染其他目标。
- 测试分为 UI 冒烟测试和 Qt Test 数据驱动测试。

目标平台矩阵如下：

| 目标平台 | 主要场景 | 编译器 | 图形环境 |
|---|---|---|---|
| Windows x86_64 | PC 多路视频监控 | MSVC | Windows Qt 平台插件、桌面 OpenGL |
| Linux ARM64 | 嵌入式硬件盒子多路视频监控 | GCC 或 Clang | 由部署环境选择 Wayland、X11 或 EGLFS，可使用 OpenGL ES |

当前根 `CMakeLists.txt` 已区分 Windows x86_64/MSVC 与 Linux ARM64/GCC 或 Clang，并集中管理两类编译器选项。通用 Ubuntu 22.04 ARM64 sysroot 与 Qt 6 已完成完整项目编译和链接验证；FFmpeg 目标库及真实硬件运行环境尚未接入，不得把交叉编译成功描述为 ARM64 GUI、渲染或视频播放已运行通过。

项目当前没有 `.clang-format`。在引入自动格式化配置前，本规范由代码审查和 `git diff --check` 共同约束，不得使用与现有代码明显不同的批量格式化结果修改无关文件。

## 3. 文件与目录

### 3.1 文件命名

| 内容 | 规则 | 示例 |
|---|---|---|
| C++ 类头文件 | 与主类同名，PascalCase | `VideoGridWidget.h` |
| C++ 类实现 | 与主类同名，PascalCase | `VideoGridWidget.cpp` |
| 测试文件 | 被测模块名加测试类型 | `VideoGridDynamicTest.cpp` |
| QSS 文件 | lower_snake_case 或稳定主题名 | `app.qss` |
| Markdown 文档 | lower_snake_case | `code_style_guide.md` |

一个头文件原则上只声明一个主要公共类。小型辅助结构可以与其唯一使用者放在同一头文件中。

### 3.2 头文件结构

头文件按以下顺序组织：

```cpp
#pragma once

#include <QtType>

class ForwardDeclaredType;

/** 公共类型 Doxygen。 */
class Example final : public QObject
{
    Q_OBJECT

public:
    // 公共 API

signals:
    // 信号

protected:
    // Qt 事件重载

private:
    // 私有实现与成员
};
```

- 使用 `#pragma once`。
- 能通过前置声明表达的指针或引用类型，不在头文件中引入完整定义。
- 基类、值成员、模板实例化或内联代码需要完整类型时才添加 include。
- 头文件不得使用 `using namespace`。
- 不依赖其他头文件的偶然间接包含。

### 3.3 实现文件 include 顺序

```cpp
#include "ui/VideoGridWidget.h"   // 自身头文件

#include <utility>                 // C++ 标准库

#include <QGridLayout>             // Qt
#include <QPointer>

#include "ui/VideoWidget.h"       // 项目内部依赖
```

每组之间空一行，组内尽量按字母顺序排列。自身头文件必须位于第一行，以便发现其缺失的直接依赖。

## 4. 命名规范

| 元素 | 规则 | 示例 |
|---|---|---|
| 类、结构体 | PascalCase | `FullscreenVideoWindow`、`GridDimensions` |
| 强类型枚举 | PascalCase | `GridInteractionState` |
| 枚举值 | PascalCase | `AddingWidget`、`Fullscreen` |
| 函数、槽函数 | lowerCamelCase | `addVideoWidget()`、`restoreAfterFullscreen()` |
| 信号 | 事件或请求语义 | `videoWidgetAdded()`、`fullscreenRequested()` |
| 布尔查询 | `is`、`has`、`can` 开头 | `isFullscreenActive()`、`canAddVideoWidget()` |
| 普通变量和参数 | lowerCamelCase | `widgetCount`、`videoWidget` |
| 私有成员 | lowerCamelCase + `_` | `gridLayout_`、`interactionState_` |
| 编译期常量 | `k` + PascalCase | `kMaximumVideoWidgetCount` |
| Qt objectName | lowerCamelCase，稳定且唯一 | `videoToolBar`、`statusLabel` |
| QSS styleRole | lowerCamelCase | `videoWidget`、`videoSurface` |

不要使用无业务含义的缩写。循环中的短索引 `i` 可以接受；设备、帧、时间戳等核心变量必须使用完整名称。

## 5. 格式规范

### 5.1 缩进与空白

- 使用 4 个空格缩进，不使用 Tab。
- 行尾不得包含空格。
- 文件末尾保留一个换行符。
- 逗号后、二元运算符两侧和控制语句关键字后保留空格。
- 软行宽建议为 100 字符，超过 120 字符必须合理换行；URL、命令和不可拆分字符串除外。
- 不为了对齐而加入大量易失效空格。

### 5.2 大括号

类、结构体和函数使用换行大括号；控制语句左大括号与条件同一行：

```cpp
class VideoWidget final : public QFrame
{
public:
    void updateState();
};

void VideoWidget::updateState()
{
    if (isVisible()) {
        update();
    }
}
```

即使分支只有一行，也必须使用大括号。空函数只有在语义明确且确实必要时才能写成单行。

### 5.3 换行

长函数调用按参数换行，右括号与最后一个参数保持现有风格：

```cpp
connect(fullscreenVideoWindow_, &FullscreenVideoWindow::fullscreenExited,
        this, [this](VideoWidget *videoWidget) {
            videoGrid_->notifyFullscreenExited(videoWidget);
            restoreAfterFullscreen();
        });
```

复杂条件优先在逻辑运算符附近换行，不把多个独立业务判断压成难以阅读的一行。

## 6. C++17 使用规范

### 6.1 类型与接口

- 构造函数避免隐式转换时使用 `explicit`。
- 不允许继承的具体 UI 类使用 `final`。
- 重载虚函数必须写 `override`，不得只重复写 `virtual`。
- 不应忽略返回值的查询或创建函数使用 `[[nodiscard]]`。
- 明确不会抛出异常的简单查询使用 `noexcept`。
- 使用 `nullptr`，禁止使用 `NULL` 或整数 `0` 表示空指针。
- 使用 `enum class`，避免无作用域枚举污染命名空间。
- 数量上限、动画时间等规则使用 `static constexpr`，不散落魔法数字。

### 6.2 const 正确性

- 不修改对象状态的成员函数标记为 `const`。
- 只读引用参数使用 `const T &`。
- 遍历只读容器时使用 `std::as_const()` 或 const 引用，避免隐式 detach。
- 不使用 `const_cast` 绕开设计问题；仅在第三方 API 无 const 接口且语义确实只读时评估使用，并说明原因。

### 6.3 所有权与资源管理

- 非 QObject 资源优先使用栈对象、`std::unique_ptr` 或专用 RAII 包装。
- QObject/QWidget 可以使用 `new`，但创建时必须立即指定可靠 parent，或在同一作用域明确转移 parent。
- 裸指针默认表示借用，不表示共享所有权；公共接口必须在 Doxygen 中说明释放责任。
- 不使用 `std::shared_ptr<QObject>` 管理已有 Qt 父子关系的对象。
- 动画、定时器和延迟回调跨越事件循环时，对可能被销毁的 QObject 使用 `QPointer`。
- lambda 连接必须提供 context QObject，确保 context 销毁时自动断开。

## 7. 跨平台与 ARM64 设计规范

### 7.1 同一程序与平台边界

- Windows x86_64 与 Linux ARM64 构建的是同一套 RtmpMonitor 程序，两端应提供一致的核心功能和用户交互。
- 业务逻辑、UI 状态、视频网格、拖拽、全屏、播放控制和错误模型默认必须跨平台，不得以平台宏改变其业务语义。
- 优先使用标准 C++、Qt 和 FFmpeg 的跨平台 API。只有这些 API 无法满足系统能力时，才允许引入平台实现。
- Win32、POSIX、DRM、EGL、DMA、V4L2 或厂商 SDK 调用必须集中在平台适配层，不得散落在 `VideoWidget`、`VideoGridWidget`、播放器状态机等业务类中。
- 平台无关头文件只能暴露稳定的抽象、值类型和所有权契约，不得向调用方泄漏 `HWND`、文件描述符、DRM handle 或厂商句柄。

平台代码增加后推荐采用以下边界；尚不存在的目录在首次引入对应能力时再创建，不提前生成空目录：

```text
include/common/         平台无关接口与共享模块
src/common/             两个平台共享的实现
src/platform/windows/   Windows x86_64 实现
src/platform/linux/     Linux ARM64 实现
cmake/toolchains/       Linux ARM64 交叉编译工具链
```

同一接口的平台实现使用可辨识的文件名，例如 `HardwareDecoderWindows.cpp` 和 `HardwareDecoderLinux.cpp`，由 CMake 为目标平台选择源文件。不要通过运行时操作系统字符串选择本应在编译期确定的实现。

### 7.2 条件编译

- Qt C++ 源码使用 `Q_OS_WIN`、`Q_OS_LINUX`、`Q_PROCESSOR_X86_64` 和 `Q_PROCESSOR_ARM_64` 判断操作系统或架构。
- 普通业务代码不得直接使用 `_WIN32`、`__linux__`、`__aarch64__` 等编译器预定义宏；确需对接不包含 Qt 的第三方 C 接口时，仅允许在平台适配层使用。
- 小于约十行且语义紧密的平台差异可以使用条件编译；更大的差异必须拆分实现文件，避免一个函数包含多套流程。
- 支持平台必须显式列出，平台适配层遇到未知系统时使用编译期错误，不能静默选择一个看似接近的实现。

```cpp
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
// Windows x86_64 平台适配。
#elif defined(Q_OS_LINUX) && defined(Q_PROCESSOR_ARM_64)
// Linux ARM64 平台适配。
#else
#error "Unsupported RtmpMonitor target platform"
#endif
```

CMake 必须根据目标端的 `CMAKE_SYSTEM_NAME`、`CMAKE_SYSTEM_PROCESSOR` 和 `CMAKE_<LANG>_COMPILER_ID` 选择实现和选项。交叉编译时禁止使用 `CMAKE_HOST_SYSTEM_PROCESSOR` 代替目标架构。

CPU 架构、操作系统和硬件能力是三个不同概念。是否存在硬件解码器、GPU、摄像头或特定像素格式必须在运行时探测，不能仅依据 ARM64 或 x86_64 宏推断。

### 7.3 编译器、类型与 ABI

- `/utf-8`、`/Zc:__cplusplus` 等选项只应用于 MSVC；GCC/Clang 使用各自支持的警告和编码选项。
- 编译器专用属性、内建函数、pragma 和扩展类型只能出现在平台适配层，并必须提供通用实现或明确的功能降级路径。
- 网络协议、文件格式、硬件寄存器和跨模块稳定数据使用 `<cstdint>` 或 Qt 的固定宽度整数类型。
- 不假定 `long`、枚举、指针、`size_t` 或结构体在两个平台上的大小一致。需要固定布局时使用显式字段并执行 `static_assert`。
- 不直接序列化或通过网络发送 C++ 结构体的原始内存；逐字段编码，显式处理字节序和版本。
- 不进行未对齐指针解引用。读取外部缓冲区时使用安全复制或库提供的读取函数，不能依赖 x86_64 对部分未对齐访问的容忍性。
- `size_t`、`qsizetype` 与仍使用 `int` 的 Qt/FFmpeg API 之间转换前必须检查范围，避免 ARM64 或大缓冲场景下截断。
- 公共二进制接口不得暴露编译器相关 STL 布局。模块默认通过同一工具链构建；跨进程边界使用稳定协议而不是 C++ ABI。

### 7.4 文件系统与部署

- 不硬编码 Windows 盘符、反斜杠、路径大小写不敏感行为、`.dll` 或 `.so` 后缀。
- 路径拼接和规范化使用 `QDir`、`QFileInfo`、`QStandardPaths` 或标准文件系统 API。
- 文件名大小写必须与实际资源完全一致，Windows 开发环境也按 Linux 大小写规则审查。
- 可执行文件目录只用于随程序部署的只读资源或明确允许覆盖的主题文件；用户配置、缓存和日志使用 `QStandardPaths` 对应位置。
- QRC 是跨平台资源的稳定回退来源。外部资源加载失败时的行为在两个平台上必须一致且可诊断。
- 启动外部进程时使用 `QProcess` 和参数列表，不手工拼接依赖 PowerShell、CMD 或 POSIX shell 的命令字符串。

### 7.5 UI、全屏与图形后端

- 不在通用 UI 代码中直接操作 Win32 窗口句柄、X11 window、Wayland surface 或 EGL native handle。
- 屏幕选择、全屏、光标、窗口状态和高 DPI 信息优先通过 `QScreen`、`QWindow`、`QWidget` 等 Qt API 获取。
- Linux ARM64 不固定使用 Wayland、X11 或 EGLFS；Qt 平台插件由部署环境配置，业务代码不得据此分叉功能。
- 全屏和控件 reparent 的正确性以 Qt 生命周期与布局契约为依据。Windows 合成器或某个 Linux 插件下观察到的刷新顺序不能当作跨平台保证。
- OpenGL 渲染必须同时考虑桌面 OpenGL 与 OpenGL ES。着色器版本、纹理格式、像素行对齐和扩展在上下文创建后探测，不按 x86_64 或 ARM64 写死。
- 不假定所有平台都支持 QWidget、原生视频 surface 与 OpenGL 控件之间任意 reparent；接入真实渲染后应为各目标图形后端增加回归测试。

### 7.6 Linux ARM64 资源约束

- 所有帧、包、日志和任务队列必须有容量上限，并为实时预览定义过载时的丢弃策略。
- 避免无必要的帧复制、重复像素格式转换和逐帧堆分配；优化前先记录内存带宽、CPU 和延迟数据。
- 网络、解码、硬件和设备 I/O 必须支持超时或中断回调，确保程序能够停止和退出。
- 高频错误和状态日志必须限流或聚合，避免嵌入式存储被持续写满；关键状态转换仍需保留可诊断记录。
- 不以牺牲正确性为由提前引入平台专用优化。平台优化必须封装、可测试，并保留软件实现作为回退。

## 8. Qt 规范

### 8.1 UI 线程

- 所有 QWidget、QLayout、QPixmap 和窗口父子关系修改只能在 UI 线程执行。
- 工作线程不得直接调用 `VideoWidget`。
- 解码线程通过信号槽或线程安全队列传递 `QImage`、状态和统计信息。
- 跨线程连接应依赖明确的自动队列连接，特殊使用 `Qt::QueuedConnection` 时说明原因。
- 禁止使用 `sleep()` 或频繁调用 `QApplication::processEvents()` 掩盖时序问题。

### 8.2 信号与槽

- 使用函数指针形式的类型安全 `connect()`，不使用旧式 `SIGNAL()`/`SLOT()` 宏。
- 动态创建同类控件时集中连接信号；可能重复执行连接路径时使用 `Qt::UniqueConnection` 或显式保证只连接一次。
- 信号表达已经发生的事件或请求，例如 `videoWidgetAdded`、`fullscreenRequested`。
- `handle...` 用于处理下层事件，`notify...` 用于上层协调结果，`update...` 用于刷新派生 UI 状态。
- 信号中传递 QObject 裸指针时，接收方只能借用；异步保存必须转为 `QPointer`。

### 8.3 事件处理

- 已完整处理的事件调用 `accept()` 并立即返回。
- 不处理的事件调用基类实现或 `ignore()`，不得无意吞掉事件。
- 同一个双击、拖拽或键盘事件只能有一个明确处理者，避免父子控件和 event filter 重复触发。

### 8.4 布局与动画

- `QLayout` 是控件几何的唯一管理者。
- 禁止直接对布局管理中的真实控件执行 geometry 动画。
- 布局变化动画使用临时快照覆盖层，完成后删除快照并恢复真实控件。
- 动画期间必须禁用冲突交互，并使用显式状态恢复。
- 新增状态优先扩展强类型状态机，不堆叠多个缺乏约束的 bool。

### 8.5 文本与字符串

- 面向用户的按钮、提示和状态文本使用 `tr()`。
- objectName、styleRole、MIME 类型、资源路径和协议字段使用 `QStringLiteral()` 或字面量。
- 字符串格式化使用 `QString::arg()`，不要手工拼接难以阅读的格式。
- 日志和 UI 中不得输出完整鉴权 token、密码或其他敏感字段。

## 9. QSS 与资源规范

- QSS 统一存放在 `resources/styles/`，不得重新写回控件构造函数。
- 应用样式由 `StyleLoader` 统一加载，控件内不调用局部 `setStyleSheet()` 覆盖主题。
- 选择器使用明确的 `styleRole` 和 objectName，不使用无范围的全局 `QFrame`、`QLabel` 规则。
- 修改 objectName 或 styleRole 视为 UI 样式 API 变更，必须同步更新 QSS、测试和文档。
- QRC 内置资源和可执行文件同级外部资源必须保持相同路径契约。

## 10. 多线程与 FFmpeg 规范

### 10.1 线程模型

- 每路播放器拥有明确的 worker 和线程生命周期，不共享可变解码上下文。
- 网络打开、`av_read_frame()`、解码和像素转换不得在 UI 线程执行。
- 启动、停止和销毁必须成对设计；停止流程要能解除网络阻塞和等待。
- 队列必须有容量上限。实时预览允许丢弃旧帧，禁止无限积压 AVPacket、AVFrame 或 QImage。
- 状态变量跨线程访问时使用信号槽、原子变量或锁，并在注释中说明同步策略。

### 10.2 FFmpeg 资源

- FFmpeg C 头文件放在 `extern "C"` 中。
- 每个 `AVFormatContext`、`AVCodecContext`、`AVPacket`、`AVFrame` 和 `SwsContext` 都必须有唯一、可追踪的释放责任。
- 优先封装 RAII 删除器；若暂时使用裸指针，创建与释放逻辑应相邻且覆盖所有失败分支。
- FFmpeg 返回值统一通过错误转换函数生成可读文本，不在各处重复拼装错误码。
- 时间戳换算使用 `av_rescale_q()` 等官方 API，不手写可能溢出的比例运算。
- 预期的网络中断、EOF 和解码错误通过状态或结果返回，不使用异常跨越 FFmpeg C API 或 Qt 信号槽边界。

### 10.3 硬件解码与硬件帧

- FFmpeg 软件解码是两个目标平台都必须保留的通用回退路径。
- Windows x86_64 与 Linux ARM64 的硬件解码后端通过统一接口和运行时能力探测选择，不把具体后端判断散落到播放器主流程。
- 初始化硬件后端失败时必须记录后端名称和错误原因，并按配置决定回退软件解码或报告不可用。
- 厂商 SDK、DMA/DRM 缓冲、硬件帧上下文和零拷贝图像必须使用 RAII 或专用包装明确释放责任。
- 硬件帧跨线程、跨设备或映射到 CPU/GPU 时，必须明确同步、缓存一致性、像素格式和生命周期边界。
- 不能仅根据 CPU 架构选择硬件后端；同为 Linux ARM64 的设备可能使用完全不同的 VPU、GPU 和驱动。

## 11. 错误处理与日志

- 可恢复失败返回 `bool`、空指针或结构化结果，并提供可诊断信息。
- 参数越界、状态冲突等调用方可预期情况应安全拒绝，不崩溃。
- `qDebug()` 仅用于 Debug 诊断；正常生命周期使用 `qInfo()`，可恢复问题使用 `qWarning()`，无法继续使用当前功能时使用 `qCritical()`。
- 日志应包含设备 ID、状态和错误原因，但不得记录敏感凭据。
- 不静默忽略 FFmpeg、文件、网络或布局恢复失败。
- 循环重试、丢帧和设备异常等高频日志必须限流或汇总，避免在 Linux ARM64 设备上造成 I/O 压力和存储耗尽。
- 平台错误应转换为统一的项目错误语义，同时保留足以定位问题的系统错误码或后端名称。

## 12. CMake 规范

- 最低版本、C++ 标准和平台约束集中在根 `CMakeLists.txt`。
- 使用 `target_include_directories()`、`target_link_libraries()` 和 `target_compile_options()`。
- 正确区分 `PRIVATE`、`PUBLIC` 和 `INTERFACE`，不得为了方便全部设为 PUBLIC。
- 可复用模块编译为独立 target；应用入口只组合模块，不重复编译同一实现文件。
- 测试依赖只在 `BUILD_TESTING` 内查找和链接。
- 新增资源时同步更新 QRC、构建复制和安装规则。
- 不在 CMake 中硬编码开发者个人临时目录；本地 Qt 路径放入用户预设或环境变量。
- 编译选项使用编译器条件或生成器表达式按 target 设置，禁止把 MSVC 参数传给 GCC/Clang，反之亦然。
- 平台源文件和系统库由目标操作系统选择；不得在公共 target 中无条件链接仅 Windows 或仅 Linux 存在的库。
- Linux ARM64 交叉编译参数集中在 toolchain 文件和用户预设中，sysroot、编译器及 Qt 路径不得硬编码到公共 `CMakeLists.txt`。
- Windows x86_64 使用 `out/build-windows-x64/<config>`，Linux ARM64 使用 `out/build-linux-arm64/<config>`，禁止复用 CMake 缓存。

## 13. 测试规范

- 纯算法和状态逻辑优先使用 Qt Test 数据驱动测试。
- UI 所有权、QSS 回退、父子关系和全屏恢复使用冒烟测试。
- 每个缺陷修复应增加能在修复前失败、修复后通过的回归测试。
- 动画测试等待完成信号或使用 `QTRY_*` 超时机制，不使用无边界等待。
- 测试必须验证结果和不变量，例如对象身份、信号次数、逻辑顺序和最终状态，不只验证“没有崩溃”。
- 不能自动判断的视觉流畅度和闪烁必须提供人工验收步骤，不得声明为自动通过。
- 平台无关算法和状态测试必须在 Windows x86_64 与 Linux ARM64 两个目标上运行。
- 全屏、拖拽、OpenGL、平台插件和硬件解码等平台相关行为必须分别验收，不能用一个平台的通过结果代替另一个平台。
- Linux ARM64 至少覆盖一种正式部署图形环境；切换 Wayland、X11 或 EGLFS 时，受影响的全屏和渲染路径需要专项验证。

推荐验证命令：

```powershell
cmake --preset Qt-Debug
cmake --build out/build-windows-x64/debug
ctest --test-dir out/build-windows-x64/debug --output-on-failure
git diff --check
```

Linux ARM64 交叉构建命令：

```bash
cmake --preset Linux-ARM64-Debug
cmake --build --preset Linux-ARM64-Debug
file out/build-linux-arm64/debug/rtmp_monitor
aarch64-linux-gnu-readelf -h out/build-linux-arm64/debug/rtmp_monitor
```

当前 WSL2 只编译并链接 ARM64 测试目标，不运行 Qt GUI 测试。持续集成或发布检查必须在真实 ARM64 硬件上补充 CTest、QPA、全屏和渲染验收；文档不得虚构实机运行结论。

## 14. 注释、待办与 Git

- 源码注释统一使用中文，标识符统一使用英文。
- 公共类、函数、枚举、信号及重要常量必须有 Doxygen。
- 注释解释设计原因、所有权、线程和失败行为，不逐行翻译代码。
- `TODO`、`FIXME` 必须关联 GitHub Issue；`HACK` 和 `SECURITY` 必须说明风险与移除条件。
- 源码中不记录作者、日期或修改历史，历史由 Git 管理。
- 一个提交只包含一个可解释目标，不混入格式化、生成文件或无关重构。
- 不提交 `.vs/`、`out/`、`build/`、测试媒体和本地用户预设。

## 15. 代码审查清单

提交或请求评审前检查：

- [ ] 命名、缩进、括号和 include 顺序符合本文规范。
- [ ] 公共 API 具有中文 Doxygen，线程和所有权边界明确。
- [ ] QObject parent、RAII 或 `QPointer` 能覆盖全部生命周期。
- [ ] UI 线程没有网络、磁盘或解码阻塞。
- [ ] 没有 `sleep()`、无界 `processEvents()` 或无上限帧队列。
- [ ] 动画和全屏切换具有可靠互斥与结束状态。
- [ ] objectName/styleRole 变化已同步 QSS、测试和文档。
- [ ] FFmpeg 创建与释放路径成对，错误信息可诊断。
- [ ] 公共业务和 UI 代码没有泄漏 Win32、POSIX、DRM、EGL 或厂商 SDK 类型。
- [ ] 条件编译使用目标平台宏且规模受控，不以 CPU 架构代替运行时硬件能力探测。
- [ ] 协议、文件和硬件数据没有依赖本机字节序、结构体填充、指针大小或未对齐访问。
- [ ] 文件路径、资源名称和外部进程调用同时满足 Windows x86_64 与 Linux ARM64 约束。
- [ ] 平台优化具有软件回退，硬件帧的所有权、同步和失败行为明确。
- [ ] 高频日志和队列具有资源上限，适合嵌入式设备长期运行。
- [ ] 新行为有自动测试或明确的人工验收步骤。
- [ ] Debug 构建、CTest 和 `git diff --check` 通过。
- [ ] Git 差异不包含构建产物、测试媒体或无关文件。
