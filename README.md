# RtmpMonitor

RtmpMonitor 是一个面向 Windows x86_64 PC 与 Linux ARM64 嵌入式硬件盒子的多路摄像头监控客户端。目标链路是：嵌入式设备主动推送 H.264/RTMP，客户端使用 FFmpeg 拉流解码，并通过 Qt Widgets 在一个窗口中显示多路实时画面。

项目采用渐进式实现：先用 nginx-rtmp 或 SRS 验证推流链路，再完成可维护的多路 UI 框架，随后接入一路 FFmpeg 播放并扩展多路解码。完整路线见 [项目规划](docs/project_plan.md)。

## 当前状态

| 阶段 | 状态 | 已完成内容 |
|---|:---:|---|
| 第 1 周：RTMP 链路验证 | 已完成 | nginx-rtmp、FFmpeg、ffplay 验证脚本和排查文档 |
| 第 2 周：Qt 多路 UI | 已完成 | 1～16 路动态网格、添加动画、拖拽交换、单路全屏 |
| QSS 与工程规范 | 已完成 | 外部 QSS 优先、QRC 回退、代码和注释规范 |
| 第 3 周：一路 FFmpeg 播放 | 待开始 | RTMP 拉流、H.264 解码、QImage 显示 |

当前版本没有接入 FFmpeg 开发库，也不会播放真实 RTMP 画面。每个视频格中的黑色区域是后续唯一的视频渲染承载区。

## 已实现功能

- 程序启动时真实创建一个 `Camera 01` 视频窗口。
- 点击顶部“添加视频窗口”，可逐个创建到 `Camera 16`。
- 根据数量自动使用 1x1、1x2、2x2、2x3、3x3、3x4 或 4x4 布局。
- 拖拽任意两个视频格，交换实际 `VideoWidget` 对象和逻辑顺序。
- 添加和交换使用快照动画，不直接动画 `QGridLayout` 管理的真实控件。
- 双击视频格进入单路全屏，支持双击、`Esc` 和控制栏按钮退出。
- 全屏底部提供自动隐藏的 Overlay 控制栏，静音和截图暂为接口占位。
- 添加、拖拽和全屏通过统一状态互斥，避免动画重入。
- 使用 `StyleLoader` 统一加载外部或 QRC 内置 QSS。

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
- FFmpeg 命令行工具：当前用于 RTMP 链路验证
- FFmpeg 开发库：计划在第三周接入
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
│       └── ui/                      # Qt Widgets 接口
├── resources/
│   ├── styles/app.qss
│   └── styles.qrc
├── scripts/
│   ├── setup_arm64_build_env.sh
│   └── verify_rtmp_chain.ps1
├── src/
│   ├── main.cpp                     # 跨平台程序入口
│   ├── common/                      # 两个平台共享的实现
│   │   ├── app/
│   │   └── ui/
│   └── platform/                    # 有真实需求时加入平台实现
│       ├── windows/
│       └── linux/
└── tests/
    ├── VideoGridSmokeTest.cpp
    └── VideoGridDynamicTest.cpp
```

## 环境要求

### Windows x86_64

- Windows 10/11 x64。
- Visual Studio 2022，并安装“使用 C++ 的桌面开发”工作负载。
- CMake 3.21 或更高版本。
- Qt 6 MSVC x64 Kit。当前验证环境为 Qt 6.6.1 `msvc2019_64`。

工程会拒绝 MinGW 编译器或 MinGW Qt Kit，不能把 MinGW Qt 库与 MSVC 混用。

### Linux ARM64

- 在 WSL2 或 Linux 主机中使用 `aarch64-linux-gnu-g++`，也可替换为硬件厂商 SDK 提供的 GCC/Clang。
- 仓库提供 `cmake/toolchains/aarch64-linux.cmake` 和 `Linux-ARM64-Debug` 预设。
- 当前验证环境使用 WSL2 Ubuntu 22.04、AArch64 GCC 11、x86_64 Qt 6.2.4 host tools，以及 `/opt/rtmp-monitor/sysroots/jammy-arm64` 中的 ARM64 Qt 6.2.4。
- 主程序和两个测试程序已经完成 ARM64 编译、链接和 ELF 架构检查；尚未在真实 ARM64 图形环境中运行。
- 第三周接入 FFmpeg 后，还需要增加与目标系统 ABI 匹配的 ARM64 FFmpeg 开发库。

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

首次准备 WSL2 环境时，以 root 运行幂等配置脚本。下载和 sysroot 位于 WSL 的 G 盘 VHDX，构建产物位于 E 盘仓库：

```bash
sudo bash scripts/setup_arm64_build_env.sh
cmake --preset Linux-ARM64-Debug
cmake --build --preset Linux-ARM64-Debug
```

ARM64 Debug 构建目录为 `out/build-linux-arm64/debug`。当前只要求两个测试目标完成 ARM64 编译和链接，不在 WSL2 中运行 Qt GUI 测试；全屏、QPA、OpenGL 和真实交互必须在硬件盒子上验收。完整环境、产物检查和故障排查见 [跨平台构建说明](docs/cross_platform_build.md)。

当前自动化测试包括：

| 测试目标 | 主要覆盖 |
|---|---|
| `rtmp_monitor_ui_smoke_test` | QSS、拖拽对象交换、全屏转移和恢复 |
| `rtmp_monitor_dynamic_grid_test` | 1～16 路布局、数量上限、状态互斥和工具栏状态 |

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

## 敏感配置

当前项目不需要 OpenAI 或其他 AI 服务 API Key。开发过程中使用的 GPT/API 凭据不得写入源码、README、CMake、脚本或提交记录。

安全要求：

- API Key 只保存在操作系统环境变量、密钥管理器或被 Git 忽略的 `.env` 文件中。
- 不在日志、截图、测试输出和 RTMP URL 中记录密钥或完整 token。
- 可以提交 `.env.example`，但只能包含占位符，例如 `OPENAI_API_KEY=replace_me`。
- 提交前使用 `git status` 和密钥扫描检查暂存区。
- 如果密钥曾经提交过，仅删除文件不够，必须立即吊销并轮换该密钥。

`.gitignore` 已排除 `.env`、本地凭据目录、私钥文件、日志、转储、构建目录和测试媒体。

## 下一步

第三周计划实现一路 `FFmpegPlayer`：在工作线程打开 RTMP、解析并解码 H.264、转换为 `QImage`，通过 Qt 信号槽更新指定 `VideoWidget`。UI 线程不得执行网络读取或解码。

## 文档索引

- [项目规划](docs/project_plan.md)
- [Windows x64 与 Linux ARM64 跨平台构建说明](docs/cross_platform_build.md)
- [第二周 UI 布局说明](docs/week2_ui_layout.md)
- [动态视频网格详解](docs/week2_dynamic_grid.md)
- [拖拽换位与单路全屏详解](docs/week2_drag_and_fullscreen.md)
- [QSS 样式加载说明](docs/style_loading.md)
- [RTMP 推流链路验证说明](docs/rtmp_chain_verification.md)
- [项目代码规范](docs/code_style_guide.md)
- [注释规范](docs/comment_style_guide.md)
