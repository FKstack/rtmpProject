# Comment Style Guide

本规范适用于 RtmpMonitor 项目中的 C++、Qt、FFmpeg 和 CMake 相关源码。RtmpMonitor 是同一套运行于 Windows x86_64 PC 和 Linux ARM64 嵌入式硬件盒子的 Qt 程序；注释需要解释工程决策、平台边界、线程约束和资源责任，使两个目标平台上的 RTMP 拉流、多路解码和视频显示都能被准确维护。

## 基本原则

1. 注释重点解释设计原因、约束和副作用，而不是逐行翻译代码。
2. 所有公共类和公共函数必须编写 Doxygen 注释。
3. 并发、所有权、阻塞行为必须明确说明。
4. TODO 和 FIXME 必须关联 GitHub Issue。
5. 不允许在源码中维护修改历史，历史由 Git 管理。
6. 注释内容发生变化时必须与代码同步更新。
7. 平台差异必须写明适用平台、依赖能力和回退行为，不能用含糊的“PC 端”或“ARM 特殊处理”代替准确边界。

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
- Windows x86_64 与 Linux ARM64 之间真实存在差异的接口、条件编译和系统资源。
- 字节序、内存对齐、硬件帧、DMA/DRM 缓冲、GPU 映射和零拷贝生命周期。
- Wayland、X11、EGLFS、桌面 OpenGL 或 OpenGL ES 下不同的限制和回退策略。
- 临时方案、已知问题和安全相关逻辑。

以下内容通常不应写注释：

- 对明显赋值、条件判断、循环语句的重复描述。
- 已经由清晰函数名和类型表达的行为。
- 平台无关接口中重复书写“支持 Windows x86_64 和 Linux ARM64”。
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

### 平台差异

平台无关公共 API 不需要重复标注平台。存在操作系统、CPU 架构、图形后端、驱动或硬件能力限制时，使用项目约定的 `@platform` 标签说明。未来生成 Doxygen 文档时，应在 Doxygen 配置中将 `platform` 注册为自定义别名。

```cpp
/**
 * @brief 创建平台视频输出后端。
 *
 * @platform Windows x86_64 与 Linux ARM64 的实现不同，调用接口保持一致。
 * @note Linux 实现不得假定固定使用 Wayland、X11 或 EGLFS。
 * @return 由调用方独占的视频输出后端；创建失败时返回空指针。
 */
[[nodiscard]] std::unique_ptr<VideoOutputBackend> createVideoOutputBackend();
```

`@platform` 的内容应按实际需要回答：

1. 支持或限制在哪个操作系统和架构。
2. 依赖哪个系统 API、Qt 平台插件、驱动或硬件能力。
3. 能力不可用时是回退、禁用功能还是返回错误。
4. 平台资源由谁释放、在哪个线程访问、操作是否可能阻塞。

不要把 CPU 架构当成硬件能力。例如不能写“ARM64 使用硬件解码”，应写明后端由运行时探测，探测失败后回退 FFmpeg 软件解码。

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

## 平台差异与条件编译

平台适配代码的注释应解释差异产生的原因，而不是翻译预处理条件。较大的平台实现应拆分到不同实现文件，文件和类型名称已经能够表达平台时，不需要在每一行重复平台名称。

```cpp
#if defined(Q_OS_WIN)
// Win32 后端需要把 FFmpeg 硬件帧导入桌面图形上下文；
// Linux ARM64 的对应实现位于 VideoOutputBackendLinux.cpp。
initializeWindowsVideoOutput();
#elif defined(Q_OS_LINUX)
initializeLinuxVideoOutput();
#endif
```

不推荐：

```cpp
#ifdef Q_OS_LINUX
// ARM 特殊处理。
configureOutput();
#endif
```

条件编译注释至少说明差异原因和另一平台的实现或回退位置。仅仅描述宏名称、操作系统名称或代码动作没有维护价值。

涉及协议、文件、硬件寄存器或共享内存时，注释还必须说明：

