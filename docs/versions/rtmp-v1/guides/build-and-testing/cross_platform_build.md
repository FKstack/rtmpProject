# Windows x64 与 Linux ARM64 跨平台构建说明

> 文档分类：构建与验证。
>
> 发布标识：`RtmpMonitor v0.1.0-alpha.1`；Windows 为 **Development Preview**，
> ARM Linux 为 **Engineering Preview**。Windows+WSL2 最小 SRS 链路已独立复验；
> 4/16 路与 600 秒结果属于历史证据；ARM 真机、真实摄像头和网页现场结果仍为
> `[需要验证]`。嵌入式接收方应先填写
> [嵌入式二次开发交接与环境填写模板](embedded_developer_handoff.md)。

## 1. 验证范围

RtmpMonitor 使用同一套 C++17/Qt Widgets 源码生成两个目标：

| 目标 | 编译环境 | 当前验证结论 |
| --- | --- | --- |
| Windows x86_64 | Visual Studio 2022、MSVC、Qt 6.6.1、FFmpeg 8.1.2 LGPL、Ninja | 2026-08-15 Windows Debug 全目标构建与完整 CTest 29/29 通过（118.13 秒），Release 全目标构建通过；未把自动化音频测试描述为真实 SRS/声学延迟复验。 |
| Linux ARM64 | WSL2 Ubuntu 22.04、AArch64 GCC 11、Qt 6.2.4 Multimedia、FFmpeg 8.1.2 LGPL | 2026-08-14 RASTER/GLES3 主程序和全测试目标生成 AArch64 ELF，音频依赖为 Qt Multimedia/libswresample；双路径脚本门禁及 QEMU 纯逻辑测试通过。 |

ARM64 结果证明源码可以通过目标编译器编译并链接目标 Qt、EGL 和 OpenGL ES 3.0，不代表程序已在硬件盒子上运行。Wayland、X11、EGLFS、全屏、输入事件和性能仍需实机验证。

## 2. 存储位置

仓库只约定目录职责，不固定开发者的 Windows 盘符或个人绝对路径：

```text
<workspace>\rtmpProject
  -> 源码；构建产物和验证报告统一写入被 Git 忽略的 out\

<tool-root>\vcpkg
<cache-root>\vcpkg-downloads
<cache-root>\vcpkg-binary-cache
<temp-root>\rtmp-monitor-vcpkg
  -> Windows x64 FFmpeg 开发库、下载、二进制缓存和临时文件

<wsl-storage>\<distribution>.vhdx
<wsl-storage>\wsl-swap.vhdx
  -> WSL 发行版和 swap；具体宿主机位置由接收方自行记录，不写入公共配置

/opt/rtmp-monitor/sysroots/jammy-arm64
  -> WSL/Linux 内的项目标准 ARM64 Qt/FFmpeg sysroot

/opt/rtmp-monitor/ffmpeg-arm64
  -> WSL/Linux 内的 FFmpeg 8.1.2 源码、交叉构建目录和构建参数
```

Windows 侧通过 `VCPKG_ROOT`、`QTDIR` 和被忽略的 `CMakeUserPresets.json` 注入
本机位置。空间受限时可把工具和缓存放到非系统盘，但不得把个人路径提交到公共
Preset 或文档。`out/` 已被 Git 忽略。

## 3. Windows x64 构建

首次 clone 后使用统一入口。下面的路径是占位符，不应写入公共配置：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File scripts/setup_windows_dev.ps1 `
    -Action All -Configuration Debug `
    -QtRoot "<Qt-msvc-root>" -ToolRoot "<tool-root>"
