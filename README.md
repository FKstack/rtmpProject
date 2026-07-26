# RtmpMonitor

RtmpMonitor 是一个面向 Windows x86_64 PC 与 Linux ARM64 嵌入式硬件盒子的多路摄像头监控客户端。目标链路是：嵌入式设备主动推送 H.264/RTMP，客户端使用 FFmpeg 拉流解码，并通过 Qt Widgets 在一个窗口中显示多路实时画面。

项目采用渐进式实现：先用 nginx-rtmp 或 SRS 验证推流链路，再完成可维护的多路 UI 框架，随后接入一路 FFmpeg 播放并扩展多路解码。完整路线见 [项目规划](docs/project_plan.md)。

## 当前状态

| 阶段 | 状态 | 已完成内容 |
|---|:---:|---|
| 第 1 周：RTMP 链路验证 | 已完成 | nginx-rtmp、FFmpeg、ffplay 验证脚本和排查文档 |
| 第 2 周：Qt 多路 UI | 已完成 | 1～16 路动态网格、添加动画、拖拽交换、单路全屏 |
| QSS 与工程规范 | 已完成 | 外部 QSS 优先、QRC 回退、代码和注释规范 |
| 第 3 周：一路 FFmpeg 播放 | 已完成 | Camera 01 拉流、H.264 解码、RGB888 显示、断线重连 |
| 第 3 周：桌面实况延迟基线 | 已完成 | 本地回环中位数 117.5 ms，P95 156 ms |
| 第 4 周：首批四路播放 | 已完成 | Camera 01～04 独立拉流、故障隔离、批量启停和四路实况验收 |

当前版本启动时确定性创建 `Camera 01`～`Camera 04`，分别绑定
`camera001`～`camera004`。每路拥有独立的 `FFmpegPlayer`、解码线程、重连状态和
最新帧邮箱；后续仍可动态添加 `Camera 05`～`Camera 16`，这些新增格本阶段只作为
UI 占位。

当前 Windows 本地回环测试已经覆盖“桌面采集 → H.264 编码 → nginx-rtmp →
FFmpegPlayer 解码 → Qt 绘制”的完整链路。10 个有效样本的典型延迟约为
0.12 秒，详细测试条件、误差边界和复现步骤见
[桌面实况端到端延迟测试](docs/week3_desktop_latency_test.md)。

## 已实现功能

- 程序启动时无动画创建 `Camera 01`～`Camera 04` 四个真实视频窗口。
- Camera 01～04 分别使用独立播放器和专用 `QThread` 拉取首批四路 RTMP。
- 单路连接、解码或重连失败只更新对应视频格，不影响其他三路。
- 点击顶部“添加视频窗口”，可从 `Camera 05` 逐个创建到 `Camera 16`。
- 根据数量自动使用 1x1、1x2、2x2、2x3、3x3、3x4 或 4x4 布局。
- 拖拽任意两个视频格，交换实际 `VideoWidget` 对象和逻辑顺序。
- 添加和交换使用快照动画，不直接动画 `QGridLayout` 管理的真实控件。
- 双击视频格进入单路全屏，支持双击、`Esc` 和控制栏按钮退出。
- 全屏底部提供自动隐藏的 Overlay 控制栏，静音和截图暂为接口占位。
- 添加、拖拽和全屏通过统一状态互斥，避免动画重入。
- 使用 `StyleLoader` 统一加载外部或 QRC 内置 QSS。
- `MultiStreamPlaybackManager` 负责四路播放器的所有权、稳定索引路由和两阶段批量停止。
- 每个 `FFmpegPlayer` 在自己的专用 `QThread` 中完成 RTMP 拉流、解封装、H.264 软件解码和 RGB888 转换。
- UI 线程只绘制最新一帧；旧帧会被覆盖，避免网络流较快时 Qt 事件队列持续增长。
- 断流后立即清黑画面，并按 1、2、4、5 秒退避自动重连；恢复推流后继续显示。
- 关闭程序时先同时请求四路停止，再逐路等待线程退出；FFmpeg 网络模块按进程生命周期统一初始化和释放。

