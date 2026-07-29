# Windows x64 与 Linux ARM64 跨平台构建说明

> 文档分类：构建与验证。

## 1. 验证范围

RtmpMonitor 使用同一套 C++17/Qt Widgets 源码生成两个目标：

| 目标 | 编译环境 | 当前验证结论 |
| --- | --- | --- |
| Windows x86_64 | Visual Studio 2022、MSVC、Qt 6.6.1、FFmpeg 8.1.2 LGPL、Ninja | 主程序构建成功；2026-07-29 完整 CTest 10/10 通过，WGL 和 Qt OpenGL 冒烟程序实际运行成功。 |
| Linux ARM64 | WSL2 Ubuntu 22.04、AArch64 GCC 11、Qt 6.2.4、FFmpeg 8.1.2 LGPL | 主程序、测试、EGL/GLES2 冒烟程序和 Qt OpenGL 原型均生成 AArch64 ELF。 |

ARM64 结果证明源码可以通过目标编译器编译并链接目标 Qt、EGL 和 GLES2，不代表程序已在硬件盒子上运行。Wayland、X11、EGLFS、全屏、OpenGL ES、输入事件和性能仍需实机验证。

## 2. 存储位置

为减少 C 盘占用，当前环境使用以下位置：

```text
E:\rtmpProject
  -> 源码、Windows/ARM64 构建产物和 out/week6-opengl 验证日志

F:\DevTools\vcpkg
F:\DevTools\vcpkg-downloads
F:\DevTools\vcpkg-binary-cache
F:\Temp\rtmp-monitor-vcpkg
  -> Windows x64 FFmpeg 开发库、下载、二进制缓存和临时文件

G:\WSL\Ubuntu-22.04-New\ext4.vhdx
  -> WSL 系统、交叉编译器、Qt host tools、ARM64 sysroot

F:\WSL\wsl-swap.vhdx
  -> WSL2 swap

/opt/rtmp-monitor/sysroots/jammy-arm64
  -> WSL VHDX 内的 Ubuntu 22.04 ARM64 Qt/FFmpeg sysroot

/opt/rtmp-monitor/ffmpeg-arm64
  -> FFmpeg 8.1.2 已验证源码、交叉构建目录和构建参数
```

不要把 SDK、sysroot 或构建目录放入 `%TEMP%`、用户下载目录或 C 盘。`out/` 已被 Git 忽略。

## 3. Windows x64 构建

首次配置 FFmpeg 开发环境：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
    -File scripts/setup_ffmpeg_windows_dev.ps1
```

脚本使用 vcpkg 的 `x64-windows` 动态 triplet，仅安装
`ffmpeg[core,avcodec,avformat,swscale]`。vcpkg 根目录会加入用户 PATH，但
Release/Debug FFmpeg DLL 目录不会永久加入 PATH，避免运行时混用配置。
脚本固定使用已验证的 vcpkg 提交 `4eb0f7cabb9ca18132d80009312411b9261bba7b`，
防止端口版本随 `master` 漂移。
当前 `CMakeUserPresets.json` 通过
`F:/DevTools/vcpkg/scripts/buildsystems/vcpkg.cmake` 为 Windows Preset 启用依赖发现。

在 Visual Studio Developer PowerShell 中执行：

```powershell
cmake --preset Qt-Debug
cmake --build out/build-windows-x64/debug
ctest --test-dir out/build-windows-x64/debug --output-on-failure
./out/build-windows-x64/debug/rtmp_monitor.exe
```

`Qt-Debug` 来自本机忽略提交的 `CMakeUserPresets.json`，当前指向 `E:/QT6/6.6.1/msvc2019_64`。Windows 构建只接受 MSVC，不得与 MinGW Qt 库混用。

Week 6 OpenGL 一键验证：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_week6_opengl.ps1
```

脚本复用 E 盘 MSVC/Qt/CMake/Ninja 和 F 盘 FFmpeg，只读使用 C 盘现有
Windows SDK 的 `GL/gl.h` 与 `OpenGL32.Lib`。它会拒绝 C 盘可写输出路径，并把
`TEMP`、`TMP`、`APPDATA`、`LOCALAPPDATA`、日志和报告临时重定向到
`out/week6-opengl`。验证包括：

- 隐藏 Win32/WGL 上下文、清屏和缓冲交换。
- `VideoRenderWidget` RGB/RGBA 纹理上传与绘制。
- 完整 Windows Debug CTest 回归。

## 4. WSL2 环境配置

