# RtmpMonitor

<p align="center">
  <img src="resources/icons/rtmp-monitor-128.png" width="96" height="96" alt="RtmpMonitor 图标">
</p>

RtmpMonitor 是一个使用 C++17、Qt 6 Widgets、FFmpeg 和 Eclipse Paho MQTT C 开发的多路 H.264/RTMP 监控客户端。它支持保存常用推流、动态添加、重连和移除最多 16 路视频，并通过一个全局 MQTT 目标控制单台小车启停推流、前后左右和停车。桌面产品标题为“RtmpMonitor 监控台”。

当前版本为 `0.1.0-alpha.1`：Windows 是 **Development Preview**，Linux ARM64 是 **Engineering Preview**。Windows 构建与自动测试、Ubuntu 22.04 到 ARM64 的交叉构建已经验证；ARM 真机的 QPA、GPU、视频和长期稳定性仍需在目标设备验收。

## 当前界面

- 全局采用“深石墨监控舱”主题，Fusion 基础样式、深色 Palette 与限定作用域 QSS 保持统一；Windows 使用原生 DWM 深色标题栏。
- 主工具栏、空连接状态、视频边界、设备控制、日志和对话框共享同一语义色板；应用图标提供 SVG、PNG 和 Windows ICO 源。
- 设备控制是高优先级侧边操作区，停靠在左侧或右侧时始终保持全高；事件消息只占中央视频列的上方或下方，不会截断摇杆和停车按钮。
- 设备控制使用可滚动容器和约 40 px 的主要触控目标，1280×720 下内容保持可达；Android 当前仅作为触控和图标安全区设计约束，尚未提供 Android 工程。

主题选择器、外部 QSS 优先级、图标和 Windows 外观实现见[样式加载与主题扩展指南](docs/guides/development/style_loading.md)。

## MQTT 联调状态

截至 2026-08-14，已通过 EMQX 后台和 MQTTX 完成当前 MQTT Broker 的连接、订阅、发布与消息观察联调；MQTTX 观察到的七类控制指令及消息返回符合当前 Topic 和 JSON 协议约定，PC 端 MQTT 通信链路工作正常。

该结论覆盖桌面客户端、Broker 和 MQTTX 消息链路，不替代小车实际动作验收。当前仍采用 QoS 0，PC 收到同 Topic 消息表示 Broker 已完成转发，不等于设备执行回执；车辆运动测试仍需在架空或清场条件下单独确认。

首次启动时 MQTT 默认关闭且 Broker 为空，不会执行 DNS、TCP 连接、MQTT CONNECT 或自动订阅。真实 Broker 只能由用户在“MQTT 设置”中输入，并保存到本机应用配置目录；仓库、示例和发布包不提供任何用户或现场公网端点默认值。已有合法本机配置保持兼容；启用 MQTT 或点击“测试连接”时必须填写合法的 `mqtt://主机[:端口]`。

## 功能

- RTMP/FLV 可选 AAC-LC 音频下行：48 kHz 单声道输出、全应用唯一活动音频源、默认静音、视频格/全屏状态同步和 `M` 静音快捷键；视频无音频或输出设备异常不影响画面。
- 动态 0～16 路 RTMP 连接，以及单路重连、移除和故障隔离。
- 共享解码池和容量为 1 的最新帧邮箱，避免慢 UI 形成无界队列。
- OpenGL YUV 合成，初始化失败时自动回退到 CPU/QPainter。
- 拖拽换位、单路全屏、F11 监控墙和异步截图。
- 深石墨统一界面、Windows 原生深色标题栏和侧边操作 Dock 优先布局。
- schema v1 保存推流列表，按条目选择启动时自动接入。
- 单目标 MQTT 桌面控制台；支持固定中心鼠标摇杆，以及显式解锁的 WASD/方向键控制，松开、失焦、
  隐藏或全屏切换时停车，并可折叠观察同一 Topic 最近 20 条消息。
- 结构化系统日志、审计日志、敏感字段脱敏和运行指标。
- Windows x64 原生开发与 Ubuntu 22.04 → Linux ARM64 交叉开发入口。

## 支持范围

| 目标 | 开发环境 | Qt | FFmpeg | 状态 |
|---|---|---|---|---|
| Windows x64 | Windows 11、Visual Studio 2022/MSVC、CMake、Ninja | 6.6.1 MSVC x64 | FFmpeg 8.1.2、Paho MQTT C 1.3.16，vcpkg `x64-windows` | 自动构建与 CTest 已验证 |
| Linux ARM64 | Ubuntu 22.04 或 WSL2 Ubuntu 22.04、AArch64 GCC 11 | 6.2.4 Jammy ARM64 | FFmpeg 8.1.2、Paho MQTT C 1.3.16 | RASTER/GLES3 交叉构建与 ELF 依赖已验证 |