## 动态布局

| 视频窗口数量 | 布局 |
|---:|:---:|
| 1 | 1x1 |
| 2 | 1x2 |
| 3～4 | 2x2 |
| 5～6 | 2x3 |
| 7～9 | 3x3 |
| 10～12 | 3x4 |
| 13～16 | 4x4 |

达到 16 路后，添加动作会禁用，并通过工具提示和状态栏说明数量上限。本阶段尚未实现删除视频窗口。

## 界面操作

| 操作 | 结果 |
|---|---|
| 点击“添加视频窗口” | 创建一路新视频格并自动重排 |
| 按住一个视频格并拖到另一个视频格 | 交换两个真实视频格的位置 |
| 双击普通视频格 | 进入该路全屏预览 |
| 全屏时双击或按 `Esc` | 退出全屏并恢复原网格位置 |
| 点击全屏控制栏“退出全屏” | 退出全屏 |
| 全屏时移动到屏幕底部 | 显示悬浮控制栏 |

## 技术栈

- C++17
- Qt 6 Widgets
- CMake 3.21+
- Windows x86_64：MSVC / Visual Studio 2022
- Linux ARM64：AArch64 GCC/Clang 交叉工具链
- Qt Test / CTest
- FFmpeg 8.1.2 命令行工具：用于 RTMP 链路验证
- FFmpeg 8.1.2 LGPL 动态开发库：Windows x64 与 Linux ARM64 环境及首批四路播放器均已接入
- nginx-rtmp 或 SRS：外部 RTMP Server

## 项目结构

```text
rtmpProject/
├── CMakeLists.txt
├── CMakePresets.json
├── cmake/
│   ├── ProjectOptions.cmake
│   └── toolchains/aarch64-linux.cmake
├── README.md
├── docs/
│   ├── project_plan.md
│   ├── cross_platform_build.md
│   ├── rtmp_chain_verification.md
│   ├── week3_ffmpeg_player.md
│   ├── week3_desktop_latency_test.md
│   ├── week4_multi_stream_playback.md
│   ├── week2_ui_layout.md
│   ├── week2_dynamic_grid.md
│   ├── week2_drag_and_fullscreen.md
│   ├── style_loading.md
│   ├── code_style_guide.md
│   └── comment_style_guide.md
├── include/
│   └── common/                      # 两个目标平台共享的接口
│       ├── app/                     # 应用级服务接口
│       ├── core/                    # 通用基础设施
│       ├── media/                   # FFmpegPlayer 与多路管理器接口
│       └── ui/                      # Qt Widgets 接口
├── resources/
│   ├── styles/app.qss
│   └── styles.qrc
├── scripts/
│   ├── setup_ffmpeg_windows_dev.ps1
│   ├── setup_arm64_build_env.sh
│   ├── verify_ffmpeg_arm64_env.sh
│   ├── ffmpeg_smoke/
│   ├── test_desktop_latency.ps1
│   ├── test_week4_multi_stream.ps1
│   └── verify_rtmp_chain.ps1
├── src/
│   ├── main.cpp                     # 跨平台程序入口
│   ├── common/                      # 两个平台共享的实现
│   │   ├── app/
│   │   ├── media/
│   │   └── ui/
│   └── platform/                    # 有真实需求时加入平台实现
│       ├── windows/
│       └── linux/
└── tests/
    ├── FFmpegPlayerLifecycleTest.cpp
    ├── MultiStreamPlaybackManagerTest.cpp
    ├── VideoGridSmokeTest.cpp
    └── VideoGridDynamicTest.cpp
```

## 环境要求

### Windows x86_64