发行版名称为 `Ubuntu-22.04-New`。首次配置或修复环境时，在仓库根目录执行：

```bash
sudo bash scripts/setup_arm64_build_env.sh
bash scripts/verify_ffmpeg_arm64_env.sh
```

脚本会完成以下工作：

1. 优先使用 Windows `127.0.0.1:7890` 代理，失败时回退直连。
2. 安装 AArch64 GCC、Ninja、CMake、QEMU 和 x86_64 Qt host tools。
3. 检查 Ubuntu multiarch 运行库版本；版本不一致时使用隔离 sysroot，避免降级宿主系统。
4. 在 `/opt/rtmp-monitor/sysroots/jammy-arm64` 安装 ARM64 Qt 6，以及
   `libgl-dev`、`libegl-dev`、`libgles-dev`、`libqt6opengl6-dev`。
5. 下载 FFmpeg 8.1.2 官方源码、签名与发布密钥，并验证签名密钥指纹。
6. 使用 AArch64 GCC 构建仅含 RTMP、FLV、H.264 解码和 swscale 的 LGPL 共享库，安装到 sysroot 的 `/usr/local`。
7. 验证 `moc/rcc/uic` 为 x86_64，Qt 和 FFmpeg 目标库为 ARM64，并检查 FFmpeg 未启用 GPL/nonfree。
8. 清理 APT 缓存；Qt 和 FFmpeg 环境完整时重复运行会跳过慢速安装与构建。

脚本必须使用 root 权限，因为它会修改 WSL 的 APT 架构和 `/opt`。它不会修改 Windows C 盘中的 SDK，也不会执行完整系统升级。

## 5. Linux ARM64 构建

在 WSL 仓库目录 `/mnt/e/rtmpProject` 中执行：

```bash
cmake --preset Linux-ARM64-Debug
cmake --build --preset Linux-ARM64-Debug
```

该预设使用：

| 配置 | 路径或值 |
| --- | --- |
| 工具链 | `cmake/toolchains/aarch64-linux.cmake` |
| 目标 sysroot | `/opt/rtmp-monitor/sysroots/jammy-arm64` |
| 目标 Qt CMake package | sysroot 内的 `usr/lib/aarch64-linux-gnu/cmake/Qt6` |
| Qt host tools | `/usr/lib/qt6/libexec` |
| FFmpeg 目标库 | sysroot 内的 `usr/local/lib/aarch64-linux-gnu` |
| FFmpeg pkg-config | sysroot 内的 `usr/local/lib/aarch64-linux-gnu/pkgconfig` |
| 构建目录 | `out/build-linux-arm64/debug` |

构建同时生成：

```text
rtmp_monitor
rtmp_monitor_ui_smoke_test
rtmp_monitor_dynamic_grid_test
rtmp_monitor_ffmpeg_player_test
rtmp_monitor_opengl_egl_smoke
rtmp_monitor_qt_opengl_smoke
```

## 6. ARM64 产物检查

```bash
file out/build-linux-arm64/debug/rtmp_monitor
aarch64-linux-gnu-readelf -h out/build-linux-arm64/debug/rtmp_monitor
aarch64-linux-gnu-readelf -d out/build-linux-arm64/debug/rtmp_monitor
```

验收结果必须满足：

- `file` 包含 `ELF 64-bit` 和 `ARM aarch64`。
- ELF `Machine` 为 `AArch64`。
- 动态依赖包含 ARM64 `libQt6Widgets.so.6`、`libQt6Gui.so.6`、`libQt6Core.so.6`、`libavformat.so.62`、`libavcodec.so.62`、`libavutil.so.60` 和 `libswscale.so.9`。
- 动态依赖不出现 Windows DLL、PE 文件或 x86_64 Qt 库。
- 构建规则中的 `moc/rcc/uic` 来自宿主 `/usr/lib/qt6/libexec`。

当前不在 WSL 中运行交叉编译出的测试程序。QEMU 仅用于最小 ARM64 C++17 命令行烟雾测试，不能模拟真实 Qt 平台插件、GPU 或 RTMP 实时播放。

OpenGL 环境、交叉构建和动态依赖的一键门禁：

```bash
bash scripts/verify_opengl_arm64_env.sh
```

该脚本只验证，不自行安装软件。它要求 GL/EGL/GLES2 头文件和库、
`Qt6OpenGL`/`Qt6OpenGLWidgets` CMake 模块均位于 ARM64 sysroot，随后构建全部
目标，并用 `file` 与 `aarch64-linux-gnu-readelf` 检查：