- 多字节字段使用的字节序和转换位置。
- 缓冲区对齐要求以及是否允许未对齐访问。
- 结构布局是否属于外部 ABI，禁止依赖编译器填充时应明确指出。
- `size_t`、`qsizetype` 与固定宽度整数或 Qt `int` 之间的范围约束。

## 硬件帧与图形资源

涉及 FFmpeg 硬件帧、DMA、DRM、GPU 纹理或零拷贝路径时，注释必须说明以下责任：

1. 缓冲区或句柄由哪个对象拥有，使用哪个 API 释放。
2. 资源可由 CPU、GPU 或两者中的哪一方访问。
3. 跨线程或跨设备传递前需要何种同步。
4. 缓存一致性、像素格式、行对齐和有效生命周期。
5. 硬件路径不可用时是否回退软件解码和普通内存帧。

```cpp
// Linux ARM64 硬件帧由 HardwareFrameHandle 独占 DRM 描述符；渲染线程只借用纹理映射。
// 映射完成信号返回解码线程后才能释放底层帧，导入失败时回退为 CPU 可访问的 AVFrame。
QPointer<HardwareFrameHandle> pendingFrame_;
```

涉及全屏、窗口重建或视频 surface reparent 时，应区分 Qt 提供的通用生命周期保证与特定平台上的观察结果。不得把 Windows 合成器下的绘制顺序描述为 Wayland、X11 或 EGLFS 同样保证的行为。

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

HACK 用于临时绕过环境、第三方库或平台问题。注释必须说明为什么不能采用常规方案，以及计划删除的条件。平台 HACK 还必须注明操作系统、架构、Qt 平台插件、驱动或第三方库版本中实际相关的部分。

```cpp
// HACK: Windows 版 nginx 会按当前工作目录解析相对 hls_path；
// 因此脚本启动前切换到 nginx 根目录。待 nginx 配置改为绝对路径后删除。
```

```cpp
// HACK: Linux ARM64 的 VendorVpu 1.8 驱动在分辨率切换后不会主动刷新旧 DMA 缓冲；
// 当前重新创建硬件帧池。升级到已修复驱动并通过连续切流回归测试后删除。
```

示例中的产品和版本只是格式说明；实际代码必须记录项目真实使用的驱动、SDK 或 Qt 插件信息。

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
- 平台相关窗口或渲染限制应注明是 Qt 通用约束，还是 Windows、Wayland、X11、EGLFS 或 OpenGL ES 的特定行为。

### FFmpeg 资源

- 每个 FFmpeg 上下文的所有权和释放函数必须相邻说明。
- 注释中优先描述缓存、超时、延迟和错误恢复策略，不重复 FFmpeg API 名称本身的含义。
- 影响延迟的选项应说明取舍，例如“减少缓冲可能增加丢帧或网络抖动风险”。
- 硬件解码后端不得只按 x86_64 或 ARM64 命名推断；注释应说明运行时能力探测、软件回退以及硬件帧所有权。

## 代码审查检查清单

提交前检查：

- 公共 API 是否都有中文 Doxygen 注释。
- 注释是否解释“为什么”，而不是重复“做了什么”。
- 新增线程、锁、队列、网络 I/O 或 FFmpeg 资源时，是否说明并发、所有权和阻塞行为。
- 平台专用 API 是否使用 `@platform` 准确说明适用平台、依赖能力和回退行为。
- 条件编译注释是否解释差异原因和另一平台的实现位置，而不是重复宏含义。
- 协议、文件和硬件数据是否说明必要的字节序、对齐和 ABI 约束。
- DMA/DRM、GPU、硬件帧和零拷贝资源是否说明释放、同步、缓存一致性和软件回退。
- 全屏与渲染注释是否区分 Qt 通用契约和特定图形后端的观察结果。
- TODO/FIXME 是否带有 GitHub Issue 编号。
- 是否错误地在源码中加入修改历史。
- 代码逻辑修改后，相关注释是否仍然准确。