- Windows 10/11 x64。
- Visual Studio 2022，并安装“使用 C++ 的桌面开发”工作负载。
- CMake 3.21 或更高版本。
- Qt 6 MSVC x64 Kit。当前验证环境为 Qt 6.6.1 `msvc2019_64`。
- FFmpeg 8.1.2 LGPL 动态开发库由 vcpkg 安装到 `F:\DevTools\vcpkg\installed\x64-windows`。

工程会拒绝 MinGW 编译器或 MinGW Qt Kit，不能把 MinGW Qt 库与 MSVC 混用。

首次准备 Windows FFmpeg 开发库时，在 PowerShell 中执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
    -File scripts/setup_ffmpeg_windows_dev.ps1
```

脚本把 vcpkg、下载和二进制缓存放在 F 盘，设置用户级 `VCPKG_ROOT`、
`VCPKG_DOWNLOADS`、`VCPKG_DEFAULT_BINARY_CACHE` 和 PATH。FFmpeg DLL 不永久
加入 PATH；CMake 会按 Debug/Release 把四个运行库复制到应用和播放器测试目录。

### Linux ARM64

- 在 WSL2 或 Linux 主机中使用 `aarch64-linux-gnu-g++`，也可替换为硬件厂商 SDK 提供的 GCC/Clang。
- 仓库提供 `cmake/toolchains/aarch64-linux.cmake` 和 `Linux-ARM64-Debug` 预设。
- 当前验证环境使用 WSL2 Ubuntu 22.04、AArch64 GCC 11、x86_64 Qt 6.2.4 host tools，以及 `/opt/rtmp-monitor/sysroots/jammy-arm64` 中的 ARM64 Qt 6.2.4。
- 主程序和四个测试程序已经完成 ARM64 编译、链接和 ELF 架构检查；尚未在真实 ARM64 图形环境中运行。
- ARM64 sysroot 的 `/usr/local` 中使用 FFmpeg 8.1.2 LGPL 最小动态构建，与 Windows 开发库保持版本一致。

### RTMP 验证工具

- `ffmpeg`、`ffplay`、`ffprobe` 已加入 `PATH`。
- FFmpeg 支持 H.264、RTMP 和 FLV。
- nginx-rtmp 或 SRS。
- 本地测试视频默认放在 `testdata/test.mp4`，该目录不会提交到 Git。

## 构建与测试

应在 Visual Studio Developer PowerShell 中构建。普通 PowerShell 如果没有初始化 MSVC 环境，Ninja 可能无法找到 C++ 标准库头文件。

当前机器已经配置 `Qt-Debug` 用户预设时，执行：

```powershell
cmake --preset Qt-Debug
cmake --build out/build-windows-x64/debug
ctest --test-dir out/build-windows-x64/debug --output-on-failure
```

运行程序：

```powershell
./out/build-windows-x64/debug/rtmp_monitor.exe
```

默认依次拉取以下四路：

```text
rtmp://127.0.0.1:1935/live/camera001
rtmp://127.0.0.1:1935/live/camera002
rtmp://127.0.0.1:1935/live/camera003
rtmp://127.0.0.1:1935/live/camera004
```

`--url` 可重复 1～4 次，依次覆盖 Camera 01～04；未覆盖的位置继续使用默认地址。
单次 `--url` 兼容原有用法：

```powershell
./out/build-windows-x64/debug/rtmp_monitor.exe `
    --url rtmp://127.0.0.1:1935/live/camera001
```

覆盖四路的示例：

```powershell
./out/build-windows-x64/debug/rtmp_monitor.exe `
    --url rtmp://127.0.0.1:1935/live/camera001 `
    --url rtmp://127.0.0.1:1935/live/camera002 `
    --url rtmp://127.0.0.1:1935/live/camera003 `
    --url rtmp://127.0.0.1:1935/live/camera004
```

超过四次会在创建窗口前报告参数错误。当前仅接受 `rtmp://` URL，并仅解码 H.264
视频；不处理音频、RTMPS、录像或硬件解码。任一路连接失败或推流停止时，仅对应
格子清黑并显示重连状态。URL 可能包含凭据，因此错误信息不会回显完整地址。