- 纯图形目标依赖 `libEGL.so` 和 `libGLESv2.so`。
- Qt 原型依赖 `libQt6OpenGLWidgets.so` 和 `libQt6OpenGL.so`。
- 目标为 ELF64/AArch64，且没有 Windows 或 x86_64 库混入。

FFmpeg 环境验证：

```bash
source /opt/rtmp-monitor/ffmpeg-arm64/ffmpeg-arm64.env
pkg-config --modversion libavformat libavcodec libavutil libswscale

aarch64-linux-gnu-gcc --sysroot="${PKG_CONFIG_SYSROOT_DIR}" \
    scripts/ffmpeg_smoke/ffmpeg_environment_smoke.c \
    $(pkg-config --cflags --libs libavformat libavcodec libavutil libswscale) \
    -o out/ffmpeg_environment_smoke_arm64

file out/ffmpeg_environment_smoke_arm64
aarch64-linux-gnu-readelf -h out/ffmpeg_environment_smoke_arm64
qemu-aarch64-static -L "${PKG_CONFIG_SYSROOT_DIR}" \
    -E LD_LIBRARY_PATH=/usr/local/lib/aarch64-linux-gnu \
    out/ffmpeg_environment_smoke_arm64
```

输出必须显示 FFmpeg `8.1.2` 和 `LGPL`，不能显示 GPL-only 构建。

## 7. 常见问题

### Qt Widgets 提示找不到 WrapOpenGL

确认 sysroot 中安装 `libgl-dev`、`libegl-dev`、`libgles-dev` 和
`libqt6opengl6-dev`，并同时存在 `Qt6OpenGLConfig.cmake` 与
`Qt6OpenGLWidgetsConfig.cmake`。优先重新运行幂等的
`setup_arm64_build_env.sh`；若旧缓存仍保存 `NOTFOUND`，再删除失败的 ARM64 构建目录并
重新运行 preset。

### `qt_standard_project_setup` 不存在

Ubuntu 22.04 提供 Qt 6.2.4，该版本没有较新 Qt 的便利命令。项目会在命令存在时调用；旧版继续使用显式启用的 AUTOMOC、AUTOUIC 和 AUTORCC。

### 信号携带的指针类型不完整

Qt 6.2 在部分类型化 `connect()` 场景中要求指针目标是完整类型。实现文件应包含对应类头文件，不能只依赖前置声明。

### CMake 找到了 ARM64 `moc`

检查 `QT_HOST_PATH=/usr` 和 `QT_HOST_PATH_CMAKE_DIR=/usr/lib/x86_64-linux-gnu/cmake`。代码生成工具必须在 x86_64 WSL 宿主运行，只有库和头文件来自 ARM64 sysroot。

### 找到了宿主 x86_64 FFmpeg

不要使用宿主 `/usr/lib/x86_64-linux-gnu/pkgconfig`。ARM64 Preset 已设置
`PKG_CONFIG_SYSROOT_DIR` 和 `PKG_CONFIG_LIBDIR`，目标 `.pc` 文件必须来自 sysroot
的 `/usr/local/lib/aarch64-linux-gnu/pkgconfig`。

### FFmpeg 许可证检查失败

构建脚本要求 `CONFIG_GPL=0` 和 `CONFIG_NONFREE=0`。不得通过删除检查或增加
`--enable-gpl` 绕过；确需 GPL 编码器时必须先重新评估程序分发许可证。

## 8. 真实设备验收

确定硬件盒子后，需要用厂商 SDK/sysroot 复验 ABI，并至少完成：

- 程序启动、Qt QPA 插件加载和字体资源。
- 动态网格、拖拽交换、单路全屏和控制栏交互。
- Wayland、X11 或 EGLFS 中实际采用的一种图形环境。
- OpenGL ES、屏幕选择、光标隐藏和窗口恢复。
- FFmpeg 软件解码、网络重连、播放中/连接中/重连中关闭和长期运行。
- 后续硬件解码回退与多路资源占用。

通用 Jammy AArch64 ELF 是工程门禁，不是最终发布包。

最小图形实机命令：

```bash
./rtmp_monitor_opengl_egl_smoke
QT_QPA_PLATFORM=eglfs ./rtmp_monitor_qt_opengl_smoke
```

若设备使用 Wayland 或 X11，应把 `QT_QPA_PLATFORM` 改为实际采用的
`wayland` 或 `xcb`。详细 Week 6 结果和边界见
[Windows 与 WSL2 ARM64 OpenGL 环境及原型验证](../../weeks/week6/week6_opengl_environment_and_validation.md)。
