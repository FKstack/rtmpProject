# RtmpMonitor

RtmpMonitor 是一个面向 Windows PC 的多路嵌入式摄像头实时预览软件。嵌入式设备将 H.264 视频通过 RTMP 推送到流媒体服务器；PC 端后续将使用 Qt + FFmpeg 拉流、解码，并以多宫格方式显示画面。

项目采用渐进式路线：先验证 RTMP 基础链路，再完成 Qt UI 框架，最后接入 FFmpeg 一路播放与多路解码。完整设计见 [项目规划](docs/project_plan.md)。

## 当前进度

| 阶段 | 状态 | 当前产出 |
| --- | --- | --- |
| 第 1 周：RTMP 推流链路验证 | 已完成 | `nginx-rtmp` + FFmpeg + ffplay 验证脚本及使用文档。 |
| 第 2 周：Qt 2x2 多宫格布局 | 已完成 | `MainWindow`、`VideoWidget`、`VideoGridWidget`、CTests 冒烟测试。 |
| QSS 样式加载 | 已完成 | `Singleton<StyleLoader>`、外部 QSS 优先、QRC 回退和部署复制规则。 |
| 第 3 周：一路 FFmpeg 播放 | 待开始 | RTMP 拉流、H.264 解码、QImage 显示。 |

当前程序已经显示固定 2x2 视频格：`camera001` 至 `camera004`。每个格子包含设备名称、“未连接”状态和黑色视频占位区域；尚未接入 FFmpeg、RTMP、H.264 解码或真实视频帧。

## 项目结构

```text
rtmpProject/
├── CMakeLists.txt                   # Qt 6 / MSVC / CTest / 样式部署构建规则
├── CMakePresets.json                # 可共享的 CMake 预设
├── README.md
├── docs/
│   ├── project_plan.md              # 整体项目规划
│   ├── rtmp_chain_verification.md   # 第一阶段 RTMP 验证说明
│   ├── week2_ui_layout.md           # 第二周 UI 布局说明
│   ├── style_loading.md             # QSS 加载与部署说明
│   └── comment_style_guide.md       # C++/Qt 注释规范
├── include/
│   ├── app/StyleLoader.h            # 应用级 QSS 加载服务
│   ├── core/Singleton.h             # 通用 CRTP 单例模板
│   └── ui/                          # MainWindow、视频格和网格控件声明
├── resources/
│   ├── styles/app.qss               # 默认应用样式
│   └── styles.qrc                   # 内置样式资源映射
├── scripts/
│   └── verify_rtmp_chain.ps1        # FFmpeg + nginx-rtmp 验证脚本
├── src/
│   ├── app/StyleLoader.cpp
│   ├── ui/                          # Qt Widgets 实现
│   └── main.cpp
└── tests/
    └── VideoGridSmokeTest.cpp       # UI、单例和样式加载冒烟测试
```

## 环境要求

### Qt 桌面程序

- Windows 10/11 x64。
- Visual Studio 2022，并安装“使用 C++ 的桌面开发”工作负载。
- CMake 3.21 或更高版本。
- Qt 6 的 MSVC x64 Kit；当前验证环境使用 `E:\QT6\6.6.1\msvc2019_64`。

必须使用 MSVC 版 Qt。不要将 `mingw_64` 版 Qt 与 Visual Studio/MSVC 混用；工程会在配置阶段主动拒绝该组合。

### 第 1 阶段 RTMP 链路验证

- FFmpeg，且 `ffmpeg`、`ffplay`、`ffprobe` 已加入 `PATH`。
- FFmpeg 支持 `libx264`、RTMP 协议和 FLV 封装。
- nginx-rtmp，默认目录为 `E:\DevTools\nginx-rtmp`。
- 本地测试视频默认路径为 `testdata\test.mp4`；该目录已被 Git 忽略，不提交测试媒体。

## 构建、测试与运行

在 Visual Studio 2022 的“开发人员 PowerShell”中设置 Qt Kit：

```powershell
$env:QTDIR = "E:\QT6\6.6.1\msvc2019_64"
```

配置并构建：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="$env:QTDIR" -DBUILD_TESTING=ON
cmake --build build --config Debug
```

运行自动化测试：

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

运行程序：

```powershell
.\build\Debug\rtmp_monitor.exe
```

若从命令行运行时提示缺少 Qt DLL，可使用 Qt Kit `bin` 目录中的 `windeployqt.exe` 部署依赖，或从 Qt Creator 直接运行。

## QSS 样式加载

应用启动时由 `StyleLoader` 统一加载 QSS：

```text
<可执行文件目录>/styles/app.qss
  -> 外部样式优先，修改后重启应用生效
:/styles/app.qss
  -> 外部文件缺失或不可读时的内置 QRC 回退
```

构建目标后，CMake 会自动把 `resources/styles/app.qss` 复制到可执行文件同级 `styles/` 目录。QSS 选择器和扩展方式见 [QSS 样式加载说明](docs/style_loading.md)。

## RTMP 链路快速验证

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

默认测试地址：

```text
rtmp://127.0.0.1:1935/live/camera001
```

脚本参数、nginx 配置、退出顺序和排查流程见 [RTMP 推流链路验证脚本说明](docs/rtmp_chain_verification.md)。

## 下一步

下一阶段实现一路 `FFmpegPlayer`：在工作线程拉取 RTMP、解码 H.264、转换为 `QImage`，并只通过 Qt 信号槽更新指定的 `VideoWidget`。UI 线程不会执行网络读取或解码操作。

## 文档索引

- [项目规划](docs/project_plan.md)
- [第二周 UI 布局说明](docs/week2_ui_layout.md)
- [QSS 样式加载说明](docs/style_loading.md)
- [RTMP 推流链路验证脚本说明](docs/rtmp_chain_verification.md)
- [注释规范](docs/comment_style_guide.md)
