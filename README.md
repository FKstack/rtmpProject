# RtmpMonitor

RtmpMonitor 是一个面向 Windows PC 的多路嵌入式摄像头实时预览软件。嵌入式设备将 H.264 视频通过 RTMP 推送到流媒体服务器，PC 端后续将使用 Qt + FFmpeg 拉流、解码，并以多宫格方式显示画面。

当前项目遵循“先验证 RTMP 链路，再实现 Qt 播放器”的路线：第一阶段的外部 RTMP 推流/拉流验证已完成；Qt 端目前只提供可构建的 Widgets 主窗口骨架，尚未接入 FFmpeg。

完整路线见 [项目规划](docs/project_plan.md)。

## 当前进度

| 阶段 | 状态 | 当前产出 |
| --- | --- | --- |
| 第 1 周：RTMP 推流链路验证 | 已完成 | `nginx-rtmp` + FFmpeg + ffplay 验证脚本及使用文档。 |
| Qt 6 Widgets 工程骨架 | 已完成 | CMake、MSVC、C++17、`MainWindow` 空窗口。 |
| 第 2 周：多宫格布局 | 待开始 | `VideoWidget`、`VideoGridWidget`、2x2 布局。 |
| 第 3 周：一路 FFmpeg 播放 | 待开始 | RTMP 拉流、H.264 解码、QImage 显示。 |

当前 Qt 程序不包含 FFmpeg、RTMP 拉流、H.264 解码或视频显示逻辑。这些能力会在后续阶段逐步加入。

## 项目结构

```text
rtmpProject/
├── CMakeLists.txt                   # Qt 6 Widgets / MSVC 构建定义
├── CMakePresets.json                # Qt Creator 生成的基础预设
├── README.md                        # 项目入口与快速开始
├── docs/
│   ├── project_plan.md              # 完整项目规划与学习路线
│   └── rtmp_chain_verification.md   # 第一阶段验证脚本说明
├── include/ui/
│   └── MainWindow.h                 # 当前空主窗口声明
├── scripts/
│   └── verify_rtmp_chain.ps1        # FFmpeg + nginx-rtmp 链路验证脚本
└── src/
    ├── main.cpp                     # Qt 应用入口
    └── ui/MainWindow.cpp            # 当前空主窗口实现
```

## 环境要求

### Qt 桌面程序

- Windows 10/11 x64。
- Visual Studio 2022，并安装“使用 C++ 的桌面开发”工作负载。
- CMake 3.21 或更高版本。
- Qt 6 的 MSVC x64 Kit，例如 `msvc2022_64`。

必须使用 MSVC 版 Qt。不要将 `mingw_64` 版 Qt 与 Visual Studio/MSVC 混用；工程会在配置阶段主动拒绝该组合。

### 第 1 阶段 RTMP 链路验证

- FFmpeg，且 `ffmpeg`、`ffplay`、`ffprobe` 已加入 `PATH`。
- FFmpeg 支持 `libx264`、RTMP 协议和 FLV 封装。
- nginx-rtmp，默认目录为 `E:\DevTools\nginx-rtmp`。
- 本地测试视频，默认路径为 `testdata\test.mp4`；测试视频不应提交到 Git。

## 快速开始

### 1. 验证 RTMP 推流链路

在项目根目录执行环境检查：

```powershell
powershell `
    -NoProfile `
    -ExecutionPolicy Bypass `
    -File ".\scripts\verify_rtmp_chain.ps1" `
    -Action Check
```

一键启动 nginx-rtmp、FFmpeg 推流和 ffplay 播放：

```powershell
powershell `
    -NoProfile `
    -ExecutionPolicy Bypass `
    -File ".\scripts\verify_rtmp_chain.ps1" `
    -Action All
```

默认测试 RTMP 地址为：

```text
rtmp://127.0.0.1:1935/live/camera001
```

脚本的参数、nginx 配置要求、退出顺序和问题排查见 [RTMP 推流链路验证脚本说明](docs/rtmp_chain_verification.md)。

### 2. 构建并启动 Qt 空窗口

在 Visual Studio 2022 的“开发人员 PowerShell”中，先将 `QTDIR` 设置为本机 **MSVC** Qt Kit 的根目录：

```powershell
$env:QTDIR = "D:\Qt\6.8.0\msvc2022_64"
```

再执行 CMake 配置和构建：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="$env:QTDIR"
cmake --build build --config Debug
```

运行生成的空窗口：

```powershell
.\build\Debug\rtmp_monitor.exe
```

如果从命令行运行时提示缺少 Qt DLL，可在 Qt Kit 的 `bin` 目录中执行 `windeployqt.exe`，或者直接通过 Qt Creator 启动工程。

## 常见问题

| 现象 | 原因与处理 |
| --- | --- |
| CMake 提示检测到 MinGW Qt Kit | `CMAKE_PREFIX_PATH` 指向了 `mingw_64`。将 `QTDIR` 改为 Qt 的 `msvc2022_64` 目录后重新配置。 |
| 程序启动提示缺少 Qt DLL | 使用 `windeployqt` 部署依赖，或从 Qt Creator 运行。 |
| 脚本提示找不到 `ffmpeg` | 将 FFmpeg 的 `bin` 目录加入 `PATH`，再重新打开 PowerShell。 |
| 脚本检查 nginx 失败 | 确认 `E:\DevTools\nginx-rtmp\sbin\nginx.exe`、`conf\nginx.conf` 和 HLS 临时目录存在。 |
| RTMP 推流成功但 ffplay 黑屏 | 核对 RTMP 地址和 nginx `application live` 配置，等待 H.264 关键帧后重试。 |

## 下一步

下一阶段将实现 `VideoWidget` 和 `VideoGridWidget`：使用 Qt Widgets 构建固定 2x2 视频宫格，每个格子先显示设备名称、连接状态和黑色占位区域。届时仍不会接入 FFmpeg，先把 UI 和布局稳定下来。

## 文档索引

- [项目规划](docs/project_plan.md)
- [RTMP 推流链路验证脚本说明](docs/rtmp_chain_verification.md)
