# RtmpMonitor 项目代码规范

## 1. 目的与适用范围

本文定义 RtmpMonitor 项目的 C++17、Qt 6、FFmpeg、CMake、QSS 和测试代码规范，目标是让多人开发及 AI 生成代码保持一致，并降低后续接入多路解码、跨线程传帧和硬件渲染时的维护成本。

适用目录：

```text
include/        公共和模块内部头文件
src/            C++ 实现
tests/          自动化测试
resources/      QSS、QRC 等资源
scripts/        开发和验证脚本
```

注释的详细要求以 [《Comment Style Guide》](comment_style_guide.md) 为准；本文重点约束命名、格式、接口、所有权、线程、Qt/FFmpeg 使用和测试方式。两份规范冲突时，针对注释内容优先遵循注释规范。

## 2. 当前工程风格基线

当前代码已经采用以下约定，新代码应继续保持：

- C++17，关闭编译器语言扩展。
- MSVC 和 Qt 6 Widgets，源文件使用 UTF-8。
- 类、结构体和枚举使用 PascalCase。
- 函数、局部变量和参数使用 lowerCamelCase。
- 私有成员变量使用 `_` 后缀。
- 编译期常量使用 `k` 前缀。
- Qt 控件由父对象管理，异步生命周期使用 `QPointer` 保护。
- 公共类和公共函数使用中文 Doxygen。
- CMake 使用 target-based 写法，不使用全局 include/link 配置污染其他目标。
- 测试分为 UI 冒烟测试和 Qt Test 数据驱动测试。

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

## 7. Qt 规范

### 7.1 UI 线程

- 所有 QWidget、QLayout、QPixmap 和窗口父子关系修改只能在 UI 线程执行。
- 工作线程不得直接调用 `VideoWidget`。
- 解码线程通过信号槽或线程安全队列传递 `QImage`、状态和统计信息。
- 跨线程连接应依赖明确的自动队列连接，特殊使用 `Qt::QueuedConnection` 时说明原因。
- 禁止使用 `sleep()` 或频繁调用 `QApplication::processEvents()` 掩盖时序问题。

### 7.2 信号与槽

- 使用函数指针形式的类型安全 `connect()`，不使用旧式 `SIGNAL()`/`SLOT()` 宏。
- 动态创建同类控件时集中连接信号；可能重复执行连接路径时使用 `Qt::UniqueConnection` 或显式保证只连接一次。
- 信号表达已经发生的事件或请求，例如 `videoWidgetAdded`、`fullscreenRequested`。
- `handle...` 用于处理下层事件，`notify...` 用于上层协调结果，`update...` 用于刷新派生 UI 状态。
- 信号中传递 QObject 裸指针时，接收方只能借用；异步保存必须转为 `QPointer`。

### 7.3 事件处理

- 已完整处理的事件调用 `accept()` 并立即返回。
- 不处理的事件调用基类实现或 `ignore()`，不得无意吞掉事件。
- 同一个双击、拖拽或键盘事件只能有一个明确处理者，避免父子控件和 event filter 重复触发。

### 7.4 布局与动画

- `QLayout` 是控件几何的唯一管理者。
- 禁止直接对布局管理中的真实控件执行 geometry 动画。
- 布局变化动画使用临时快照覆盖层，完成后删除快照并恢复真实控件。
- 动画期间必须禁用冲突交互，并使用显式状态恢复。
- 新增状态优先扩展强类型状态机，不堆叠多个缺乏约束的 bool。

### 7.5 文本与字符串

- 面向用户的按钮、提示和状态文本使用 `tr()`。
- objectName、styleRole、MIME 类型、资源路径和协议字段使用 `QStringLiteral()` 或字面量。
- 字符串格式化使用 `QString::arg()`，不要手工拼接难以阅读的格式。
- 日志和 UI 中不得输出完整鉴权 token、密码或其他敏感字段。

## 8. QSS 与资源规范

- QSS 统一存放在 `resources/styles/`，不得重新写回控件构造函数。
- 应用样式由 `StyleLoader` 统一加载，控件内不调用局部 `setStyleSheet()` 覆盖主题。
- 选择器使用明确的 `styleRole` 和 objectName，不使用无范围的全局 `QFrame`、`QLabel` 规则。
- 修改 objectName 或 styleRole 视为 UI 样式 API 变更，必须同步更新 QSS、测试和文档。
- QRC 内置资源和 exe 同级外部资源必须保持相同路径契约。

## 9. 多线程与 FFmpeg 规范

### 9.1 线程模型