```

脚本检查 Visual Studio 2022 和 Qt 6.6.1 MSVC x64，但不自动安装它们；只自动准备
项目专用 vcpkg 和 `ffmpeg[core,avcodec,avformat,swscale]`。默认不修改用户 PATH，
所有子进程使用隔离的 MSVC/Qt/vcpkg 环境。底层安装固定使用已验证的 vcpkg 提交
`4eb0f7cabb9ca18132d80009312411b9261bba7b`，防止 FFmpeg 端口随 `master` 漂移。
公共 `CMakePresets.json` 的隐藏预设 `Windows-MSVC-vcpkg` 通过
`$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake` 启用依赖发现；被 Git 忽略的
`CMakeUserPresets.json` 在 `Qt-Debug`/`Qt-Release` 的 `environment` 中提供本机
`VCPKG_ROOT`。不要把任何开发者的绝对路径写入公共 Preset。

在 Visual Studio Developer PowerShell 中执行：

```powershell
cmake --preset Qt-Debug --fresh
cmake --build out/build-windows-x64/debug
ctest --test-dir out/build-windows-x64/debug --output-on-failure
```

日常开发使用 `Qt-Debug`；发布候选验证使用独立的 `Qt-Release`：

```powershell
cmake --preset Qt-Release --fresh
cmake --build out/build-windows-x64/release
ctest --test-dir out/build-windows-x64/release --output-on-failure
```

`Qt-Debug` 和 `Qt-Release` 来自本机忽略提交的 `CMakeUserPresets.json`，均应指向
MSVC Qt Kit。
GUI 的首选运行方式是 Visual Studio 将 `rtmp_monitor.exe` 设为启动项后按 F5；命令行
只负责构建/CTest。Windows 构建只接受 MSVC，不得与 MinGW Qt 库混用。

Windows 环境与 OpenGL/CTest 验证：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\setup_windows_dev.ps1 `
  -Action Test -Configuration Debug `
  -QtRoot "<Qt-msvc-root>" -ToolRoot "<tool-root>"
```

统一入口复用开发者已安装的 MSVC/Qt/CMake/Ninja 与 Windows SDK，并检查固定版本
的 vcpkg/FFmpeg。需要强制工具、缓存、临时文件和构建目录离开系统盘时，可同时传入
`-RequireNonSystemDrive` 以及显式的 `-DownloadsRoot`、`-BinaryCacheRoot`、
`-TemporaryRoot` 和 `-BuildRoot`。CTest 包括：

- 隐藏 Win32/WGL 上下文、清屏和缓冲交换。
- `VideoRenderWidget` RGB/RGBA 纹理上传与绘制。
- 完整 Windows Debug CTest 回归。

## 4. WSL2 环境配置

支持的宿主基线为 Ubuntu 22.04 Jammy；WSL 发行版名称可以不同。先以普通用户检查，
只有缺失环境时才以 root 安装：

```bash
bash scripts/setup_linux_arm64_dev.sh --action check
sudo bash scripts/setup_linux_arm64_dev.sh --action install
bash scripts/setup_linux_arm64_dev.sh --action all --render-mode both
```

脚本会完成以下工作：

1. 默认直连；只有调用方显式提供 `--proxy-url` 时才在直连失败后使用代理。
2. 安装 AArch64 GCC、Ninja、CMake、QEMU 和 x86_64 Qt host tools。
3. 检查 Ubuntu multiarch 运行库版本；版本不一致时使用隔离 sysroot，避免降级宿主系统。
4. 在 `/opt/rtmp-monitor/sysroots/jammy-arm64` 安装 ARM64 Qt 6（含 Multimedia）、ALSA，以及
   `libgl-dev`、`libegl-dev`、`libgles-dev`、`libqt6opengl6-dev`。
5. 下载 FFmpeg 8.1.2 官方源码、签名与发布密钥，并验证签名密钥指纹。
6. 使用 AArch64 GCC 构建仅含 RTMP、FLV、H.264/AAC 解码、swscale 和 swresample 的 LGPL 共享库，安装到 sysroot 的 `/usr/local`。
7. 验证 `moc/rcc/uic` 为 x86_64，Qt 和 FFmpeg 目标库为 ARM64，并检查 FFmpeg 未启用 GPL/nonfree。
8. 清理 APT 缓存；Qt 和 FFmpeg 环境完整时重复运行会跳过慢速安装与构建。