新环境没有 `Qt-Debug` 用户预设时，可使用通用 Visual Studio Generator：

```powershell
$env:QTDIR = "E:\QT6\6.6.1\msvc2019_64"
cmake -S . -B out/build-windows-x64/debug-vs2022 `
    -G "Visual Studio 17 2022" `
    -A x64 `
    -DCMAKE_PREFIX_PATH="$env:QTDIR" `
    -DBUILD_TESTING=ON
cmake --build out/build-windows-x64/debug-vs2022 --config Debug
ctest --test-dir out/build-windows-x64/debug-vs2022 -C Debug --output-on-failure
```

首次准备 WSL2 环境时，以 root 运行幂等配置脚本。Qt/FFmpeg 源码、下载和
sysroot 位于 WSL 的 G 盘 VHDX，项目构建产物位于 E 盘仓库：

```bash
sudo bash scripts/setup_arm64_build_env.sh
bash scripts/verify_ffmpeg_arm64_env.sh
cmake --preset Linux-ARM64-Debug
cmake --build --preset Linux-ARM64-Debug
```

ARM64 Debug 构建目录为 `out/build-linux-arm64/debug`。当前只要求四个测试目标完成 ARM64 编译和链接，不在 WSL2 中运行 Qt GUI 测试；全屏、QPA、OpenGL、真实 RTMP 播放和交互必须在硬件盒子上验收。完整环境、产物检查和故障排查见 [跨平台构建说明](docs/cross_platform_build.md)。

当前自动化测试包括：

| 测试目标 | 主要覆盖 |
|---|---|
| `rtmp_monitor_ui_smoke_test` | QSS、拖拽对象交换、全屏转移和恢复 |
| `rtmp_monitor_dynamic_grid_test` | 默认四格、1～16 路布局、数量上限、状态互斥和工具栏状态 |
| `rtmp_monitor_ffmpeg_player_test` | URL 校验、重复停止、连接失败重连和可中断退出 |
| `rtmp_monitor_multi_stream_test` | 四路独立实例、索引路由、故障隔离、两阶段停止和可选真实四路解码 |

播放器测试默认不要求本机存在 RTMP 服务。要额外执行真实 H.264 拉流、解码和 RGB888 输出检查，可先启动推流，再设置：

```powershell
$env:RTMP_MONITOR_TEST_URL = "rtmp://127.0.0.1:1935/live/camera001"
ctest --test-dir out/build-windows-x64/debug `
    -R rtmp_monitor_ffmpeg_player_test --output-on-failure
```

真实四路集成测试使用分号分隔的四个地址：

```powershell
$env:RTMP_MONITOR_TEST_URLS = @(
    "rtmp://127.0.0.1:1935/live/camera001",
    "rtmp://127.0.0.1:1935/live/camera002",
    "rtmp://127.0.0.1:1935/live/camera003",
    "rtmp://127.0.0.1:1935/live/camera004"
) -join ";"
ctest --test-dir out/build-windows-x64/debug `
    -R rtmp_monitor_multi_stream_test --output-on-failure
```

## Week 4 四路人工验收

仓库提供分阶段引导脚本，用本机测试视频启动四个带颜色和
`CAMERA 001`～`CAMERA 004` 标签的 RTMP 推流，并管理 nginx、Qt 程序及故障注入。
脚本状态和日志只写入被 Git 忽略的 `out/week4-multi-stream-manual/`。

```powershell
# 1. 检查 FFmpeg、nginx、测试视频、Debug 程序和 1935 端口
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\scripts\test_week4_multi_stream.ps1 -Action Check

# 2. 启动四路推流和 Qt 程序
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\scripts\test_week4_multi_stream.ps1 -Action Start

# 3. 查看进程与资源状态
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\scripts\test_week4_multi_stream.ps1 -Action Status

# 4. 注入并恢复 Camera 03 断流
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\scripts\test_week4_multi_stream.ps1 -Action StopCamera03
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\scripts\test_week4_multi_stream.ps1 -Action StartCamera03

# 5. 安全清理本次验收启动的进程
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\scripts\test_week4_multi_stream.ps1 -Action Stop
```