- 每路播放器拥有明确的 worker 和线程生命周期，不共享可变解码上下文。
- 网络打开、`av_read_frame()`、解码和像素转换不得在 UI 线程执行。
- 启动、停止和销毁必须成对设计；停止流程要能解除网络阻塞和等待。
- 队列必须有容量上限。实时预览允许丢弃旧帧，禁止无限积压 AVPacket、AVFrame 或 QImage。
- 状态变量跨线程访问时使用信号槽、原子变量或锁，并在注释中说明同步策略。

### 9.2 FFmpeg 资源

- FFmpeg C 头文件放在 `extern "C"` 中。
- 每个 `AVFormatContext`、`AVCodecContext`、`AVPacket`、`AVFrame` 和 `SwsContext` 都必须有唯一、可追踪的释放责任。
- 优先封装 RAII 删除器；若暂时使用裸指针，创建与释放逻辑应相邻且覆盖所有失败分支。
- FFmpeg 返回值统一通过错误转换函数生成可读文本，不在各处重复拼装错误码。
- 时间戳换算使用 `av_rescale_q()` 等官方 API，不手写可能溢出的比例运算。
- 预期的网络中断、EOF 和解码错误通过状态或结果返回，不使用异常跨越 FFmpeg C API 或 Qt 信号槽边界。

## 10. 错误处理与日志

- 可恢复失败返回 `bool`、空指针或结构化结果，并提供可诊断信息。
- 参数越界、状态冲突等调用方可预期情况应安全拒绝，不崩溃。
- `qDebug()` 仅用于 Debug 诊断；正常生命周期使用 `qInfo()`，可恢复问题使用 `qWarning()`，无法继续使用当前功能时使用 `qCritical()`。
- 日志应包含设备 ID、状态和错误原因，但不得记录敏感凭据。
- 不静默忽略 FFmpeg、文件、网络或布局恢复失败。

## 11. CMake 规范

- 最低版本、C++ 标准和平台约束集中在根 `CMakeLists.txt`。
- 使用 `target_include_directories()`、`target_link_libraries()` 和 `target_compile_options()`。
- 正确区分 `PRIVATE`、`PUBLIC` 和 `INTERFACE`，不得为了方便全部设为 PUBLIC。
- 可复用模块编译为独立 target；应用入口只组合模块，不重复编译同一实现文件。
- 测试依赖只在 `BUILD_TESTING` 内查找和链接。
- 新增资源时同步更新 QRC、构建复制和安装规则。
- 不在 CMake 中硬编码开发者个人临时目录；本地 Qt 路径放入用户预设或环境变量。

## 12. 测试规范

- 纯算法和状态逻辑优先使用 Qt Test 数据驱动测试。
- UI 所有权、QSS 回退、父子关系和全屏恢复使用冒烟测试。
- 每个缺陷修复应增加能在修复前失败、修复后通过的回归测试。
- 动画测试等待完成信号或使用 `QTRY_*` 超时机制，不使用无边界等待。
- 测试必须验证结果和不变量，例如对象身份、信号次数、逻辑顺序和最终状态，不只验证“没有崩溃”。
- 不能自动判断的视觉流畅度和闪烁必须提供人工验收步骤，不得声明为自动通过。

推荐验证命令：

```powershell
cmake --preset Qt-Debug
cmake --build out/build/debug
ctest --test-dir out/build/debug --output-on-failure
git diff --check
```

## 13. 注释、待办与 Git

- 源码注释统一使用中文，标识符统一使用英文。
- 公共类、函数、枚举、信号及重要常量必须有 Doxygen。
- 注释解释设计原因、所有权、线程和失败行为，不逐行翻译代码。
- `TODO`、`FIXME` 必须关联 GitHub Issue；`HACK` 和 `SECURITY` 必须说明风险与移除条件。
- 源码中不记录作者、日期或修改历史，历史由 Git 管理。
- 一个提交只包含一个可解释目标，不混入格式化、生成文件或无关重构。
- 不提交 `.vs/`、`out/`、`build/`、测试媒体和本地用户预设。

## 14. 代码审查清单

提交或请求评审前检查：

- [ ] 命名、缩进、括号和 include 顺序符合本文规范。
- [ ] 公共 API 具有中文 Doxygen，线程和所有权边界明确。
- [ ] QObject parent、RAII 或 `QPointer` 能覆盖全部生命周期。
- [ ] UI 线程没有网络、磁盘或解码阻塞。
- [ ] 没有 `sleep()`、无界 `processEvents()` 或无上限帧队列。
- [ ] 动画和全屏切换具有可靠互斥与结束状态。
- [ ] objectName/styleRole 变化已同步 QSS、测试和文档。
- [ ] FFmpeg 创建与释放路径成对，错误信息可诊断。
- [ ] 新行为有自动测试或明确的人工验收步骤。
- [ ] Debug 构建、CTest 和 `git diff --check` 通过。
- [ ] Git 差异不包含构建产物、测试媒体或无关文件。