只有 `install` 必须使用 root 权限，因为它会修改 WSL 的 APT 架构和工作根目录；
`check/build/test/all` 均以普通用户运行。脚本不会修改 Windows SDK，也不会执行完整
系统升级。`--work-root`、`--sysroot` 和 `--build-root` 可以覆盖标准路径。

## 5. Linux ARM64 构建

日常开发直接使用统一入口：

```bash
bash scripts/setup_linux_arm64_dev.sh --action build --render-mode raster
bash scripts/setup_linux_arm64_dev.sh --action build --render-mode gles3
```

公共 Preset 保留兼容的 `Linux-ARM64-Debug`（AUTO），并提供
`Linux-ARM64-RASTER-Debug` 与 `Linux-ARM64-GLES3-Debug`。高级用户仍可直接调用
对应 CMake Preset。

该预设使用：

| 配置 | 路径或值 |
| --- | --- |
| 工具链 | `cmake/toolchains/aarch64-linux.cmake` |
| 目标 sysroot | `/opt/rtmp-monitor/sysroots/jammy-arm64` |
| 目标 Qt CMake package | sysroot 内的 `usr/lib/aarch64-linux-gnu/cmake/Qt6` |
| Qt host tools | `/usr/lib/qt6/libexec` |
| FFmpeg 目标库 | sysroot 内的 `usr/local/lib/aarch64-linux-gnu` |
| FFmpeg pkg-config | sysroot 内的 `usr/local/lib/aarch64-linux-gnu/pkgconfig` |
| 构建目录 | AUTO 为 `out/build-linux-arm64/debug`；明确模式为 `raster-debug`/`gles3-debug` |

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
- 动态依赖包含 ARM64 `libQt6Widgets.so.6`、`libQt6Gui.so.6`、`libQt6Core.so.6`、`libQt6Multimedia.so.6`、`libavformat.so.62`、`libavcodec.so.62`、`libavutil.so.60`、`libswscale.so.9` 和 `libswresample.so.6`。
- 动态依赖不出现 Windows DLL、PE 文件或 x86_64 Qt 库。
- 构建规则中的 `moc/rcc/uic` 来自宿主 `/usr/lib/qt6/libexec`。

统一入口会在 QEMU 用户态运行经过筛选的纯逻辑测试。QEMU 不能模拟真实 Qt 平台插件、GPU 或 RTMP 实时播放。

GLES3 环境、交叉构建和动态依赖的一键门禁：

```bash
bash scripts/setup_linux_arm64_dev.sh --action all --render-mode gles3
```

该命令先检查环境，不自行安装软件。它要求 GL/EGL/GLES3 头文件和库、
`Qt6OpenGL`/`Qt6OpenGLWidgets` CMake 模块均位于 ARM64 sysroot，随后构建全部
目标，并用 `file` 与 `aarch64-linux-gnu-readelf` 检查：

- 纯图形目标依赖 `libEGL.so` 和 `libGLESv2.so`。
- Qt 生产渲染依赖 `libQt6OpenGLWidgets.so` 和 `libQt6OpenGL.so`。
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

## 7.1 Linux ARM64 RASTER 工程预览包

仓库提供 `scripts/package_linux_arm64.sh`，只接受已经完成交叉编译的
Release、Linux/AArch64、RASTER、`BUILD_TESTING=OFF` 构建。脚本拒绝覆盖已有阶段目录、归档或
审计目录，不修改 sysroot，也不把 SRS、DVR、用户 MQTT/保存推流配置、测试媒体、源码或调试文件
放入客户端包。

推荐使用独立构建目录：