脚本不会自动拖拽或双击；四路画面、Camera 03 故障隔离、Camera 01/04 拖拽交换、
Camera 02/03 单路全屏以及三种关闭场景仍需按
[第四周首批四路独立播放说明](docs/week4_multi_stream_playback.md)中的人工验收清单操作。
脚本默认路径适配当前 Windows 11 开发机，也可用 `-FfmpegPath`、`-NginxRoot`、
`-InputFile`、`-AppPath`、`-OutputRoot` 和 `-StreamUrls` 覆盖。

## QSS 样式加载

应用启动时按以下优先级加载：

```text
<可执行文件目录>/styles/app.qss
  -> 外部样式优先，修改后重启生效
:/styles/app.qss
  -> 外部文件缺失或不可读时使用 QRC 回退
```

CMake 构建后会把 `resources/styles/app.qss` 复制到可执行文件同级 `styles/`。选择器契约和主题扩展方式见 [QSS 样式加载说明](docs/style_loading.md)。

## RTMP 链路验证

环境检查：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
    -File ./scripts/verify_rtmp_chain.ps1 `
    -Action Check
```

启动 nginx-rtmp、FFmpeg 推流和 ffplay：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
    -File ./scripts/verify_rtmp_chain.ps1 `
    -Action All
```

默认测试地址：

```text
rtmp://127.0.0.1:1935/live/camera001
```

详细参数和问题排查见 [RTMP 推流链路验证说明](docs/rtmp_chain_verification.md)。

## 桌面实况延迟测试

仓库提供双屏“同屏双时钟”测试工具。副屏显示并采集蓝色 SOURCE 毫秒时钟，
主屏同时显示绿色 REFERENCE 时钟和 Camera 01；同一张截图中两者的时间差就是
包含采集、编码、RTMP、解码、RGB 转换和 UI 绘制在内的端到端延迟。

```powershell
# 检查双屏、FFmpeg、nginx-rtmp、Qt 和 Debug 程序
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\scripts\test_desktop_latency.ps1 -Action Check

# 启动副屏实况推流和 Qt 播放器
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\scripts\test_desktop_latency.ps1 -Action Start

# 采集 10 个样本
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\scripts\test_desktop_latency.ps1 `
    -Action Capture -SampleCount 10 -SampleIntervalMs 1000