项目目前不声明支持 Linux x86_64 原生桌面。交叉构建不能代替 ARM 真机上的 linuxfb/EGLFS、GPU/VPU、输入设备、网络和持续播放测试。

## 获取源码

```bash
git clone --branch v0.1.0-alpha.1 https://github.com/FKstack/rtmpProject.git
cd rtmpProject
git rev-parse HEAD
```

源码仓库不提交第三方 `.lib`、`.dll` 或 `.so`。开发依赖由固定版本的脚本准备，个人路径保存在已忽略的 `CMakeUserPresets.json`。

## Windows 开发

先安装以下软件：

- Visual Studio 2022，并选择“使用 C++ 的桌面开发”。
- Qt 6.6.1 MSVC x64。
- Git。CMake 和 Ninja 可以使用 Visual Studio/Qt 提供的版本。

脚本不会自动安装 Visual Studio 或 Qt。首次配置时传入 Qt 目录和一个可写的工具目录；工具目录用于固定版本的 vcpkg、下载和二进制缓存：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\setup_windows_dev.ps1 `
  -Action All -Configuration Debug `
  -QtRoot "<Qt-msvc-root>" `
  -ToolRoot "<tool-root>"
```

`All` 会依次检查 MSVC/Qt、准备固定提交的 vcpkg 与 FFmpeg 8.1.2、生成本机 Preset、配置、构建并运行 CTest。它默认不修改用户级 `PATH`，也不会覆盖已有的 `CMakeUserPresets.json`。

日常开发可以使用生成的 Preset：

```powershell
cmake --preset Qt-Debug --fresh
cmake --build out\build-windows-x64\debug --parallel
ctest --test-dir out\build-windows-x64\debug --output-on-failure
.\out\build-windows-x64\debug\rtmp_monitor.exe
```

也可以在 Visual Studio 中打开仓库，选择 `Qt-Debug` 或 `Qt-Release`，将 `rtmp_monitor.exe` 设为启动项后按 F5。详细参数和故障处理见[跨平台构建指南](docs/guides/build-and-testing/cross_platform_build.md)。

## Linux ARM64 交叉开发

当前正式基线是 Ubuntu 22.04/WSL2 Ubuntu 22.04 主机。先检查环境：

```bash
bash scripts/setup_linux_arm64_dev.sh --action check
```

只有缺少交叉工具链、sysroot、Qt 或 FFmpeg 时才执行安装；`install` 需要 root：

```bash
sudo bash scripts/setup_linux_arm64_dev.sh --action install
```

构建并验证 RASTER 和 GLES3 两种模式：

```bash
bash scripts/setup_linux_arm64_dev.sh --action all --render-mode both
```

脚本会检查 AArch64 ELF 和动态依赖，并通过 QEMU 运行可靠的纯逻辑测试。可以用 `--work-root`、`--sysroot`、`--build-root` 和 `--proxy-url` 覆盖默认位置；代理没有本机默认值。真实设备部署和资格测试见[嵌入式开发交接](docs/guides/build-and-testing/embedded_developer_handoff.md)。

## SRS 测试服务

本项目使用独立 SRS 服务，不在客户端内实现 RTMP Server。Windows 侧脚本需要显式指定 WSL 发行版：

```powershell
wsl.exe --list --quiet

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\srs\srs_dev_wsl.ps1 `
  -Action Check -Distro "<WSL-distro>"
```

也可以先设置 `RTMP_MONITOR_WSL_DISTRO`，随后省略 `-Distro`。SRS 的首次安装、启停、推拉流和排障步骤见[SRS 新手指南](docs/guides/build-and-testing/srs_beginner_guide.md)。

受控音频资格测试使用中国境内阿里云官方播放器样例。素材只下载到被忽略的 `out/qualification/`，不会提交仓库；脚本按 `MP4 → FFmpeg H.264/AAC → WSL2 SRS → 正式 AudioPlaybackEngine → QAudioSink` 运行，并生成脱敏 JSON：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\audio\qualify_mp4_audio.ps1 `
  -Action RunThree `
  -Distro "<WSL-distro>" `
  -BuildDir .\out\build-windows-x64\release `
  -Ffmpeg "<ffmpeg.exe-path>"
```

2026-08-15 的三轮 320 秒资格测试和一轮 600 秒连续测试全部通过。600 秒结果为 647 个有效样本，P50 89.561 ms、P95 111.632 ms、最大 125.396 ms、QAudioSink 欠载 0 次。该口径从 FFmpeg 发布进度到客户端写入 QAudioSink，覆盖推流、SRS、网络、解复用、解码与软件缓冲；不包含声卡 DAC、扬声器和空气传播，声学端到端仍待具备回环条件后验证。

应用可不带地址启动，也可传入一个或多个测试流：

```powershell
.\out\build-windows-x64\debug\rtmp_monitor.exe `
  --renderer auto `
  --url rtmp://127.0.0.1:1935/live/camera001
```