```bash
export PKG_CONFIG_SYSROOT_DIR=/opt/rtmp-monitor/sysroots/jammy-arm64
export PKG_CONFIG_LIBDIR=/opt/rtmp-monitor/sysroots/jammy-arm64/usr/local/lib/aarch64-linux-gnu/pkgconfig:/opt/rtmp-monitor/sysroots/jammy-arm64/usr/local/share/pkgconfig

cmake -S . -B out/build-linux-arm64/package-raster-release -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux.cmake \
  -DARM64_SYSROOT=/opt/rtmp-monitor/sysroots/jammy-arm64 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DRTMP_MONITOR_LINUX_RENDER_MODE=RASTER \
  -DRTMP_MONITOR_BUILD_OPENGL_PROTOTYPE=OFF \
  -DQt6_DIR=/opt/rtmp-monitor/sysroots/jammy-arm64/usr/lib/aarch64-linux-gnu/cmake/Qt6 \
  -DQT_HOST_PATH=/usr \
  -DQT_HOST_PATH_CMAKE_DIR=/usr/lib/x86_64-linux-gnu/cmake
cmake --build out/build-linux-arm64/package-raster-release --parallel

bash scripts/package_linux_arm64.sh \
  --build-dir out/build-linux-arm64/package-raster-release \
  --output-dir out/packages/RtmpMonitor-0.1.0-alpha.1-linux-arm64-raster \
  --sysroot /opt/rtmp-monitor/sysroots/jammy-arm64 \
  --version 0.1.0-alpha.1
```

输出包括阶段目录、同名 `.tar.gz` 和独立 `audit-*` 目录。包内带可替换的 Qt 6.2.4、FFmpeg 8.1.2、
Paho MQTT C 1.3.16 动态库，以及 linuxfb/minimal/offscreen、evdev 输入和基础图片插件；glibc、图形、
输入、字体、GStreamer/音频等操作系统组件不捆绑，准确 SONAME 清单见包内
`SYSTEM_RUNTIME_DEPENDENCIES.txt`。因此该包只面向与 Ubuntu 22.04 Jammy AArch64 ABI 兼容的工程
验证设备，不是适用于所有 ARM64 板卡的通用发行包。

目标板解压后运行：

```bash
chmod +x run-rtmp-monitor.sh
QT_QPA_PLATFORM=offscreen ./run-rtmp-monitor.sh --version
QT_QPA_PLATFORM=linuxfb ./run-rtmp-monitor.sh
```

打包流程不计算、不比对也不生成 SHA-256，不提供内容防篡改、数字签名或可信时间戳保证。仍必须在
真实设备验证 glibc、QPA、framebuffer、输入、字体、音频、实际 RTMP 播放和长期资源占用。

## 8. 真实设备验收

确定硬件盒子后，需要用厂商 SDK/sysroot 复验 ABI，并至少完成：

- 程序启动、Qt QPA 插件加载和字体资源。
- 动态网格、拖拽交换、单路全屏和控制栏交互。
- Wayland、X11 或 EGLFS 中实际采用的一种图形环境。
- OpenGL ES、屏幕选择、光标隐藏和窗口恢复。
- FFmpeg 软件解码、网络重连、播放中/连接中/重连中关闭和长期运行。
- 后续硬件解码回退与多路资源占用。

通用 Jammy AArch64 ELF 是工程门禁，不是最终发布包。

板卡、系统镜像、ABI、Qt/QPA、EGL/GLES、显示输入和摄像头编码参数应记录到
[嵌入式二次开发交接与环境填写模板](embedded_developer_handoff.md)；没有真机证据的
字段必须保持 `[需要验证]`。

最小图形实机命令：

```bash
./rtmp_monitor_opengl_egl_smoke
QT_QPA_PLATFORM=eglfs ./rtmp_monitor_qt_opengl_smoke
```

若设备使用 Wayland 或 X11，应把 `QT_QPA_PLATFORM` 改为实际采用的
`wayland` 或 `xcb`。详细 Week 6 结果和边界见
[产品级 OpenGL 视频渲染与验证总览](../../weeks/week6/week6_opengl_environment_and_validation.md)。

