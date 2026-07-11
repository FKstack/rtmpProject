# RtmpMonitor

这是 PC 端多路 RTMP 视频接收与显示项目的 Qt 6 Widgets 工程骨架。当前版本只创建并显示一个空的主窗口，尚未接入 FFmpeg、RTMP 或视频显示逻辑。

## 前置条件

- Visual Studio 2022，并安装“使用 C++ 的桌面开发”工作负载。
- Qt 6 的 MSVC x64 套件，例如 `msvc2022_64`。
- CMake 3.21 或更高版本。

不要将 MinGW 版 Qt（路径通常包含 `mingw_64`）与 Visual Studio/MSVC 混用。工程会在配置阶段主动拒绝这种组合。

## 配置与构建

在 Visual Studio 开发人员 PowerShell 中运行。将 `CMAKE_PREFIX_PATH` 替换为本机 Qt Kit 的实际路径：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="D:/Qt/6.8.0/msvc2022_64"
cmake --build build --config Debug
```

## 运行

```powershell
.\build\Debug\rtmp_monitor.exe
```

若从命令行启动时提示缺少 Qt DLL，请使用 Qt 安装目录中的 `windeployqt.exe` 部署依赖，或在 Qt Creator 中直接运行。