不要把包含用户名、密码、Token 或签名参数的真实地址提交到仓库。

## 脚本

| 脚本 | 定位 | 作用 |
|---|---|---|
| `scripts/setup_windows_dev.ps1` | 公共入口 | Windows `Check/Setup/Configure/Build/Test/All/SelfTest` |
| `scripts/setup_linux_arm64_dev.sh` | 公共入口 | ARM64 `check/install/build/test/all/self-test` |
| `scripts/package_windows.ps1` | 公共入口 | 从已验证的 Release 构建生成 Windows 测试包 |
| `scripts/qualify_embedded_device.sh` | 公共入口 | 在真实 ARM 目标板执行分级资格测试 |
| `scripts/srs/srs_dev_wsl.ps1` | 公共入口 | 管理脚本自己启动的 WSL2 SRS 开发实例 |
| `scripts/srs/verify_srs_chain.ps1` | 公共入口 | 验证 Windows/WSL2 SRS 推拉流链路 |
| `scripts/srs/build_srs_arm64.sh` | 公共入口 | 在 ARM64 环境构建 SRS |
| `scripts/srs/verify_srs_chain.sh` | 公共入口 | 在 Linux/ARM64 验证 SRS 链路 |
| `scripts/audio/qualify_mp4_audio.ps1` | 公共入口 | 使用境内正常 MP4 执行 SRS 音频延迟、稳定性和静音资格测试 |
| `scripts/audio/publish_av_reference.sh` | 公共入口 | 使用 V4L2/ALSA 发布 H.264+AAC 参考流 |
| `scripts/audio/analyze_latency_report.py` | 公共入口 | 生成脱敏音频延迟报告并执行 P50/P95/最大值门禁 |
| `scripts/setup_ffmpeg_windows_dev.ps1` | 内部辅助 | 固定 vcpkg 与 Windows FFmpeg；通常由 Windows 入口调用 |
| `scripts/setup_arm64_build_env.sh` | 内部辅助 | 准备 Ubuntu 22.04 ARM64 sysroot/Qt/FFmpeg |
| `scripts/verify_ffmpeg_arm64_env.sh` | 内部辅助 | 验证 ARM64 FFmpeg ABI 和 smoke target |
| `scripts/ffmpeg_smoke/` | 内部辅助 | FFmpeg 环境的最小 C/CMake 验证工程 |

历史上只适配维护者电脑的性能和测试编排脚本已经退役，原因和 Git 查看方式见[遗留脚本索引](docs/archive/legacy_test_scripts.md)。

## 目录结构

```text
cmake/      CMake 模块与 ARM64 toolchain
deploy/     SRS 配置、systemd unit 和部署资料
docs/       架构、构建指南、路线图与历史记录
include/    公共 C++ 头文件
resources/  Qt 资源、样式和默认配置
scripts/    可移植开发、打包、SRS 与设备资格入口
src/        应用、播放、渲染、平台和日志实现
tests/      CTest 使用的 C++ 自动测试
```

## 测试与文档

`BUILD_TESTING=ON` 时当前 Windows Debug 配置会注册 29 个 CTest 目标，覆盖保存推流、MQTT 协议与 Fake Broker、桌面摇杆与键盘输入、Dock 布局优先级、依赖方向、播放器生命周期、AAC 解码/默认音频输出、音频延迟报告门禁、动态网格、多路管理、日志、OpenGL framebuffer、渲染核心和应用命令行。2026-08-15 最近一次完整 Windows Debug 为 29/29 通过，最终 Windows Release 为 29/29 通过（98.14 秒）；ARM64 RASTER/GLES3 全目标交叉构建及依赖门禁也已通过，RASTER 未引入 OpenGL/EGL/GLES。真实 SRS 软件链路的发布端到 Sink 写入延迟门禁已通过，声学硬件部分仍需回环条件，不能把软件测量表述为扬声器实际出声延迟。不同平台和构建选项注册数量可能不同；环境脚本的 `All` 会自动运行对应平台门禁，单独重跑可使用：

```powershell
ctest --test-dir out\build-windows-x64\debug --output-on-failure
```

更多资料：

- [文档索引](docs/README.md)
- [保存推流与单车 MQTT 控制指南](docs/architecture/saved_stream_and_mqtt_device_control.md)
- [跨平台构建指南](docs/guides/build-and-testing/cross_platform_build.md)
- [SRS 新手指南](docs/guides/build-and-testing/srs_beginner_guide.md)
- [视频渲染架构](docs/architecture/video_rendering_framework.md)
- [低延迟单向音频框架与 MP4 手工测试指南](docs/architecture/low_latency_audio_stream.md)
- [项目路线图](docs/roadmap/project_plan.md)
- [已知问题](docs/memory/known_issues.md)

## License

项目许可证见 [LICENSE](LICENSE)，第三方组件和许可证说明见 [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES)。
