# Windows x86_64 与 Linux ARM64 多路 RTMP 视频接收与显示项目规划

> 文档分类：项目路线与阶段规划。

## 目录

- [1. 项目目标](#1-项目目标)
- [2. 一句话架构说明](#2-一句话架构说明)
- [3. 整体技术架构](#3-整体技术架构)
- [4. 核心技术栈](#4-核心技术栈)
- [5. 项目模块划分](#5-项目模块划分)
- [6. 推荐项目目录结构](#6-推荐项目目录结构)
- [7. 开发环境配置](#7-开发环境配置)
- [8. 实施周期规划](#8-实施周期规划)
- [9. MVP 最小可行版本](#9-mvp-最小可行版本)
- [10. 后期增强版本](#10-后期增强版本)
- [11. 新手学习路线](#11-新手学习路线)
- [12. 风险点与规避方案](#12-风险点与规避方案)
- [13. 推荐 AI 编程工作流](#13-推荐-ai-编程工作流)
- [14. 最终交付物清单](#14-最终交付物清单)

---

## 1. 项目目标

本项目计划开发一套同时运行在 Windows x86_64 PC 和 Linux ARM64 嵌入式硬件盒子上的多路摄像头实时显示软件。多个前端设备将摄像头画面编码为 H.264，并通过 RTMP 主动推送到网络中的 RTMP Server。RtmpMonitor 在两类目标平台上使用同一套 Qt 6 Widgets + FFmpeg 代码完成拉流、解析、解码、渲染和多宫格交互。

两个目标平台的功能语义保持一致：Windows x86_64 主要用于开发、调试和 PC 部署；Linux ARM64 主要用于带显示输出的嵌入式硬件盒子。Linux 端不绑定单一图形栈，部署时根据设备系统选择 Wayland、X11 或 EGLFS。

项目最终希望解决以下问题：

| 目标 | 说明 |
| --- | --- |
| 多设备接入 | 支持多个嵌入式设备同时推送摄像头画面。 |
| 实时预览 | Windows PC 和 ARM64 硬件盒子都能低延迟查看每一路设备画面。 |
| 同一程序跨平台 | 业务、UI、播放和状态管理共用代码，仅隔离必要的系统、图形和硬件差异。 |
| 工程可扩展 | 初期先做 Windows 可运行原型，后续增加 Linux ARM64 交叉构建、多路、重连、统计、录像和硬件解码。 |
| 学习友好 | 对 C++/Qt 开发者逐步引入音视频知识，避免一开始陷入 RTMP Server、H.264 码流细节和 FFmpeg 编译复杂度。 |
| AI 辅助开发 | 将项目拆成小阶段，让 Codex 可以一次实现一个明确模块。 |

建议的总体路线：

1. 先使用 SRS 或 nginx-rtmp 作为外部 RTMP Server，快速验证“设备推流 -> PC 可播放”的链路。
2. 先用 Windows x86_64 + MSVC 开发 Qt + FFmpeg 客户端，实现一路 RTMP 拉流、解码和显示。
3. 完成 4 路真实播放和故障隔离，作为多路基线。
4. 扩展为 0～16 路动态连接，并把阻塞网络、共享解码池和 UI 展示节奏解耦。
5. 建立 Linux ARM64 交叉编译门禁，确保同一源码生成 AArch64 Linux ELF。
6. 在真实 ARM64 盒子验证 QPA、FFmpeg 和长期运行；性能数据不足时再引入硬件解码。

---

## 2. 一句话架构说明

前端设备把 H.264 视频通过 RTMP 推送到 RTMP Server，运行在 Windows x86_64 PC 或 Linux ARM64 硬件盒子上的同一套 Qt 程序使用 FFmpeg 拉流并解码成图像帧，最后在 Qt 窗口中以多宫格方式显示实时画面。

更直观地说：

```text
设备负责“推流”
RTMP Server 负责“收流和转发”
FFmpeg 负责“拉流、解析、解码、转图像”
Qt 负责“窗口、布局、显示和交互”
```

---

## 3. 整体技术架构

### 3.1 初期推荐架构

初期不要从零实现 RTMP Server。推荐先使用成熟的 SRS 或 nginx-rtmp 验证整条链路。

```text
+------------------+       RTMP Push        +-------------------+
| 嵌入式设备 A      | ---------------------> |                   |
| H.264 Encoder     |                        |                   |
+------------------+                        |                   |
                                             |                   |
+------------------+       RTMP Push        |   SRS / nginx     |
| 嵌入式设备 B      | ---------------------> |   RTMP Server     |
| H.264 Encoder     |                        |                   |
+------------------+                        |                   |
                                             |                   |
+------------------+       RTMP Push        |                   |
| FFmpeg/OBS 模拟   | ---------------------> |                   |
| 测试推流源        |                        +---------+---------+
+------------------+                                  |
                                                      | RTMP Pull
                                                      v
                                      +---------------+----------------+
                                      | RtmpMonitor 跨平台客户端        |
                                      | Windows x86_64 / Linux ARM64    |
                                      | FFmpeg demux / decode H.264     |
                                      | sws_scale: YUV -> RGB/QImage    |
                                      | QThread worker                  |
                                      | Qt Widgets / OpenGL display     |
                                      +---------------+----------------+
                                                      |
                                                      v
                                      +---------------+----------------+
                                      | 多宫格实时画面显示窗口          |
                                      +--------------------------------+
```

### 3.2 数据流说明

```text
嵌入式设备
  -> 摄像头采集
  -> H.264 编码
  -> RTMP 推流
  -> RTMP Server 接收
  -> Qt 客户端通过 FFmpeg 拉流
  -> avformat 解析封装
  -> avcodec 解码 H.264
  -> swscale 转 RGB
  -> QImage / OpenGL 纹理
  -> 多宫格显示
```

### 3.3 分阶段架构演进

| 阶段 | 架构形态 | 推荐程度 | 说明 |
| --- | --- | --- | --- |
| MVP | FFmpeg 命令推流 + SRS + Qt 拉一路 | 强烈推荐 | 最短路径验证可行性。 |
| 原型版 | 多个 RTMP URL + Qt 4 宫格显示 | 推荐 | 验证多路线程、布局、基础性能。 |
| 工程版 | 状态管理、断线重连、日志、配置文件 | 推荐 | 具备可演示和可调试能力。 |
| ARM64 构建版 | WSL2 交叉编译 + ARM64 Qt/FFmpeg + sysroot | 16 路动态源码已接入 | 主程序和测试目标链接 ARM64 FFmpeg；仍需真实设备播放验收。 |
| ARM64 实机版 | 硬件盒子部署 + QPA/渲染/稳定性验证 | 发布前必做 | 交叉编译成功不能代替真实设备运行验证。 |
| 优化版 | OpenGL 渲染、硬件解码、低延迟调优 | 后期 | 解决多路性能和延迟问题。 |
| 高级版 | 内置 RTMP Server | 谨慎后置 | 协议复杂，先不要作为第一目标。 |

---

## 4. 核心技术栈

| 技术 | 在项目中的作用 | 新手建议 | 后期优化方向 |
| --- | --- | --- | --- |
| C++ | 主要开发语言，负责业务逻辑、FFmpeg 封装、线程管理。 | 使用 RAII 管理资源，避免裸指针失控。 | 抽象播放器接口、资源池、性能优化。 |
| Qt Widgets | 在 Windows x86_64 与 Linux ARM64 上实现相同的窗口、多宫格、状态栏和交互。 | 先用 QWidget、QGridLayout、QLabel 做可运行界面。 | 自定义 VideoWidget、跨 QPA 平台验证、OpenGL Widget。 |
| FFmpeg | 拉取 RTMP 流、解析封装、解码 H.264、转换像素格式。 | 先理解 avformat、avcodec、AVPacket、AVFrame。 | 硬件解码、低延迟参数、零拷贝渲染。 |
| RTMP | 设备推送视频流的传输协议。 | 初期只需要会推流、拉流和配置地址。 | 后期再研究协议细节和内置服务端。 |
| H.264 | 固定视频编码格式。 | 先知道它是压缩后的视频码流，需要解码成图像帧。 | 理解 SPS/PPS、GOP、I/P/B 帧、延迟来源。 |
| SRS/nginx-rtmp | 外部 RTMP Server，用于接收设备推流并供 RtmpMonitor 拉流。 | 推荐优先使用 SRS，文档和调试体验较好。 | 生产部署、鉴权、转协议、集群。 |
| OpenGL / OpenGL ES | Windows 与 ARM64 上的高性能视频显示。 | 初期可以不用，先用 QLabel/QImage。 | 跨桌面 OpenGL/OpenGL ES 的 YUV 纹理渲染。 |
| 多线程 | 避免拉流和解码阻塞 Qt 主线程。 | 每一路视频使用独立 worker 或线程池模型。 | 控制线程数量、帧队列、背压、任务调度。 |
| CMake 交叉编译 | 从 Windows 开发机生成 Linux ARM64 程序。 | 先理解编译器、sysroot、目标 Qt/FFmpeg 是一套整体。 | 双平台 Preset、CI 和可复现 SDK。 |

### 4.1 C++

项目中的 C++ 代码建议遵循以下原则：

- 固定使用 C++17，并关闭编译器语言扩展，避免 MSVC 与 GCC/Clang 行为分叉。
- FFmpeg 的 C API 使用 RAII 包装，确保 `AVFormatContext`、`AVCodecContext`、`AVFrame`、`AVPacket` 正确释放。
- 跨线程传递图像时优先使用 Qt 信号槽，避免直接跨线程操作 UI。
- 视频流状态使用枚举表达，例如 `Disconnected`、`Connecting`、`Playing`、`Reconnecting`、`Error`。

### 4.2 Qt Widgets

初期推荐 Qt Widgets，而不是直接上 QML。原因是：

- C++/Qt Widgets 适合 PC 和带显示输出的嵌入式监控终端。
- QGridLayout 很适合多宫格布局。
- QLabel + QImage 足够完成 MVP。
- 后期可以把 QLabel 显示替换为自定义 OpenGL 视频控件。
- Linux ARM64 端不写死 Wayland、X11 或 EGLFS，部署时通过 Qt 平台插件配置选择。

### 4.3 FFmpeg

FFmpeg 在项目中主要负责：

```text
RTMP URL
  -> avformat_open_input
  -> avformat_find_stream_info
  -> 找到 video stream
  -> avcodec_find_decoder
  -> avcodec_open2
  -> av_read_frame
  -> avcodec_send_packet
  -> avcodec_receive_frame
  -> sws_scale 转 RGB
  -> QImage
```

### 4.4 RTMP

RTMP 是推流和拉流协议。本项目中：

- 设备端使用 RTMP Push。
- RTMP Server 接收推流。
- RtmpMonitor 在 Windows x86_64 或 Linux ARM64 上使用 RTMP Pull。

典型地址示例：

```text
rtmp://127.0.0.1/live/camera001
rtmp://192.168.1.100/live/device_a
```

### 4.5 H.264

H.264 是编码格式，不是文件格式。设备发送的 H.264 码流经过 RTMP 封装传输，RtmpMonitor 在两个目标平台上都需要先解封装，再解码成原始视频帧。

新手只需要先理解：

- H.264 是压缩后的视频数据。
- 解码前的数据通常是 `AVPacket`。
- 解码后的画面通常是 `AVFrame`。
- 显示前通常要从 YUV 转换成 RGB。

### 4.6 SRS/nginx-rtmp

推荐优先使用 SRS：

- 配置简单。
- 调试工具丰富。
- 支持 RTMP、HTTP-FLV、HLS、WebRTC 等多种能力。
- 适合原型阶段验证链路。

nginx-rtmp 也可用，但 Windows 下部署和维护通常不如 Docker/Linux 环境下顺手。

### 4.7 OpenGL

OpenGL 不建议放进第一版 MVP。原因是它会引入着色器、纹理、上下文、像素格式等额外概念。

推荐顺序：

```text
QLabel + QImage
  -> 自定义 QWidget paintEvent
  -> QOpenGLWidget RGB 纹理
  -> 兼容桌面 OpenGL 与 OpenGL ES 的 YUV 三平面纹理
```

不能只在 Windows 上验证 shader 和纹理格式。Linux ARM64 设备可能使用 OpenGL ES、EGLFS 或厂商 GPU 驱动，最终必须在实际图形环境中测试。

### 4.8 多线程

Qt UI 主线程只负责界面，不要在主线程中执行 RTMP 拉流和 FFmpeg 解码。

推荐模型：

```text
MainWindow / UI Thread
  -> 创建 VideoPlayerController
  -> 创建 DecodeWorker 并移动到 QThread
  -> DecodeWorker 拉流解码
  -> emit frameReady(QImage)
  -> VideoWidget 在主线程刷新显示
```

---

## 5. 项目模块划分

| 模块 | 职责 | 输入 | 输出 | 主要类设计建议 |
| --- | --- | --- | --- | --- |
| 设备管理模块 | 管理设备列表、设备名称、RTMP 地址、在线状态。 | 配置文件、用户添加的设备信息。 | 设备列表、设备状态变化信号。 | `DeviceInfo`、`DeviceManager`、`DeviceStatus`。 |
| RTMP/视频流接收模块 | 打开 RTMP URL，读取封装包，处理连接超时和错误。 | RTMP URL、连接参数。 | 视频 `AVPacket` 或解复用后的数据。 | `StreamReceiver`、`RtmpStreamSession`。 |
| FFmpeg 解码模块 | 使用 FFmpeg 解码 H.264，并转换为可显示图像。 | `AVPacket` 或 RTMP URL。 | `AVFrame`、`QImage`、帧率统计。 | `FFmpegDecoder`、`FFmpegPlayer`、`FrameConverter`。 |
| 视频显示模块 | 显示单路视频画面，处理黑屏、错误提示、截图。 | `QImage`、状态信息。 | 屏幕画面。 | `VideoWidget`、`VideoRenderWidget`。 |
| 多路布局模块 | 管理 1/4/9/16 宫格布局，动态绑定设备画面。 | 设备列表、布局模式。 | 多宫格 UI。 | `VideoGridWidget`、`LayoutManager`。 |
| 日志与状态模块 | 记录拉流、解码、重连、错误信息，展示运行状态。 | 模块日志、错误码、状态事件。 | 日志文件、状态栏、调试面板。 | `LogManager`、`StatusPanel`。 |
| 配置管理模块 | 保存设备列表、RTMP Server 地址、窗口布局、低延迟参数。 | JSON/INI 配置文件。 | 配置对象、运行参数。 | `AppConfig`、`ConfigManager`。 |
| 平台适配模块 | 隔离窗口系统、硬件解码、系统路径和厂商 SDK 差异。 | 平台能力查询、统一业务请求。 | 平台无关结果或后端接口。 | `PlatformCapabilities`、`VideoOutputBackend`、`HardwareDecoderBackend`。 |
| 后期内置 RTMP Server 模块 | 在 RtmpMonitor 内部接收设备 RTMP 推流。 | 设备 RTMP Push。 | 内部流会话、可供解码的数据源。 | `EmbeddedRtmpServer`、`RtmpSession`，后期再做。 |

### 5.1 推荐类关系

```text
MainWindow
  -> DeviceManager
  -> VideoGridWidget
       -> VideoWidget[0]
       -> VideoWidget[1]
       -> VideoWidget[2]
       -> VideoWidget[3]
  -> LogManager
  -> ConfigManager

DeviceManager
  -> DeviceInfo
  -> DeviceStatus

VideoPlayerController
  -> QThread
  -> FFmpegPlayer / DecodeWorker
       -> StreamReceiver
       -> FFmpegDecoder
       -> FrameConverter

PlatformCapabilities
  -> Windows x86_64 platform backend
  -> Linux ARM64 platform backend
```

### 5.2 新手优先实现的模块

- `VideoWidget`：先用 QLabel 显示 QImage。
- `VideoGridWidget`：UI 原型已支持手动添加 1～16 路，并在 1x1 至 4x4 之间自动布局；真实解码仍建议先从一路开始。
- `FFmpegPlayer`：先支持一路 RTMP 拉流解码。
- `LogManager`：先输出到 Qt 控制台或 QTextEdit。
- `ConfigManager`：初期可以先硬编码 RTMP URL，后续再读 JSON/INI。

### 5.3 后期再实现的模块

- 内置 RTMP Server。
- OpenGL YUV 纹理渲染。
- 硬件解码。
- 复杂设备认证。
- 录像、截图、回放。
- 大规模多路调度。

---

## 6. 推荐项目目录结构

适合 CMake + Qt + FFmpeg 的目录结构如下：

```text
rtmpProject/
  CMakeLists.txt
  CMakePresets.json
  README.md
  cmake/
    ProjectOptions.cmake
    toolchains/
      aarch64-linux.cmake
  docs/
    README.md
    roadmap/
      project_plan.md
    guides/
      architecture/
      build-and-testing/
        cross_platform_build.md
      development/
    weeks/
      weekN/
  include/
    common/
      app/
        StyleLoader.h
      core/
        Singleton.h
      ui/
        MainWindow.h
        VideoWidget.h
        VideoGridWidget.h
        FullscreenVideoWindow.h
        FullscreenControlBar.h
      media/                    # 接入 FFmpeg 时新增
      device/                   # 接入设备管理时新增
  src/
    main.cpp
    common/
      app/
        StyleLoader.cpp
      ui/
        MainWindow.cpp
        VideoWidget.cpp
        VideoGridWidget.cpp
        FullscreenVideoWindow.cpp
        FullscreenControlBar.cpp
      media/                    # 接入 FFmpeg 时新增
      device/                   # 接入设备管理时新增
    platform/
      windows/                  # Windows 专用实现
      linux/                    # Linux/ARM 专用实现
  resources/
    app.qrc
    icons/
    styles/
  third_party/
    windows-x86_64/
      ffmpeg/
    linux-arm64/
      ffmpeg/
      qt/
      sysroot/
  scripts/
    setup_arm64_build_env.sh
    start_srs.ps1
    push_test_stream.ps1
    push_test_stream.bat
    deploy_arm64.sh
  tests/
    CMakeLists.txt
    test_config.cpp
    test_device_manager.cpp
  configs/
    app.example.json
    devices.example.json
  out/                          # Git 忽略，不跨平台复用缓存
    build-windows-x64/
      debug/
      release/
    build-linux-arm64/
      debug/
      release/
```

跨平台依赖不能共用同一套二进制目录。MSVC 生成的 `.lib`/`.dll` 只供 Windows x86_64 使用，AArch64 GCC/Clang 生成的 `.a`/`.so` 只供 Linux ARM64 使用；两者的头文件版本、编译选项和运行库必须分别记录。

上面的 `third_party/linux-arm64/` 表示依赖边界，不代表要把完整厂商 SDK、sysroot 或闭源库提交到 Git。大型工具链和本机路径应由环境变量、CMake User Preset 或包管理系统提供；仓库只提交 toolchain、版本清单、校验方式和不含敏感信息的示例配置。

### 6.1 当前目录边界的使用规则

- `include/common/` 和 `src/common/` 保存两个目标平台共享的接口与实现，现有 UI、样式服务和后续播放器状态机默认放在这里。
- `src/platform/windows/` 与 `src/platform/linux/` 只保存真实存在的平台差异，不为填充目录创建空类或占位实现。
- `src/main.cpp` 是跨平台组合入口，不包含 Win32、POSIX、DRM、EGL 或厂商 SDK 细节。
- `media/`、`device/` 和平台相关接口在对应功能首次落地时创建，不使用 `.gitkeep` 维持空目录。
- `cmake/ProjectOptions.cmake` 统一编译器选项；`cmake/toolchains/aarch64-linux.cmake` 只描述目标工具链，不硬编码 Qt、FFmpeg 和个人 SDK 路径。
- Windows Debug/Release 与 Linux ARM64 Debug/Release 各自使用独立构建目录，任何情况下都不能复用 `CMakeCache.txt`。

---

## 7. 开发环境配置

### 7.1 双平台工具链结论

需要增加 G++，但不是把普通 Windows MinGW G++ 混入当前 MSVC 构建。项目需要两套彼此独立的目标工具链：

| 构建目标 | 编译器 | 目标产物 | 依赖库 |
| --- | --- | --- | --- |
| Windows x86_64 | Visual Studio 2022 / MSVC | Windows PE `.exe`、`.dll` | MSVC 版本 Qt 6、Windows x86_64 FFmpeg |
| Linux ARM64 | `aarch64-linux-gnu-gcc/g++` 或厂商 SDK 编译器 | Linux AArch64 ELF、`.so` | ARM64 Linux Qt 6、FFmpeg、目标 sysroot |

普通 MinGW G++ 默认生成 Windows x86_64 程序，不能生成可在 Linux ARM64 上运行的程序。真正需要的是目标三元组类似 `aarch64-linux-gnu` 的交叉工具链。MSVC 与 ARM64 G++ 不能链接同一批二进制库，也不能共用 CMake 构建目录。

推荐开发方式：

```text
Windows 11 主机
  -> Visual Studio 2022 + MSVC
       -> 构建、调试 Windows x86_64 版本
  -> WSL2 Ubuntu
       -> aarch64-linux-gnu-g++
       -> ARM64 Linux sysroot
       -> ARM64 Qt 6 + ARM64 FFmpeg
       -> 交叉生成 Linux AArch64 程序
  -> scp/rsync 到真实 ARM64 硬件盒子
       -> 验证 Qt 界面、视频、OpenGL ES 和稳定性
```

若硬件厂商提供 BSP/SDK、交叉编译器、sysroot 和 Qt 包，应优先使用厂商提供的一整套环境。通用 Ubuntu 交叉编译器适合验证纯 C++，但最终链接和运行仍必须匹配盒子的 libc、动态加载器、GPU 驱动、Qt 平台插件和 FFmpeg 版本。

如果厂商明确提供可在 Windows 原生运行的 AArch64 Linux 交叉编译器，也可以为它单独编写 CMake toolchain；但大多数 Linux ARM64 SDK 在 Linux 主机环境下验证更充分，因此默认推荐 WSL2。无论宿主环境是什么，目标编译器、sysroot、Qt 和 FFmpeg 必须属于同一个 Linux ARM64 ABI 体系。

### 7.2 Windows x86_64 开发工具清单

| 工具 | 推荐版本 | 作用 | 备注 |
| --- | --- | --- | --- |
| Visual Studio 2022 | Community 或更高 | 提供 MSVC 编译器、Windows SDK、调试器。 | 安装“使用 C++ 的桌面开发”。 |
| Qt 6.x | Qt 6.5 LTS 或 Qt 6.8+ | 提供 Qt Widgets、信号槽、QThread、QOpenGLWidget。 | 选择 MSVC 2022 对应套件。 |
| CMake | 3.24+ | 管理 C++/Qt/FFmpeg 构建。 | Qt 6 项目推荐 CMake。 |
| FFmpeg 命令行工具 | 稳定预编译版本 | 用于模拟设备推流、测试拉流、分析视频。 | 需要把 `ffmpeg.exe`、`ffplay.exe` 加入 PATH。 |
| FFmpeg 开发库 | 与 MSVC 兼容的 dev/shared 包 | Qt 程序链接 `avformat`、`avcodec`、`avutil`、`swscale`。 | 注意 include、lib、dll 三类文件。 |
| SRS 或 nginx-rtmp | SRS 5/6 或 nginx-rtmp | 外部 RTMP Server，用于接收推流。 | 初期推荐 Docker 跑 SRS。 |
| VLC / ffplay / OBS | 最新稳定版 | 辅助推流、播放和验证链路。 | OBS 可以模拟摄像头推流。 |
| Git | 最新稳定版 | 版本管理。 | 每完成一阶段建议提交一次。 |

### 7.3 Linux ARM64 交叉编译环境

建议在 WSL2 Ubuntu 中准备：

| 工具或内容 | 作用 | 注意事项 |
| --- | --- | --- |
| `gcc-aarch64-linux-gnu`、`g++-aarch64-linux-gnu` | 在 x86_64 Linux 环境生成 AArch64 Linux 目标文件。 | 编译器版本应与厂商 SDK/sysroot 兼容。 |
| CMake、Ninja | 配置和执行 ARM64 构建。 | 与 Windows 构建使用不同 binary directory。 |
| ARM64 sysroot | 提供目标系统头文件、libc、动态加载器和系统库。 | 优先从厂商 SDK 或目标系统导出，不能直接使用 Windows 目录。 |
| ARM64 Qt 6 | 提供目标端 Qt Widgets、Gui、平台插件和 OpenGL ES 支持。 | Windows MSVC Qt 库不能复用；还需要可在构建主机执行的 `moc`、`rcc` 等工具。 |
| ARM64 FFmpeg 开发库 | 提供 ARM64 的 `avformat`、`avcodec`、`avutil`、`swscale`。 | 头文件和 `.so` 必须与目标运行环境及许可证配置一致。 |
| QEMU user mode（可选） | 在 WSL2 中执行部分 ARM64 Linux 命令行测试。 | 适合无界面逻辑，不代表 GPU、QPA 或硬件解码可用。 |
| SSH、SCP 或 rsync | 把程序和依赖部署到硬件盒子。 | 应记录目标目录、运行用户和动态库搜索路径。 |

截至 2026 年 7 月，当前开发机已经建立可复现的通用 Ubuntu 22.04 ARM64 构建基线：

- WSL2 发行版：`Ubuntu-22.04-New`，虚拟磁盘位于 G 盘，swap 位于 F 盘。
- 宿主工具：x86_64 Qt 6.2.4 的 `moc`、`rcc`、`uic`。
- 目标依赖：`/opt/rtmp-monitor/sysroots/jammy-arm64` 中的 ARM64 Qt 6.2.4 与 OpenGL 开发库。
- 构建产物：E 盘仓库的 `out/build-linux-arm64/debug`。
- 已验证范围：Qt Widgets 主程序和四个测试目标完成 ARM64 编译、链接及 ELF 检查；主程序及多路测试目标均为 ELF64/AArch64，并已链接 sysroot 中的 ARM64 FFmpeg 8.1.2；QEMU 可运行最小 ARM64 C++17 程序。
- 未验证范围：真实盒子上的 QPA、窗口、全屏、OpenGL、输入交互、四路实况播放和性能。

环境配置命令及存储约束见 `scripts/setup_arm64_build_env.sh` 和
[跨平台构建说明](../guides/build-and-testing/cross_platform_build.md)。

Windows 侧只安装一个通用 G++ 不够，原因是完整 Qt 程序的交叉链接至少依赖以下四项：

```text
ARM64 交叉编译器
  + 与设备系统匹配的 sysroot
  + ARM64 Qt 6 目标库和平台插件
  + ARM64 FFmpeg 目标库
  = 可部署的 Linux ARM64 RtmpMonitor
```

### 7.4 CMake ARM64 toolchain 设计

仓库已经在 `cmake/toolchains/aarch64-linux.cmake` 中集中描述目标平台。工具链默认使用 `aarch64-linux-gnu-gcc/g++`；可选 sysroot 通过 `ARM64_SYSROOT` 注入，Qt、FFmpeg 和厂商 SDK 继续由 CMake Preset 或环境变量提供，不能把个人目录提交到仓库：

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

if(DEFINED ARM64_SYSROOT AND NOT ARM64_SYSROOT STREQUAL "")
    set(CMAKE_SYSROOT "${ARM64_SYSROOT}")
    list(PREPEND CMAKE_FIND_ROOT_PATH "${ARM64_SYSROOT}")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

不同厂商 SDK 对 sysroot 和编译器前缀有自己的要求，实际 toolchain 文件应以 SDK 文档为准。当前 `Linux-ARM64-Debug` Preset 已通过完整 Qt 项目构建验证；Release preset 应在发布参数和目标硬件确定后增加，不能复制一个未经验证的占位配置。

当前根 `CMakeLists.txt` 已完成第一轮跨平台构建改造：

1. Windows 只接受 MSVC，Linux 只接受 ARM64 GCC/Clang，其他目标组合会给出明确错误。
2. `/utf-8`、`/Zc:__cplusplus` 仅在 MSVC target 上启用，GCC/Clang 使用自己的警告选项。
3. MinGW Qt 检查只作用于 Windows 构建，不会误伤 Linux ARM64 Qt。
4. `WIN32_EXECUTABLE` 只用于 Windows 主程序，Linux ARM64 生成普通 ELF。
5. 共享 target 使用 `include/common` 和 `src/common`，平台实现后续按目标系统选择。
6. Windows 与 Linux ARM64 使用独立构建目录。ARM64 Qt 6、FFmpeg 8.1.2 与通用 Jammy sysroot 已齐备；确定硬件后仍需用厂商 sysroot 复验 ABI。

### 7.5 在 Windows 开发机上验证 ARM64 构建

当前在 WSL2 中执行：

```bash
cmake --preset Linux-ARM64-Debug
cmake --build --preset Linux-ARM64-Debug
file out/build-linux-arm64/debug/rtmp_monitor
aarch64-linux-gnu-readelf -h out/build-linux-arm64/debug/rtmp_monitor
aarch64-linux-gnu-readelf -d out/build-linux-arm64/debug/rtmp_monitor
```

检查重点：

- `file` 应报告 ARM aarch64/Linux ELF，而不是 Windows PE 或 x86-64 ELF。
- `readelf -h` 的 Machine 应为 AArch64。
- `readelf -d` 中的 Qt 和系统动态库应来自 ARM64 sysroot，不应出现 Windows 路径或 x86_64 库；接入 FFmpeg 后同样检查其目标架构。
- 构建日志中不应出现 MSVC 参数，也不应误用 Windows Qt Kit。

“在 Windows 上测试 ARM 平台”需要分层理解：

| 验证层级 | 能证明什么 | 不能证明什么 |
| --- | --- | --- |
| WSL2 交叉配置、编译和链接 | 源码能够被 ARM64 GCC/Clang 编译，并生成 AArch64 ELF。 | 程序一定能在目标盒子启动。 |
| QEMU 或 ARM64 容器测试 | 部分纯逻辑、Qt Core、配置和命令行测试可以运行。 | Wayland/X11/EGLFS、OpenGL ES、VPU/GPU 和真实性能正常。 |
| 真实 ARM64 硬件盒子测试 | 验证动态库、Qt 平台插件、窗口、全屏、渲染、网络和硬件能力。 | 不能被模拟器或交叉编译完全替代。 |

因此，Windows 开发机可以完成大部分交叉构建检查，但不能只靠“成功生成文件”宣布 ARM64 平台支持完成。正式发布前必须在目标硬件上执行 CTest、启动程序，并验证实际使用的 Qt 平台插件。

### 7.6 ARM64 实机部署与运行检查

部署到盒子后先检查动态依赖和图形环境：

```bash
file ./rtmp_monitor
ldd ./rtmp_monitor
```

再根据设备镜像选择一种正式支持的 Qt 平台插件，例如：

```bash
QT_QPA_PLATFORM=wayland ./rtmp_monitor
QT_QPA_PLATFORM=xcb ./rtmp_monitor
QT_QPA_PLATFORM=eglfs ./rtmp_monitor
```

这里只表示三种可能的部署方式，不要求同一台盒子同时支持三种插件。项目不得在代码中固定其中一种；最终选定的硬件镜像必须记录正式支持的插件、GPU 驱动和 Qt 配置。

实机最低验收内容：

- 程序启动、退出和自动重连没有崩溃或线程残留。
- 动态 1～16 路布局、拖拽、全屏和悬浮控制栏行为与 Windows 一致。
- 实际 QPA 插件下没有窗口闪烁、黑底异常、光标或 reparent 问题。
- 软件解码可以播放 H.264；硬件解码未启用或初始化失败时能够安全回退。
- 连续运行期间 CPU、内存、温度、日志大小和帧率处于可接受范围。

### 7.7 工具职责关系

```text
Visual Studio 2022
  -> 编译和调试 Windows x86_64 程序

WSL2 + aarch64-linux-gnu-g++
  -> 交叉生成 Linux ARM64 程序

ARM64 sysroot + Qt 6 + FFmpeg
  -> 为目标系统提供头文件、库和 Qt 平台插件

Qt 6.x
  -> 两个平台共用的 UI、多线程、窗口显示接口

CMake
  -> 管理工程和第三方库链接

FFmpeg 命令行
  -> 模拟设备推流、验证 RTMP 地址

FFmpeg 开发库
  -> 嵌入到 Qt 程序中进行拉流解码

SRS/nginx-rtmp
  -> 接收设备或测试工具推来的 RTMP 流

VLC/ffplay/OBS
  -> 作为测试播放器或测试推流源
```

### 7.8 RTMP 链路验证命令

启动 RTMP Server 后，可以使用 FFmpeg 模拟一路设备推流：

```bash
ffmpeg -re -stream_loop -1 -i test.mp4 -c:v libx264 -an -f flv rtmp://127.0.0.1/live/camera001
```

使用 ffplay 验证拉流：

```bash
ffplay -fflags nobuffer -flags low_delay rtmp://127.0.0.1/live/camera001
```

如果 ffplay 能播放，说明 RTMP Server 和推流链路基本正常。此时再让 Qt 程序拉同一个地址，排查难度会小很多。

---

## 8. 实施周期规划

本项目推荐按 6 周推进。每周目标都要小而明确，尽量让 AI 编程一次只完成一个模块。

### 8.1 总览

| 周期 | 阶段目标 | 主要产出 | 新手优先级 |
| --- | --- | --- | --- |
| 第 1 周 | 跑通 RTMP 推流链路 | SRS/nginx-rtmp 可用，ffplay 能播放 RTMP 流。 | 必做 |
| 第 2 周 | Qt 空界面和多宫格布局 | Qt Widgets 项目，1～16 路动态视频网格。 | 必做 |
| 第 3 周 | FFmpeg 拉流并显示一路视频 | 已完成：Camera 01 使用 QImage 显示，并支持可中断退出和自动重连。 | 已完成 |
| 第 4 周 | 动态多路视频显示 | 已实现并通过短窗口功能回归：0～16 路动态连接、网络线程/解码池解耦、指标与验收脚本。 | 10 分钟 Windows 性能资格测试待执行 |
| 第 5 周 | 设备状态、日志、断线重连 | 状态栏、日志、自动重连。 | 推荐 |
| 第 6 周 | 性能优化、OpenGL 渲染、项目整理 | 已完成独立 RGB OpenGL 原型、Windows 实机验证和 ARM64 交叉链接门禁；生产渲染替换等待长测数据。 | 原型完成 |
| 六周后 1～2 周 | Linux ARM64 交叉构建与实机验证 | AArch64 ELF、toolchain、部署脚本和盒子验收记录。 | 工程化必做 |

### 8.2 第 1 周：跑通 RTMP 推流链路

| 项目 | 内容 |
| --- | --- |
| 学习目标 | 理解 RTMP 推流/拉流基本概念，知道 SRS/nginx-rtmp 的作用，学会使用 FFmpeg 命令模拟设备推流。 |
| 开发任务 | 安装 FFmpeg 命令行工具；部署 SRS 或 nginx-rtmp；准备一个测试 MP4；用 FFmpeg 推流；用 ffplay/VLC 拉流播放。 |
| 产出物 | RTMP Server 启动方式；测试推流命令；可播放的 RTMP 地址。 |
| AI 编程提示词 | “请为 Windows 项目生成一个 `scripts/push_test_stream.ps1`，用于调用 ffmpeg 将本地 test.mp4 循环推送到 `rtmp://127.0.0.1/live/camera001`，并在脚本中检查 ffmpeg 是否存在。” |
| 验收标准 | `ffplay rtmp://127.0.0.1/live/camera001` 能看到测试视频；断开推流后播放器停止；重新推流后可再次播放。 |
| 可能遇到的问题 | 防火墙阻止端口；RTMP 地址写错；SRS 未启动；FFmpeg 不在 PATH；输入视频没有 H.264 编码。 |

建议命令：

```bash
ffmpeg -re -stream_loop -1 -i test.mp4 -c:v libx264 -preset veryfast -tune zerolatency -an -f flv rtmp://127.0.0.1/live/camera001
```

本周不要做：

- 不要写内置 RTMP Server。
- 不要急着接 Qt。
- 不要一开始就编译 FFmpeg 源码。

### 8.3 第 2 周：Qt 空界面和多宫格布局

| 项目 | 内容 |
| --- | --- |
| 学习目标 | 熟悉 Qt Widgets、CMake、QMainWindow、QGridLayout、QLabel、自定义 QWidget。 |
| 开发任务 | 创建 Qt Widgets + CMake 项目；实现主窗口；实现可手动添加的 1～16 路动态多宫格；每个格子显示设备名称、连接状态和占位黑屏。 |
| 产出物 | 可运行的 Qt 界面；`VideoWidget`；动态 `VideoGridWidget`；拖拽与全屏交互。 |
| AI 编程提示词 | “请为现有 Qt 6 Widgets 项目实现动态 VideoGridWidget。启动时创建一路，用户可逐个添加到 16 路；布局按数量在 1x1 至 4x4 间自动调整，并保留拖拽交换和全屏功能。” |
| 验收标准 | 程序能启动；默认显示一路；可添加至 16 路且自动布局；调整窗口大小时均匀扩展；没有视频时显示黑屏和状态文字。 |
| 可能遇到的问题 | Qt Kit 选择错误；CMake 找不到 Qt；布局拉伸不正确；控件大小变化导致文字遮挡。 |

早期 2x2 原型示意如下；当前实现已进一步扩展为动态 1～16 路：

```text
+---------------------+---------------------+
| camera001            | camera002            |
| [未连接/黑屏]         | [未连接/黑屏]         |
+---------------------+---------------------+
| camera003            | camera004            |
| [未连接/黑屏]         | [未连接/黑屏]         |
+---------------------+---------------------+
```

本周适合新手先做：

- Qt 空窗口。
- QGridLayout。
- QLabel 显示占位图。
- 简单状态文本。

第二周已进一步完成：

- 拖拽交换实际视频格。
- 1～16 路自动布局和添加动画。
- 单路全屏预览与悬浮控制栏。

### 8.4 第 3 周：FFmpeg 拉流并显示一路视频

当前实现状态：已完成。`FFmpegPlayer` 使用专用 `QThread`，通过 FFmpeg 8.1.2 解码 H.264 并使用 `sws_scale` 转为 `QImage::Format_RGB888`；UI 只消费最新帧。断流后画面清黑并按 1、2、4、5 秒退避重连，关闭时使用 `AVIOInterruptCB` 中断网络读取并等待线程退出。

| 项目 | 内容 |
| --- | --- |
| 学习目标 | 理解 FFmpeg 拉流解码流程，掌握 `AVFormatContext`、`AVCodecContext`、`AVPacket`、`AVFrame`、`sws_scale` 的基本用途。 |
| 开发任务 | 配置 FFmpeg 开发库；封装 `FFmpegPlayer`；打开一路 RTMP URL；解码 H.264；转换为 RGB；通过信号发送 `QImage`；在 `VideoWidget` 显示。 |
| 产出物 | 一路 RTMP 实时播放原型。 |
| AI 编程提示词 | “请在现有 Qt 6 Widgets 项目中封装一个 FFmpegPlayer 类，使用 FFmpeg C API 打开一个 RTMP URL，解码视频流，将 AVFrame 通过 sws_scale 转换为 QImage::Format_RGB888，并通过 Qt signal `frameReady(const QImage&)` 发给 VideoWidget 显示。要求拉流解码运行在 QThread 中，UI 线程只负责显示。” |
| 验收标准 | SRS 中有一路流时，Qt 程序能显示画面；停止推流后不崩溃；关闭窗口时线程和 FFmpeg 资源能正常释放。 |
| 可能遇到的问题 | FFmpeg 头文件和库路径配置错误；DLL 不在运行目录；跨线程更新 UI 导致异常；YUV 到 RGB 颜色异常；退出时线程未停止。 |

核心流程：

```text
open(url)
  -> find video stream
  -> open decoder
  -> read packet loop
  -> send packet
  -> receive frame
  -> convert YUV to RGB
  -> emit frameReady(QImage)
```

本周必须注意：

- 不要在 UI 主线程中 `av_read_frame`。
- 不要跨线程直接调用 QLabel。
- 不要每帧无限堆积，要允许丢弃旧帧。

### 8.5 第 4 周：多路视频显示

当前实现状态：代码、CTest、16 路视频故障隔离和 30 秒双屏实况短验收已完成，
10 分钟 Windows 性能硬门槛仍待执行。
四路独立播放作为历史基线保留；当前普通启动显示空状态页，通过连接对话框动态添加
0～16 路，重复传入 1～16 个 `--url` 可供测试一次预装。名称、URL、播放器、状态、
指标和 `VideoWidget` 通过稳定 `StreamId` 绑定，拖拽不改变业务路由。

每路使用独立网络/解复用 `QThread` 承载阻塞式 `avformat_open_input` 和
`av_read_frame`，只产生压缩包。所有流共享固定 `DecodeWorkerPool`；当前 16 逻辑
线程电脑默认 8 个 worker，同一流固定到同一 worker，FFmpeg 解码器内部线程数为 1。
每轮处理最多 4 包或 5 ms。每路压缩队列限制为 45 包或 4 MiB；溢出时清理积压、
flush 并等待下一关键帧，避免延迟无限增长。

网格只按最高 15 FPS 转换最大 640×360 RGB，全屏最高 30 FPS、最大 1280×720。
UI 定时器轮询每路单帧邮箱，旧帧直接覆盖。关闭时先同时中断所有网络线程和队列，
等待每路解码任务后再关闭共享 worker，不使用 `QThread::terminate()`。

| 项目 | 内容 |
| --- | --- |
| 学习目标 | 理解阻塞网络与 CPU 解码的调度差异、有界背压、流亲和、公平让出和 UI 限帧。 |
| 开发任务 | 已完成 0～16 路动态添加/移除、稳定 ID、独立网络线程、共享解码池、展示目标、无敏感 URL 指标和引导测试脚本。 |
| 产出物 | 动态连接 UI、16 路播放架构、Windows/ARM64 测试目标、预录/双屏/总控脚本，以及 `week4_sixteen_stream_validation.md`。 |
| 验收标准 | CTest 4/4；短窗口 16/16 playing；30 秒实况 16/16 有延迟样本且 P95 169～177 ms；10 分钟解码 FPS、延迟、CPU、内存、队列、UI 卡顿及三种关闭门槛仍需按新文档执行。 |
| 可能遇到的问题 | 软件解码 CPU 饱和；队列反复丢到关键帧；RGB 转换或 UI 成为瓶颈；自动化执行器等待后代进程树。 |

测试推流地址建议：

```text
rtmp://127.0.0.1/live/camera001
...
rtmp://127.0.0.1/live/camera016
```

当前边界：

- 不引入 OpenGL、D3D11VA 或 ARM64 VPU。
- 连接只在当前会话有效，不持久化设备配置。
- Windows 做真实 16 路和双屏延迟；ARM64 本轮只做交叉构建门禁。
- 详细手工操作、报告字段和性能门槛见
  [16 路动态连接、解码架构与性能验收](../weeks/week4/week4_sixteen_stream_validation.md)。

### 8.6 第 5 周：设备状态、日志、断线重连

当前实现状态：已完成。现有播放器状态机已统一为 `DeviceStatus`，补齐
`Disconnected` 和 `Error`；断线后默认每 3 秒重试，最大连续失败次数通过
`--max-reconnect-failures` 配置，`0` 表示无限重试。应用新增异步、有界、URL
脱敏的系统与审计 JSONL 轮转日志，并将底部区域改为只显示大众语言的用户事件
消息；面板支持暂停滚动和清空显示。
详细设计与验收见
[Week 5 设备状态、日志与重连](../weeks/week5/week5_device_status_and_logging.md)。

| 项目 | 内容 |
| --- | --- |
| 学习目标 | 学会把播放器从“能跑”变成“可调试、可恢复、可维护”。 |
| 开发任务 | 增加设备配置；增加状态枚举；实现日志输出；实现断线检测；实现自动重连；显示 FPS、码率或最后收帧时间。 |
| 产出物 | 工程化多路播放版本。 |
| AI 编程提示词 | “请为当前多路 RTMP 播放项目增加设备状态和日志模块。定义 DeviceStatus 枚举，包括 Disconnected、Connecting、Playing、Reconnecting、Error。每一路播放器状态变化时发出信号，MainWindow 更新对应 VideoWidget 的状态文本，并把日志追加到 QTextEdit 日志面板。断线后每 3 秒自动重连，最多连续失败次数可配置。” |
| 验收标准 | 断开推流后对应格子显示断线或重连中；恢复推流后自动播放；日志能看到连接、错误、重连信息；单路异常不影响其他路。 |
| 可能遇到的问题 | FFmpeg 阻塞导致重连不及时；错误码不清晰；日志刷屏；重连线程生命周期混乱。 |

建议状态机：

```text
Disconnected
  -> Connecting
  -> Playing
  -> Error
  -> Reconnecting
  -> Connecting
```

本周重点：

- 可观察性比功能堆叠更重要。
- 日志中必须包含设备名、URL、错误原因、时间。
- 重连逻辑要能停止，避免程序退出后线程仍在跑。

### 8.7 第 6 周：性能优化、OpenGL 渲染、项目整理

当前实现状态：已完成独立 `VideoRenderWidget` RGB/RGBA 纹理原型，保留现有
QPainter/QImage 为默认生产渲染路径。Windows 已实际运行 WGL 与 Qt OpenGL 冒烟
程序，完整 CTest 10/10 通过；WSL2 已补齐 ARM64 GL/EGL/GLES/Qt OpenGL sysroot，
并生成通过 ELF 与动态依赖门禁的 AArch64 EGL/GLES2 和 Qt OpenGL 原型。真实 ARM64
QPA、EGLFS/Wayland/X11 和 GPU 运行仍待目标盒子验收。详细记录见
[Week 6 OpenGL 环境与原型验证](../weeks/week6/week6_opengl_environment_and_validation.md)。

| 项目 | 内容 |
| --- | --- |
| 学习目标 | 理解多路视频性能瓶颈，了解 QLabel/QImage 与 OpenGL 渲染差异，整理工程文档和测试脚本。 |
| 开发任务 | 已有网格 15 FPS/全屏 30 FPS、最新帧邮箱和 FPS 指标保持不变；新增独立 QOpenGLWidget RGB 纹理原型、双平台图形构建门禁、环境与测试脚本。 |
| 产出物 | `VideoRenderWidget` 原型；WGL、EGL/GLES2、Qt OpenGL 冒烟目标；Windows/ARM64 验证脚本；Week 6 环境与验收文档。 |
| AI 编程提示词 | “请分析当前 Qt + FFmpeg 多路视频显示项目的性能瓶颈，并实现一个基础优化版本：每路解码线程可以按原始帧率解码，但 UI 显示限制为最高 25 FPS；新增帧率统计；保留 QLabel/QImage 方案，同时提供一个可选的 QOpenGLWidget VideoRenderWidget 原型用于后续替换。” |
| 验收标准 | Windows OpenGL 目标实际编译运行且完整回归通过；ARM64 目标严格链接 sysroot 并生成 AArch64 ELF；文档明确交叉构建与实机运行边界。 |
| 可能遇到的问题 | OpenGL 上下文只能在特定线程使用；YUV 纹理渲染复杂；硬件解码平台差异大；过早优化导致代码复杂。 |

本周新手可以先做：

- 限制显示帧率。
- 添加 FPS 统计。
- 整理 README 和脚本。

后期优化再做：

- OpenGL YUV 三纹理渲染。
- 统一硬件解码接口，以及 Windows D3D11VA/DXVA2 和 Linux ARM64 厂商后端。
- 零拷贝或低拷贝渲染链路。
- 内置 RTMP Server。

### 8.8 六周原型后的跨平台专项：Linux ARM64

六周方案仍优先完成可工作的 Windows 原型。当前已经完成 Linux ARM64 的 Qt UI 交叉编译基础设施、ARM64 FFmpeg 8.1.2 接入和 AArch64 ELF 检查；仍需替换或复验厂商 sysroot，并在真实盒子完成图形与视频验收。

| 项目 | 内容 |
| --- | --- |
| 学习目标 | 理解目标编译器、sysroot、Qt host tools、ARM64 Qt/FFmpeg 目标库和 QPA 插件的职责。 |
| 开发任务 | 已完成通用 toolchain、Qt sysroot、Debug Preset、交叉构建和 ELF 检查；后续获取盒子厂商 SDK，补齐目标 FFmpeg，部署到盒子并运行 CTest 和 UI/视频验收。 |
| 产出物 | 可复现的 ARM64 工具链说明；AArch64 ELF 程序；部署脚本；实机测试记录。 |
| AI 编程提示词 | “请先阅读项目代码规范和当前 CMake。保持 Windows x86_64 MSVC 构建不变，为 Linux ARM64 增加 `aarch64-linux-gnu-g++` toolchain 和独立 CMake Preset。Qt、FFmpeg 与 sysroot 路径通过环境变量注入。先完成交叉配置、编译和 ELF 架构检查，不接入硬件解码。” |
| 验收标准 | Windows Debug/CTest 继续通过；WSL2 能生成 AArch64 Linux ELF；无 x86_64/Windows 库混入；目标盒子能启动程序并完成动态布局、拖拽、全屏和一路软件解码。 |
| 可能遇到的问题 | 厂商 sysroot 不完整；Qt host/target 工具混淆；glibc 或 libstdc++ 版本不匹配；QPA 插件缺失；OpenGL ES 驱动差异；FFmpeg 配置与设备不一致。 |

本阶段不要直接开始厂商硬件解码。先让同一套软件解码路径在两个平台运行，再把硬件解码放到统一后端接口中逐个平台接入。

---

## 9. MVP 最小可行版本

MVP 的目标是尽快证明“RtmpMonitor 可以接收并显示一路设备视频”。首个 MVP 使用现有 Windows x86_64 + MSVC 环境完成，不要求同时交付 ARM64 版本，但新增代码必须遵循跨平台规范，不能引入阻碍后续 Linux ARM64 构建的 Windows 专用依赖。

### 9.1 MVP 功能范围

| 功能 | 是否包含 | 说明 |
| --- | --- | --- |
| FFmpeg 命令模拟设备推流 | 包含 | 使用本地 MP4 模拟嵌入式设备。 |
| SRS 接收 RTMP | 包含 | 作为外部 RTMP Server。 |
| Qt 程序拉取一路 RTMP | 包含 | 使用 FFmpeg 开发库。 |
| H.264 解码 | 包含 | 通过 FFmpeg 解码。 |
| QLabel/QImage 显示画面 | 包含 | 最简单可运行显示方案。 |
| 4 路显示 | 可选扩展 | MVP 之后立即扩展。 |
| OpenGL 渲染 | 不包含 | 后期优化。 |
| 内置 RTMP Server | 不包含 | 后期高级能力。 |
| 硬件解码 | 不包含 | 后期性能优化。 |
| 录像截图 | 不包含 | 业务增强功能。 |

### 9.2 MVP 验收标准

```text
1. 启动 SRS。
2. 使用 FFmpeg 推送 test.mp4 到 rtmp://127.0.0.1/live/camera001。
3. 使用 ffplay 能播放该地址。
4. 启动 Qt 程序。
5. Qt 程序中一个 VideoWidget 能显示实时画面。
6. 停止推流后程序不崩溃。
7. 关闭 Qt 程序时没有线程残留或明显卡死。
```

### 9.3 MVP 推荐实现顺序

```text
SRS + ffmpeg 推流
  -> ffplay 验证
  -> Qt 空窗口
  -> VideoWidget 显示静态 QImage
  -> FFmpegPlayer 拉 RTMP
  -> QThread 解码
  -> signal 发送 QImage
  -> QLabel 刷新画面
```

---

## 10. 后期增强版本

| 增强方向 | 价值 | 实现建议 | 优先级 |
| --- | --- | --- | --- |
| 内置 RTMP Server | 让设备直接推到 RtmpMonitor，减少外部依赖。 | 先调研两个平台均可维护的成熟库或伴随服务，不建议从零写协议。 | 中后期 |
| OpenGL 渲染 | 降低多路显示时 CPU 压力，提高渲染效率。 | 从 RGB 纹理开始，再做 YUV 纹理。 | 中期 |
| 硬件解码 | 多路高清视频时显著降低 CPU 占用。 | 统一能力探测与软件回退；Windows 研究 D3D11VA/DXVA2，Linux ARM64 按硬件 SDK 选择后端。 | 后期 |
| 多设备认证 | 防止未授权设备推流。 | 可先在 RTMP URL 中使用 token，后期接入鉴权服务。 | 后期 |
| 低延迟优化 | 降低端到端画面延迟。 | 调整编码 GOP、B 帧、FFmpeg buffer、RTMP Server 参数。 | 中后期 |
| 码率/帧率统计 | 便于监控设备和链路质量。 | 统计 packet 字节数、解码帧数、显示帧数。 | 中期 |
| 自动重连 | 提升网络波动下的可用性。 | 状态机 + 定时重连 + 最大失败次数。 | 中期 |
| 录像截图功能 | 方便取证、留档和调试。 | 截图先保存当前 QImage；录像可用 FFmpeg 重新封装。 | 后期 |
| 设备配置界面 | 支持用户维护设备列表。 | JSON 配置 + Qt 表格编辑。 | 中期 |
| 全屏/单路放大 | 提升监控体验。 | 双击 VideoWidget 切换单路预览。 | 中期 |

### 10.1 内置 RTMP Server 的建议

从零实现 RTMP Server 风险很高，因为 RTMP 涉及握手、chunk、message、流控制、FLV tag、时间戳、异常连接处理等细节。建议路线：

```text
第一阶段：外部 SRS/nginx-rtmp
第二阶段：Qt 客户端稳定多路播放
第三阶段：研究是否嵌入成熟 RTMP server 组件
第四阶段：如确实必要，再设计内置服务端模块
```

除非项目明确要求“单个 exe 内完成接收 RTMP 推流”，否则外部 RTMP Server 是更稳妥的工程方案。

---

## 11. 新手学习路线

### 11.1 推荐学习顺序

| 顺序 | 知识点 | 先掌握什么 | 为什么重要 |
| --- | --- | --- | --- |
| 1 | 视频帧 | 一帧图像、分辨率、帧率、像素格式。 | 显示和性能的基础。 |
| 2 | 编码/解码 | 编码是压缩，解码是还原图像。 | 理解 H.264 和 FFmpeg 的作用。 |
| 3 | H.264 | I/P/B 帧、GOP、SPS/PPS 的概念。 | 排查延迟、花屏、首帧问题。 |
| 4 | RTMP | 推流、拉流、RTMP URL、Server。 | 理解设备与监控终端的连接方式。 |
| 5 | FFmpeg 基本命令 | 推流、拉流、转码、查看信息。 | 先用命令行验证链路。 |
| 6 | AVPacket / AVFrame | 压缩包和解码帧的区别。 | FFmpeg 编码时最核心的概念。 |
| 7 | YUV / RGB | YUV 常用于视频，RGB 常用于显示。 | 理解为什么需要 sws_scale。 |
| 8 | Qt 多线程 | QThread、worker、信号槽。 | 避免 UI 卡顿和跨线程错误。 |
| 9 | Qt 绘图 | QLabel、QImage、paintEvent。 | 实现初期视频显示。 |
| 10 | CMake 交叉编译 | 编译主机、目标平台、toolchain、sysroot、目标依赖。 | 从 Windows 可靠生成 Linux ARM64 程序的基础。 |
| 11 | OpenGL 纹理渲染 | RGB 纹理、YUV 纹理、桌面 OpenGL 与 OpenGL ES。 | 多路高性能显示和 ARM64 图形适配的关键。 |

### 11.2 每个知识点的实践任务

| 知识点 | 实践任务 |
| --- | --- |
| 视频帧 | 用 Qt 显示一张本地图片，理解宽、高、像素格式。 |
| 编码/解码 | 用 FFmpeg 把 MP4 转成 H.264，再播放。 |
| H.264 | 用 `ffprobe` 查看视频编码格式、帧率、码率。 |
| RTMP | 用 FFmpeg 推流到 SRS，用 ffplay 拉流。 |
| FFmpeg 命令 | 写一个推流脚本和一个拉流测试脚本。 |
| AVPacket / AVFrame | 在代码中打印 packet 数量和 frame 数量。 |
| YUV / RGB | 将解码帧转换为 QImage 并显示。 |
| Qt 多线程 | 写一个 worker 每秒发送数字到 UI。 |
| Qt 绘图 | 实现 VideoWidget 黑屏、状态文字、画面显示。 |
| CMake 交叉编译 | 用一个不依赖 Qt 的小程序练习 `aarch64-linux-gnu-g++`，通过 `file` 和 `readelf` 确认 AArch64 ELF。 |
| OpenGL | 用 QOpenGLWidget 显示一张纹理图片。 |

### 11.3 不建议新手一开始学习的内容

- 从零实现 RTMP 协议。
- 从零解析 H.264 NALU 并手写解码。
- 编译完整 FFmpeg 源码并裁剪。
- 直接做硬件解码。
- 直接做 16 路 OpenGL YUV 渲染。

---

## 12. 风险点与规避方案

| 风险 | 具体表现 | 影响 | 规避方案 |
| --- | --- | --- | --- |
| 从零写 RTMP Server 风险 | 握手、chunk、时间戳、异常连接处理复杂。 | 项目长期卡住，难以完成 MVP。 | 初期使用 SRS/nginx-rtmp；内置 RTMP Server 放到后期。 |
| 多路视频解码性能风险 | 4 路以上 CPU 飙高，画面卡顿。 | 多路显示不可用。 | 先限制分辨率和帧率；后期使用 OpenGL 和硬件解码。 |
| Qt 主线程卡顿风险 | UI 无响应，窗口拖动卡死。 | 用户体验差，程序像崩溃。 | 拉流解码必须放到 QThread；UI 只显示最后一帧。 |
| FFmpeg 编译和链接风险 | Windows 与 ARM64 头文件、库或运行时版本混用。 | 链接失败，或只在目标设备启动时暴露 ABI 错误。 | 按目标平台隔离依赖；Windows 使用 MSVC 库，Linux 使用 AArch64 `.so`；记录版本和构建选项。 |
| ARM64 工具链风险 | 把 MinGW G++ 当成 ARM Linux 编译器，或工具链与 sysroot 不匹配。 | 生成错误架构产物，或链接到错误 libc。 | 使用 `aarch64-linux-gnu-g++` 或厂商 SDK；通过 `file`、`readelf` 检查 ELF。 |
| Qt 交叉编译风险 | 误用 Windows Qt 库，或缺少 ARM64 Qt 平台插件和 host tools。 | CMake 找不到 Qt、链接失败，或盒子无法创建窗口。 | 分离 host tools 与 target Qt；使用与设备镜像匹配的 ARM64 Qt；实机验证正式 QPA 插件。 |
| 模拟验证边界风险 | QEMU/容器通过后就认为 ARM64 图形和硬件能力可用。 | 发布后出现 EGLFS、OpenGL ES、VPU 或全屏问题。 | 模拟器只跑纯逻辑测试；GUI、渲染、解码和性能必须在真实盒子验证。 |
| 音视频延迟问题 | 画面比实时慢数秒。 | 监控价值下降。 | 设备端关闭 B 帧、降低 GOP；FFmpeg 使用低延迟参数；减少缓存。 |
| 设备断线重连问题 | 推流中断后无法恢复，线程卡死。 | 现场使用不稳定。 | 实现状态机、超时、自动重连和可停止线程。 |
| QImage 拷贝风险 | 每帧复制导致内存和 CPU 压力。 | 多路时性能下降。 | 限制显示帧率；复用 buffer；后期改 OpenGL YUV。 |
| 时间戳和首帧风险 | 首帧慢、画面花屏、短暂黑屏。 | 用户误以为连接失败。 | 状态显示“等待关键帧”；设备端稳定发送 SPS/PPS 和 IDR。 |
| 双平台部署风险 | Windows 缺 DLL，Linux 缺 `.so`、动态加载器或 QPA 插件。 | 开发机构建成功，目标机器无法启动。 | Windows 使用 windeployqt；Linux 检查 `ldd`、Qt plugin path、rpath 和部署清单。 |
| 网络环境风险 | 防火墙、端口、IP 地址变化。 | 设备无法推流到 PC。 | 固定端口；配置防火墙规则；提供网络排查文档。 |

### 12.1 重点风险说明

#### 12.1.1 从零写 RTMP Server

这是本项目最大的不必要风险。RTMP Server 看起来只是“收一个流”，但工程细节很多。对于本项目来说，真正核心价值是“Windows x86_64 与 Linux ARM64 上稳定的多路视频显示”，不是重新实现一个流媒体服务器。

推荐策略：

```text
先把外部 RTMP Server 当成基础设施
  -> 完成 Qt 多路显示主功能
  -> 形成稳定播放器架构
  -> 再决定是否内置接收能力
```

#### 12.1.2 多路解码性能

如果每路都是 1080p 30 FPS，4 路软件解码就可能对 CPU 造成明显压力。初期测试可以先使用：

- 720p 或更低分辨率。
- 15 FPS 或 25 FPS。
- 码率 1 Mbps 到 2 Mbps。
- H.264 baseline/main profile。

#### 12.1.3 Qt 主线程卡顿

所有耗时操作都不要放在 UI 线程：

- `avformat_open_input`
- `av_read_frame`
- `avcodec_send_packet`
- `avcodec_receive_frame`
- `sws_scale`
- 重连等待

UI 线程只接收已经准备好的图像，并尽快刷新。

#### 12.1.4 交叉编译成功不等于 ARM64 运行通过

交叉编译只验证源码、头文件和链接依赖在目标工具链下基本成立。它无法替代以下设备能力：

- 目标镜像中的 glibc、libstdc++ 和动态加载器版本。
- Wayland、X11 或 EGLFS 平台插件及其系统依赖。
- OpenGL ES、GPU/VPU 驱动和硬件帧互操作。
- 设备温度、内存带宽、长期运行和断线重连稳定性。

推荐把 ARM64 验证拆成“交叉编译门禁、QEMU 纯逻辑测试、真实盒子验收”三层。任何一层都不能冒充另一层的结果，发布结论必须注明测试硬件型号、系统镜像、Qt/FFmpeg 版本和实际 QPA 插件。

---

## 13. 推荐 AI 编程工作流

后续使用 Codex 生成代码时，建议每次只让 AI 做一个明确任务，并要求它先阅读当前项目结构。

### 13.1 分阶段提示词

| 阶段 | 目标 | 推荐提示词 |
| --- | --- | --- |
| 1 | 生成 Qt 空项目 | “请检查当前仓库结构，然后创建一个 Qt 6 Widgets + CMake 的最小可运行项目。当前先使用 MSVC 和 C++17，同时避免在业务代码中使用 Win32 专用 API，为后续 Linux ARM64 构建保留平台边界。包含 MainWindow，启动后显示空窗口，不接入 FFmpeg。” |
| 2 | 生成 VideoWidget | “请实现一个 VideoWidget 类，继承 QWidget 或 QFrame，内部使用 QLabel 显示画面。它需要支持 `setTitle(QString)`、`setStatus(QString)`、`setFrame(QImage)`，无画面时显示黑色背景和状态文字。” |
| 3 | 生成多宫格布局 | “请实现 VideoGridWidget，启动时创建一个 VideoWidget，并允许用户逐个添加到 16 路。根据数量自动计算 1x1 至 4x4 布局，维护稳定逻辑顺序。” |
| 4 | 接入 Windows FFmpeg 库 | “请先为 Windows x86_64 MSVC 构建接入 FFmpeg，依赖目录必须带平台名称并通过 CMake 变量指定。链接 avformat、avcodec、avutil、swscale，说明 DLL 部署；不要把 Windows 路径写入播放器业务类。” |
| 5 | 实现单路 FFmpegPlayer | “请封装 FFmpegPlayer，运行在 QThread 中，打开一个 RTMP URL，解码视频帧，转换为 QImage，并通过 `frameReady(QImage)` 信号发送给 UI。要求提供 start、stop、errorOccurred、statusChanged。” |
| 6 | 接入一路显示 | “请把 FFmpegPlayer 接入 MainWindow 的第一个 VideoWidget。程序启动后拉取 `rtmp://127.0.0.1/live/camera001` 并显示画面。关闭窗口时安全停止线程。” |
| 7 | 扩展 4 路视频 | “请将单路播放扩展为首批 4 路。每一路有独立播放器和线程，分别绑定到动态 VideoGridWidget 的前 4 个 VideoWidget；保留 UI 添加至 16 路的能力，单路失败不能影响其他路。” |
| 8 | 添加日志和状态管理 | “请增加 LogManager 和设备状态显示。每路播放器连接、播放、错误、重连时都输出日志，并更新对应 VideoWidget 状态。” |
| 9 | 添加断线重连 | “请为 FFmpegPlayer 增加断线自动重连。连接失败后等待 3 秒重试，停止播放时必须能取消重连。请避免 UI 线程阻塞。” |
| 10 | 添加配置文件 | “请增加 JSON 配置文件读取，配置设备名称和 RTMP URL。程序启动时从 `configs/devices.json` 加载最多 4 个设备并显示。” |
| 11 | 性能优化 | “请为多路显示增加 FPS 统计和显示帧率限制。解码线程可以持续解码，但 UI 刷新每路最高 25 FPS，避免 QImage 队列堆积。” |
| 12 | OpenGL 原型 | “请新增一个可选的 QOpenGLWidget 视频渲染控件，先支持上传 RGB QImage 为纹理显示。保留原 QLabel 方案，不要一次替换全部逻辑。” |
| 13 | ARM64 交叉构建 | “请阅读代码规范和当前 CMake，保持 Windows MSVC 构建可用，为 Linux ARM64 增加 `aarch64-linux-gnu-g++` toolchain 和独立 Preset。sysroot、ARM64 Qt 与 FFmpeg 路径通过环境变量注入；构建后用 file/readelf 验证 AArch64 ELF。” |
| 14 | ARM64 实机部署 | “请根据目标盒子的系统镜像生成部署清单和脚本，检查 Qt 平台插件、FFmpeg `.so`、动态库路径和软件解码。先验证 UI、拖拽、全屏和一路 RTMP，不接入厂商硬件解码。” |

### 13.2 使用 Codex 时的建议

- 每次提示词都说明“请先检查当前仓库结构”。
- 每次只改一个模块，避免一次生成大量互相耦合的代码。
- 让 Codex 写完后运行当前目标的 CMake 配置、构建和 CTest；涉及 ARM64 时还要检查 ELF 架构，不能只看命令退出码。
- 明确告诉 Codex 当前目标是 Windows x86_64 还是 Linux ARM64，禁止自动混用 Qt Kit、FFmpeg 库和构建目录。
- 每完成一阶段提交一次 Git。
- 让 Codex 同步更新 README 和排查文档。
- 出现崩溃时，优先让 Codex 分析日志和线程退出逻辑。

### 13.3 不推荐的提示方式

不建议直接说：

```text
请帮我完整实现一个多路 RTMP 视频监控系统。
```

这个任务太大，AI 很容易一次生成过多代码，导致 FFmpeg、Qt 线程、CMake、UI、配置和日志混在一起，后续难以调试。

更好的方式是：

```text
请只实现 VideoWidget，不接入 FFmpeg。
请只实现 FFmpegPlayer 的一路拉流，不做多路。
请只把单路扩展到 4 路，不做 OpenGL。
```

---

## 14. 最终交付物清单

| 交付物 | 说明 | 是否 MVP 必需 |
| --- | --- | --- |
| 可运行程序 | 同一源码生成 Windows x86_64 与 Linux ARM64 多路视频显示程序；MVP 先交付 Windows 版本。 | Windows MVP 必需，ARM64 工程版必需 |
| CMake 项目 | 支持 MSVC 原生构建，并在跨平台阶段增加 Linux ARM64 toolchain 和独立 Preset。 | 是 |
| README.md | 项目介绍、构建方式、运行方式。 | 是 |
| docs/roadmap/project_plan.md | 当前项目规划文档。 | 是 |
| 环境配置文档 | 说明 Windows MSVC、WSL2 ARM64 工具链、Qt、FFmpeg、sysroot 和 SRS 配置。 | 推荐 |
| 测试推流脚本 | 一键使用 FFmpeg 推送测试视频到 RTMP Server。 | 是 |
| 示例 RTMP 地址 | 如 `rtmp://127.0.0.1/live/camera001`。 | 是 |
| 问题排查文档 | 记录黑屏、断流、DLL/`.so` 缺失、QPA 插件、ABI 和延迟问题。 | 推荐 |
| SRS/nginx-rtmp 配置 | 外部 RTMP Server 的启动和配置方式。 | 是 |
| 设备配置示例 | JSON/INI 形式保存设备名和 RTMP URL。 | 推荐 |
| 日志文件 | 程序运行日志，便于排查现场问题。 | 推荐 |
| 性能测试记录 | 多路播放时 CPU、内存、帧率、延迟记录。 | 后期 |
| ARM64 toolchain 与 Preset | 已提供可复现的 Linux ARM64 Debug 交叉配置；厂商 SDK 仍可覆盖 sysroot。 | 基础版已完成 |
| ARM64 实机测试记录 | 硬件型号、镜像、QPA、Qt/FFmpeg、CTest、UI、视频和性能结果。 | ARM64 发布必需 |
| 打包部署说明 | Windows 的 windeployqt/FFmpeg DLL，以及 Linux 的 `.so`、QPA plugin、rpath 和配置复制说明。 | 后期 |

### 14.1 推荐阶段性交付

```text
MVP 交付：
  - Qt 程序能显示一路 RTMP 视频
  - SRS 启动说明
  - FFmpeg 推流脚本
  - README 基础运行说明

原型版交付：
  - 4 路多宫格显示
  - 基础状态显示
  - 基础日志
  - 设备 RTMP URL 配置

工程版交付：
  - 自动重连
  - 错误处理
  - 性能统计
  - 环境配置文档
  - 问题排查文档
  - Linux ARM64 交叉工具链和 AArch64 ELF
  - ARM64 硬件盒子实机验收记录

增强版交付：
  - OpenGL 渲染
  - 硬件解码
  - 录像截图
  - 内置 RTMP Server 可行性方案或实现
```

---

## 结论

本项目最稳妥的实现路径是：先把 RTMP Server 当成外部基础设施，使用 SRS 或 nginx-rtmp 跑通推流链路；再用 Windows x86_64 + MSVC 完成 Qt + FFmpeg 一路视频播放 MVP；随后扩展到 4 路真实播放，并保持业务和 UI 代码跨平台；功能主链路稳定后，在 WSL2 中使用 AArch64 G++、目标 sysroot、ARM64 Qt 和 FFmpeg 建立 Linux ARM64 交叉构建；最后在真实硬件盒子上验证 QPA、OpenGL ES、软件解码、全屏和长期稳定性，再按设备能力接入硬件解码。

因此，应该增加 G++，但准确说是增加 `aarch64-linux-gnu-g++` 或硬件厂商 SDK 中的 ARM64 Linux 交叉编译器，而不是用普通 MinGW G++ 替代 MSVC。Windows 侧负责两个独立流程：MSVC 构建 Windows 程序，WSL2 交叉生成 ARM64 Linux ELF。交叉生成、模拟执行和真实盒子验收是三个不同层级，只有三层结果都记录清楚，才能对 Linux ARM64 支持作出可靠结论。

对新手来说，第一阶段的核心不是理解所有音视频底层细节，而是建立一条能跑通、能观察、能逐步扩展的工程链路。只要先把“推流 -> 收流 -> 解码 -> 显示”跑通，后续每个优化点都可以拆成小任务，逐步交给 Codex 辅助实现。
