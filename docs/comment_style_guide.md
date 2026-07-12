# Comment Style Guide

本规范适用于 RtmpMonitor 项目中的 C++、Qt、FFmpeg 和 CMake 相关源码。目标是让注释解释工程决策、线程边界和资源责任，使后续接入 RTMP 拉流与多路解码时仍能快速理解代码意图。

## 基本原则

1. 注释重点解释设计原因、约束和副作用，而不是逐行翻译代码。
2. 所有公共类和公共函数必须编写 Doxygen 注释。
3. 并发、所有权、阻塞行为必须明确说明。
4. TODO 和 FIXME 必须关联 GitHub Issue。
5. 不允许在源码中维护修改历史，历史由 Git 管理。
6. 注释内容发生变化时必须与代码同步更新。

## 语言

当前项目的源码注释统一使用中文。

类名、函数名、变量名统一使用英文。Doxygen 标签、Qt 类型名、FFmpeg 类型名和命令行参数保持其原始英文形式。

## 注释范围

以下内容应当写注释：

- 公开类、公开函数、公开枚举和公共配置项。
- 不能从命名直接推断的设计取舍。
- Qt 对象父子关系以外的资源所有权。
- 跨线程信号槽、锁、队列、定时器和线程退出逻辑。
- 可能阻塞的 I/O、网络、FFmpeg 解码或等待操作。
- 与 RTMP、H.264、时间戳、缓存、丢帧和低延迟相关的关键参数。
- 临时方案、已知问题和安全相关逻辑。

以下内容通常不应写注释：

- 对明显赋值、条件判断、循环语句的重复描述。
- 已经由清晰函数名和类型表达的行为。
- 代码修改日期、作者、版本号或手工维护的变更历史。

不推荐：

```cpp
// 将 index 加一。
++index;
```

推荐：

```cpp
// 设备编号从 1 开始展示，避免将内部 0 基索引暴露给用户界面。
const QString deviceName = QStringLiteral("camera%1").arg(index + 1, 3, 10, QLatin1Char('0'));
```

## Doxygen 规范

### 公共类

所有公共类都使用 `/** ... */` 形式的 Doxygen 注释，并至少包含 `@brief`。当类涉及线程、资源或 UI 线程限制时，使用 `@note`、`@warning` 或 `@thread` 说明。

```cpp
/**
 * @brief 单路设备视频的显示槽位。
 *
 * 该类只负责设备名称、状态和视频区域的界面组织。
 * 后续解码模块应通过主线程信号更新该控件，不能从解码线程直接访问它。
 *
 * @thread 仅允许在 Qt UI 线程中创建和更新。
 */
class VideoWidget final : public QFrame
{
    // ...
};
```

### 公共函数

所有公共函数都需要说明作用、参数、返回值和重要限制。参数名和返回值使用 Doxygen 标记；若函数会阻塞、转移所有权或要求特定线程，也必须明确写出。

```cpp
/**
 * @brief 设置界面显示的设备名称。
 *
 * 该函数只更新 UI 文本，不会建立网络连接或启动播放器。
 *
 * @param deviceName 要显示的设备名称。
 * @thread 必须在 Qt UI 线程中调用。
 */
void setDeviceName(const QString &deviceName);

/**
 * @brief 获取指定索引对应的视频显示槽位。
 *
 * @param index 从 0 开始的网格索引。
 * @return 对应的 VideoWidget；索引越界时返回 nullptr。
 * @note 返回的指针由 VideoGridWidget 管理，调用方不得释放。
 */
[[nodiscard]] VideoWidget *videoWidgetAt(int index) const noexcept;
```

### 私有函数和成员

私有实现不强制使用 Doxygen。仅在算法目的、生命周期、并发约束或业务规则不直观时添加简短中文注释。

```cpp
// 只保留最新帧，避免 UI 刷新速度低于解码速度时无限积压内存。
latestFrame_ = std::move(frame);
```

## 并发、所有权与阻塞行为

### 并发

涉及 `QThread`、信号槽连接类型、互斥锁、原子变量或帧队列时，注释必须回答：