## 9. SRS Server ARM64 部署边界

> 本节是 `docs/versions/rtmp-v1/architecture/srs_server_integration_plan.md` Phase 2 的落地说明；SRS 是
> 独立基础设施，不属于上方 Qt 客户端构建链。

- 版本固定为 SRS 6.0.184（`v6.0-r0`），与 Windows 开发侧使用同一份
  `deploy/srs/conf/srs-minimal.conf`：1935 对所有接口监听，1985 只绑定
  回环，RAW API 关闭。
- **首选目标设备本机构建**：官方 ARM 文档建议 ARMv7/ARMv8 直接
  `./configure && make`。使用仓库脚本：

```bash
bash scripts/srs/build_srs_arm64.sh \
    --source-dir <srs-6.0.184-repo> \
    --prefix /opt/rtmp-monitor/srs-6.0.184 \
    --config deploy/srs/conf/srs-minimal.conf \
    --mode native
```

- **交叉编译仅作后备**：目标设备资源不足、工具链明确且 ABI 已冻结时才用
  `--mode cross --cross-prefix aarch64-linux-gnu-`。交叉构建只证明生成
  AArch64 ELF，**交叉构建不等于实机通过**；必须在目标板重新执行
  `file/ldd/srs -v` 和链路验证。
- 目标板链路验收（本机推拉流）：

```bash
bash scripts/srs/verify_srs_chain.sh \
    --srs-home /opt/rtmp-monitor/srs-6.0.184 \
    --srs-source <srs-6.0.184-repo>
```

- 产品期由 systemd 管理，安装仓库提供的
  `deploy/srs/systemd/rtmp-monitor-srs.service`（`SIGQUIT` 优雅停止、
  `on-failure` 重启、日志由 journald 接管）。第一版以系统服务默认用户
  运行；正式产品化前创建最小权限服务用户并复验（标记 `[需要验证]`）。
- 防火墙只向摄像头/客户端所在 LAN 网段开放 TCP 1935；1985 保持回环。
- 在硬件/SDK 未确定前，目标 glibc/musl、动态加载器、sysroot、systemd
  可用性和存储边界全部标记 `[需要验证]`；不得把当前 Qt/FFmpeg ARM64
  sysroot 自动假定为 SRS 的运行 sysroot。

## 10. Windows 便携 ZIP

必须先完成 Windows Release 全目标构建和 CTest，再从 Visual Studio x64 开发环境执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\package_windows.ps1 `
  -BuildDir .\out\build-windows-x64\release `
  -OutputDir .\out\packages\RtmpMonitor-0.1.0-alpha.1-windows-x64 `
  -Version 0.1.0-alpha.1
```

输出目录必须不存在或为空，脚本拒绝覆盖已有阶段目录、审计目录和 ZIP。包内使用 app-local MSVC
运行库，不携带 `vc_redist.x64.exe`；包含 Qt Multimedia、`multimedia/windowsmediaplugin.dll`、FFmpeg
和 swresample DLL，明确排除 Qt 的 `ffmpegmediaplugin.dll`、PDB、源码、SRS、资格素材与报告。

Codex 桌面宿主可能同时带有 `PATH` 和 `Path`。脚本在探测 FFmpeg DLL 时会使用进程级最小搜索
路径；手工运行 Release CTest 时也应先同时清除两个变量，再仅加入 Release 目录、MSVC Qt、vcpkg
Release bin 和 Windows 系统目录，防止误载 Conda/MinGW Qt。

生成 ZIP 后必须解压到全新目录，并在收敛 Path 下完成 `--version`、GUI 启动、真实 SRS 视频、
开启声音、静音和正常退出冒烟。2026-08-15 产物为
`out/packages/RtmpMonitor-0.1.0-alpha.1-windows-x64.zip`；产物和审计均在 Git 忽略目录，不提交仓库。