# 停止脚本启动的临时进程
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\scripts\test_desktop_latency.ps1 -Action Stop
```

截图、FFmpeg 日志和状态清单写入被 Git 忽略的 `out/desktop-latency/`，不得上传。
本次 10 个样本的最小值为 92 ms、平均值为 117.4 ms、中位数为 117.5 ms、
P95 和最大值均为 156 ms。该结果是本机回环基线，不包含真实摄像头、远端网络
或 ARM64 硬件盒子的额外延迟。

## 仓库上传边界

提交和推送前应按下面的边界检查。仓库只保存能够复现项目的源码、配置、脚本和
正式文档，不保存个人机器状态、密钥、构建产物或测试素材。

可以上传：

- `include/`、`src/`、`resources/` 和 `tests/` 中的源码与测试；
- `CMakeLists.txt`、共享 `CMakePresets.json`、`cmake/` 和 `.gitattributes`；
- `scripts/` 中不包含凭据、可重复执行的环境和测试脚本；
- `docs/` 中的项目规划、构建说明、代码规范及 Week 2～Week 4 正式文档；
- 只含占位符、不含真实值的 `.env.example`。

当前 Week 4 分支新增或更新的可上传内容包括：

- 四路播放管理器、`FFmpegPlayer` 生命周期改造、主窗口和程序入口；
- 动态网格回归测试与四路生命周期、索引路由、故障隔离测试；
- Week 4 架构/验收文档和可覆盖本机路径的人工验收脚本；
- CMake、项目规划与本 README。

不可以上传：

- `.env`、凭据、私钥、token、带认证信息的 RTMP URL；
- `.vs/`、`out/`、`build/`、`CMakeUserPresets.json` 和编辑器本地配置；
- `testdata/`、MP4/FLV/MKV、H.264/H.265 裸码流、YUV、HLS 分片和播放列表；
- 日志、转储、PID、抓包、桌面截图、延迟样本和录像分析产物；
- `docs/project_handoff.md`、`docs/local/`、`.agents/` 和 `.codex/` 等个人工作记录。

检查命令：

```powershell
git status --short
git status --short --ignored
git diff --check
```

`.gitignore` 已覆盖上述本地文件。若新增文件不确定是否适合上传，应先检查其是否
包含机器绝对路径、账号、凭据、个人桌面信息或不可再分发的二进制数据。
验收脚本中的默认绝对路径只是当前开发机的可覆盖默认值，不包含凭据；实际生成的
状态文件、PID、日志和截图仍必须留在 `out/` 中。`docs/project_handoff.md` 是本机
交接记录，不属于正式项目文档，继续保持忽略。

## 敏感配置

当前项目不需要 OpenAI 或其他 AI 服务 API Key。开发过程中使用的 GPT/API 凭据不得写入源码、README、CMake、脚本或提交记录。

安全要求：

- API Key 只保存在操作系统环境变量、密钥管理器或被 Git 忽略的 `.env` 文件中。
- 不在日志、截图、测试输出和 RTMP URL 中记录密钥或完整 token。
- 可以提交 `.env.example`，但只能包含占位符，例如 `OPENAI_API_KEY=replace_me`。
- 提交前使用 `git status` 和密钥扫描检查暂存区。
- 如果密钥曾经提交过，仅删除文件不够，必须立即吊销并轮换该密钥。

`.gitignore` 已排除 `.env`、本地凭据目录、私钥文件、日志、转储、构建目录和测试媒体。

## FFmpeg 分发边界

项目以动态方式链接 LGPL 配置的 FFmpeg 8.1.2。发布程序时应一并提供对应许可证声明、FFmpeg 源码获取方式和实际构建参数，并允许用户替换动态库。不得在没有重新评估许可证的情况下启用 `--enable-gpl`、`--enable-nonfree`、x264 或 x265。

Windows 发布目录需要 `avformat-62.dll`、`avcodec-62.dll`、`avutil-60.dll` 和 `swscale-9.dll`；ARM64 设备需要部署与 sysroot ABI 一致的 `.so`。Debug/Release DLL 不能混用，ARM64 运行时也不能混入 WSL 宿主的 x86_64 库。

## 下一步

在真实 ARM64 设备上验证 QPA、四路持续拉流、断线恢复、关闭耗时和资源占用。
根据测量数据再决定是否引入 UI 限帧、硬件解码或更大规模的统一解码调度，不预先
让长生命周期阻塞式解码任务占用通用线程池。

## 文档索引

- [项目规划](docs/project_plan.md)
- [Windows x64 与 Linux ARM64 跨平台构建说明](docs/cross_platform_build.md)
- [第三周 FFmpegPlayer 初学者教程](docs/week3_ffmpeg_player.md)
- [第三周桌面实况端到端延迟测试](docs/week3_desktop_latency_test.md)
- [第四周首批四路独立播放说明](docs/week4_multi_stream_playback.md)
- [第二周 UI 布局说明](docs/week2_ui_layout.md)
- [动态视频网格详解](docs/week2_dynamic_grid.md)
- [拖拽换位与单路全屏详解](docs/week2_drag_and_fullscreen.md)
- [QSS 样式加载说明](docs/style_loading.md)
- [RTMP 推流链路验证说明](docs/rtmp_chain_verification.md)
- [项目代码规范](docs/code_style_guide.md)
- [注释规范](docs/comment_style_guide.md)