1. 代码运行在哪个线程。
2. 哪些对象可以跨线程访问，哪些不可以。
3. 谁负责启动和停止线程。
4. 退出时如何解除阻塞并释放资源。

示例：

```cpp
// 本 worker 运行在 DecodeThread；不得直接访问 VideoWidget。
// frameReady 信号使用队列连接，由 UI 线程消费最新帧。
emit frameReady(image);
```

### 所有权

Qt 父对象机制能够表达的所有权不必重复说明。出现以下情况时必须注明：

- 裸指针由谁释放。
- `std::unique_ptr`、`std::shared_ptr` 的所有权边界。
- FFmpeg 的 `AVFormatContext`、`AVCodecContext`、`AVPacket`、`AVFrame` 的创建与释放责任。
- 函数返回的指针是否借用、转移或共享所有权。

示例：

```cpp
// decoderContext_ 由 FFmpegPlayer 独占；stop() 后通过 avcodec_free_context 释放。
AVCodecContext *decoderContext_ = nullptr;
```

### 阻塞行为

任何可能等待网络、磁盘、线程或硬件资源的函数，都必须说明是否阻塞以及调用限制。

```cpp
/**
 * @brief 打开 RTMP 输入流并读取媒体信息。
 *
 * @warning 该操作可能因网络超时而阻塞，必须在解码工作线程中调用。
 * @return 成功时返回 true，失败时返回 false 并记录错误信息。
 */
bool openStream();
```

## 标记

- TODO：待实现功能。
- FIXME：已知错误。
- HACK：临时解决方案。
- SECURITY：安全相关事项。

### TODO 和 FIXME

TODO 与 FIXME 必须关联 GitHub Issue，使用 `#问题编号` 标注。Issue 应描述背景、验收标准和优先级。

```cpp
// TODO(#123): 接入 QImage 帧显示，并限制 UI 刷新频率为每路最高 25 FPS。
// FIXME(#124): 网络中断时 av_read_frame 可能超过预期超时，需要增加可取消的 I/O 中断回调。
```

没有对应 Issue 时，不应把待办或已知错误写入源码；应先创建 Issue。

### HACK

HACK 用于临时绕过环境、第三方库或平台问题。注释必须说明为什么不能采用常规方案，以及计划删除的条件。

```cpp
// HACK: Windows 版 nginx 会按当前工作目录解析相对 hls_path；
// 因此脚本启动前切换到 nginx 根目录。待 nginx 配置改为绝对路径后删除。
```

### SECURITY

SECURITY 用于认证、授权、敏感数据、日志脱敏、输入校验或网络暴露面相关代码。必须说明风险与保护措施。

```cpp
// SECURITY: 日志中不得记录完整 RTMP 鉴权 token，只允许记录脱敏后的设备标识。
```

## Qt 与 FFmpeg 的补充约定

### Qt UI 更新

- `QWidget`、`QLabel`、`QLayout` 等 UI 对象只能在创建它们的 UI 线程中访问。
- 解码线程必须通过信号槽或线程安全队列传递数据，不能直接修改 `VideoWidget`。
- 需要特殊连接方式时，应在注释中说明使用 `Qt::QueuedConnection` 的原因。

### FFmpeg 资源

- 每个 FFmpeg 上下文的所有权和释放函数必须相邻说明。
- 注释中优先描述缓存、超时、延迟和错误恢复策略，不重复 FFmpeg API 名称本身的含义。
- 影响延迟的选项应说明取舍，例如“减少缓冲可能增加丢帧或网络抖动风险”。

## 代码审查检查清单

提交前检查：

- 公共 API 是否都有中文 Doxygen 注释。
- 注释是否解释“为什么”，而不是重复“做了什么”。
- 新增线程、锁、队列、网络 I/O 或 FFmpeg 资源时，是否说明并发、所有权和阻塞行为。
- TODO/FIXME 是否带有 GitHub Issue 编号。
- 是否错误地在源码中加入修改历史。
- 代码逻辑修改后，相关注释是否仍然准确。
