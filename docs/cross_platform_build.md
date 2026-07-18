# Windows x64 与 Linux ARM64 跨平台构建说明

## 1. 验证范围

RtmpMonitor 使用同一套 C++17/Qt Widgets 源码生成两个目标：

| 目标 | 编译环境 | 当前验证结论 |
| --- | --- | --- |
| Windows x86_64 | Visual Studio 2022、MSVC、Qt 6.6.1、Ninja | 主程序构建成功，CTest 2/2 通过，窗口启动 5 秒且保持响应。 |
| Linux ARM64 | WSL2 Ubuntu 22.04、AArch64 GCC 11、Qt 6.2.4 | 主程序和两个测试程序均生成 AArch64 ELF。 |

ARM64 结果证明源码可以通过目标编译器编译并链接目标 Qt，不代表程序已在硬件盒子上运行。Wayland、X11、EGLFS、全屏、OpenGL、输入事件和性能仍需实机验证。

## 2. 存储位置

为减少 C 盘占用，当前环境使用以下位置：

```text
E:\rtmpProject
  -> 源码及 Windows/ARM64 构建产物

G:\WSL\Ubuntu-22.04-New\ext4.vhdx
  -> WSL 系统、交叉编译器、Qt host tools、ARM64 sysroot

F:\WSL\wsl-swap.vhdx
  -> WSL2 swap

/opt/rtmp-monitor/sysroots/jammy-arm64
  -> WSL VHDX 内的 Ubuntu 22.04 ARM64 Qt sysroot
```

不要把 SDK、sysroot 或构建目录放入 `%TEMP%`、用户下载目录或 C 盘。`out/` 已被 Git 忽略。

## 3. Windows x64 构建

在 Visual Studio Developer PowerShell 中执行：

```powershell
cmake --preset Qt-Debug
cmake --build out/build-windows-x64/debug
ctest --test-dir out/build-windows-x64/debug --output-on-failure
./out/build-windows-x64/debug/rtmp_monitor.exe
```

`Qt-Debug` 来自本机忽略提交的 `CMakeUserPresets.json`，当前指向 `E:/QT6/6.6.1/msvc2019_64`。Windows 构建只接受 MSVC，不得与 MinGW Qt 库混用。

## 4. WSL2 环境配置

发行版名称为 `Ubuntu-22.04-New`。首次配置或修复环境时，在仓库根目录执行：

```bash
sudo bash scripts/setup_arm64_build_env.sh
```

脚本会完成以下工作：

1. 优先使用 Windows `127.0.0.1:7890` 代理，失败时回退直连。
2. 安装 AArch64 GCC、Ninja、CMake、QEMU 和 x86_64 Qt host tools。
3. 检查 Ubuntu multiarch 运行库版本；版本不一致时使用隔离 sysroot，避免降级宿主系统。
4. 在 `/opt/rtmp-monitor/sysroots/jammy-arm64` 安装 ARM64 Qt 6 与 OpenGL 开发依赖。
5. 验证 `moc/rcc/uic` 为 x86_64，`libQt6Widgets` 为 ARM64。
6. 清理 APT 缓存；环境完整时重复运行会跳过慢速目标包安装。

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
| 构建目录 | `out/build-linux-arm64/debug` |

构建同时生成：

```text
rtmp_monitor
rtmp_monitor_ui_smoke_test
rtmp_monitor_dynamic_grid_test
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
- 动态依赖包含 ARM64 `libQt6Widgets.so.6`、`libQt6Gui.so.6` 和 `libQt6Core.so.6`。
- 动态依赖不出现 Windows DLL、PE 文件或 x86_64 Qt 库。
- 构建规则中的 `moc/rcc/uic` 来自宿主 `/usr/lib/qt6/libexec`。

当前不在 WSL 中运行这两个 Qt GUI 测试。QEMU 仅用于最小 ARM64 C++17 命令行烟雾测试，不能模拟真实 Qt 平台插件和 GPU。

## 7. 常见问题

### Qt Widgets 提示找不到 WrapOpenGL

确认 sysroot 中安装 `libgl-dev`、`libegl-dev` 和 `libgles-dev`，然后删除失败配置生成的 `out/build-linux-arm64/debug`，重新运行 preset。CMake 会缓存 `OPENGL_INCLUDE_DIR-NOTFOUND`，补包后直接重试可能仍使用旧结果。

### `qt_standard_project_setup` 不存在

Ubuntu 22.04 提供 Qt 6.2.4，该版本没有较新 Qt 的便利命令。项目会在命令存在时调用；旧版继续使用显式启用的 AUTOMOC、AUTOUIC 和 AUTORCC。

### 信号携带的指针类型不完整

Qt 6.2 在部分类型化 `connect()` 场景中要求指针目标是完整类型。实现文件应包含对应类头文件，不能只依赖前置声明。

### CMake 找到了 ARM64 `moc`

检查 `QT_HOST_PATH=/usr` 和 `QT_HOST_PATH_CMAKE_DIR=/usr/lib/x86_64-linux-gnu/cmake`。代码生成工具必须在 x86_64 WSL 宿主运行，只有库和头文件来自 ARM64 sysroot。

## 8. 真实设备验收

确定硬件盒子后，需要用厂商 SDK/sysroot 复验 ABI，并至少完成：

- 程序启动、Qt QPA 插件加载和字体资源。
- 动态网格、拖拽交换、单路全屏和控制栏交互。
- Wayland、X11 或 EGLFS 中实际采用的一种图形环境。
- OpenGL ES、屏幕选择、光标隐藏和窗口恢复。
- 后续 FFmpeg 软件解码、硬件解码回退、网络重连和长期运行。

通用 Jammy AArch64 ELF 是工程门禁，不是最终发布包。
