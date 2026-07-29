# Week 6：Windows 与 WSL2 ARM64 OpenGL 环境及原型验证

> 验证日期：2026-07-29
> 验证边界：Windows 完成编译与真实 GPU 运行；WSL2 完成 ARM64 交叉编译、链接和 ELF 门禁，不代表 ARM GPU 已运行。

## 1. 本周结论

| 平台 | 结论 | 证据 |
| --- | --- | --- |
| Windows x86_64 | 通过 | WGL 隐藏上下文和 Qt `QOpenGLWidget` 纹理原型均实际运行成功；完整 CTest 10/10 通过。 |
| WSL2 / Linux ARM64 | 交叉构建门禁通过 | 主程序、EGL/GLES2 冒烟程序和 Qt OpenGL 原型均生成 ELF64/AArch64，并链接目标 sysroot 中的 ARM64 库。 |
| ARM64 真实盒子 | 待验收 | 未在 WSL2/QEMU 中宣称 QPA、EGLFS、Wayland/X11、GPU 或 OpenGL ES 运行成功。 |
| 生产渲染路径 | 未改变 | `VideoWidget` 的 QPainter/QImage 路径仍为默认；OpenGL 代码是独立、可选原型。 |
| 600 秒性能测试 | 未执行 | 本周没有改变 Week 4 交接文档中的长测结论，渲染架构决策仍需等待长测数据。 |

## 2. 存储约束与依赖位置

本次没有安装 GLAD、GLEW 或新的 Windows vcpkg 包，也没有把下载、缓存或构建输出写到 C 盘。

```text
E:\rtmpProject
  源码、Windows 构建、ARM64 交叉构建和 Week 6 日志

E:\QT6\6.6.1\msvc2019_64
  Windows Qt 6.6.1 MSVC x64

E:\C
  MSVC 14.41 工具链

F:\DevTools\vcpkg
  Windows FFmpeg 8.1.2 开发库

G:\WSL\Ubuntu-22.04-New\ext4.vhdx
  WSL2 系统、交叉编译器和 ARM64 sysroot

F:\WSL\wsl-swap.vhdx
  WSL2 swap

/opt/rtmp-monitor/sysroots/jammy-arm64
  Ubuntu 22.04 ARM64 Qt、GL/EGL/GLES 和 FFmpeg sysroot
```

Windows 仍只读使用现有 Windows SDK：

```text
C:\Program Files (x86)\Windows Kits\10\include\10.0.22621.0\um\GL\gl.h
C:\Program Files (x86)\Windows Kits\10\lib\10.0.22621.0\um\x64\OpenGL32.Lib
```

`scripts/test_week6_opengl.ps1` 会拒绝解析到 C 盘的可写输出路径，并把
`TEMP`、`TMP`、`APPDATA`、`LOCALAPPDATA`、日志和报告临时重定向到
`E:\rtmpProject\out\week6-opengl`。环境变量只在脚本进程内生效。

## 3. 实现内容

### 3.1 可选 RGB 纹理控件

新增 `VideoRenderWidget`，继承 `QOpenGLWidget`：

- `setFrame(const QImage&)` 在 UI 线程保存一份隐式共享图像，并请求下一次绘制。
- `paintGL()` 将待处理图像转换为 RGBA8888、上传 `QOpenGLTexture`，使用
  `QOpenGLTextureBlitter` 按宽高比居中绘制。
- 无画面时使用黑色清屏；窗口缩放时重新计算目标矩形。
- `clearFrame()` 释放当前帧；`openGLInitialized(...)` 报告 vendor、renderer 和 version；
  `frameRendered()` 用于确认纹理绘制路径已经执行。
- OpenGL 资源只在持有当前上下文时创建和销毁，并监听
  `QOpenGLContext::aboutToBeDestroyed` 处理上下文重建。

该控件没有接入 FFmpeg YUV 数据，没有替换现有 `VideoWidget`，也没有改变网格、拖拽或全屏逻辑。

### 3.2 CMake 与冒烟目标

`RTMP_MONITOR_BUILD_OPENGL_PROTOTYPE` 控制原型，当前 Windows 和
`Linux-ARM64-Debug` 验证配置均启用。

| 目标 | 平台 | 作用 |
| --- | --- | --- |
| `rtmp_monitor_opengl_windows_smoke` | Windows | 创建隐藏 Win32/WGL 上下文，读取 GL 字符串、清屏并交换缓冲。 |
| `rtmp_monitor_qt_opengl_smoke` | Windows/ARM64 | 使用 `VideoRenderWidget` 上传测试渐变图；Windows 实际运行，ARM64 只生成目标 ELF。 |
| `rtmp_monitor_opengl_egl_smoke` | ARM64 | 创建 EGL pbuffer 和 OpenGL ES 2.0 上下文，供交叉链接门禁及以后盒子运行。 |

Windows 链接 `OpenGL::GL`、`Qt6::OpenGL` 和 `Qt6::OpenGLWidgets`。ARM64
配置只接受位于 `CMAKE_SYSROOT` 内的 EGL/GLES2 头文件和库；若 CMake 查找结果逃出
sysroot 会直接失败，防止宿主 x86_64 库混入。

## 4. Windows 实测

执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_week6_opengl.ps1
```

工具和运行时：

| 项目 | 实测值 |
| --- | --- |
| 编译器 | MSVC 14.41，位于 `E:\C` |
| Qt | 6.6.1 MSVC x64，位于 `E:\QT6\6.6.1\msvc2019_64` |
| Windows SDK | 10.0.22621.0，只读使用 C 盘现有安装 |
| OpenGL vendor | NVIDIA Corporation |
| OpenGL renderer | NVIDIA GeForce RTX 3060 Laptop GPU/PCIe/SSE2 |
| OpenGL version | 4.6.0 NVIDIA 591.86 |

结果：

```text
rtmp_monitor_opengl_windows_smoke  Passed
rtmp_monitor_qt_opengl_smoke       Passed
完整 Windows Debug CTest            10/10 Passed
完整 CTest 用时                    75.25 s
```

首次人工观察 Qt 冒烟程序时，Computer Use 捕获到调试断言：

```text
ASSERT failure in VideoRenderWidget:
"Called object is not of the correct type (class destructor may have already run)"
```

原因是派生类析构开始后，`QOpenGLWidget` 基类销毁上下文仍可能通过
`aboutToBeDestroyed` 调用派生类成员。修复方式是在 `VideoRenderWidget` 析构入口先断开
保存的 `QMetaObject::Connection`，再在当前上下文中释放纹理和 blitter。修复后窗口正常绘制、
自动退出，且没有残留断言窗口。

报告保存在被 Git 忽略的：

```text
out/week6-opengl/windows-opengl-validation.txt
```

## 5. WSL2 ARM64 环境与门禁

### 5.1 环境修复

`setup_arm64_build_env.sh` 的幂等检查现覆盖：

- `GL/gl.h`、`EGL/egl.h`、`GLES2/gl2.h`
- `libGL.so`、`libEGL.so`、`libGLESv2.so`
- `Qt6OpenGL`、`Qt6OpenGLWidgets` CMake 模块和目标库

本机检查发现 sysroot 已有 GL/EGL/GLES 和 `Qt6OpenGLWidgets`，但缺少
`Qt6OpenGLConfig.cmake`。脚本只在 G 盘 WSL VHDX 的隔离 ARM64 sysroot 中补装：

```text
libqt6opengl6-dev arm64 6.2.4+dfsg-2ubuntu1.1
```

已有依赖保持不变：

```text
libgl-dev arm64 1.4.0-1
libegl-dev arm64 1.4.0-1
libgles-dev arm64 1.4.0-1
qt6-base-dev arm64 6.2.4+dfsg-2ubuntu1.1
```

安装完成后脚本执行 `apt-get clean` 并删除宿主与 sysroot 的 APT 索引。隔离
chroot 因未挂载 `/dev/pts` 输出了一条无法写终端日志的提示，但包安装、环境检查和后续
交叉构建均成功。

修复命令：

```bash
sudo bash scripts/setup_arm64_build_env.sh
```

### 5.2 交叉构建与 ELF 检查

执行：

```bash
bash scripts/verify_opengl_arm64_env.sh
```

实测工具链：

```text
aarch64-linux-gnu-g++ 11.4.0
CMake 3.22.1
Ninja 1.10.1
Qt target 6.2.4
```

门禁结果：

- `rtmp_monitor` 为 ELF64/AArch64。
- 现有 UI、动态网格、FFmpeg 生命周期、多流、日志、用户消息、连接控制器和日志面板
  测试目标均逐一通过 ELF64/AArch64 检查。
- `rtmp_monitor_opengl_egl_smoke` 为 ELF64/AArch64，动态依赖包含
  `libEGL.so` 和 `libGLESv2.so`。
- `rtmp_monitor_qt_opengl_smoke` 为 ELF64/AArch64，动态依赖包含
  `libQt6OpenGLWidgets.so` 和 `libQt6OpenGL.so`。
- `aarch64-linux-gnu-readelf -h` 的 `Machine` 为 `AArch64`。
- 三个目标均未发现 Windows、MinGW 或 x86_64 动态依赖。
- 配置提示缺少可选 XKB/Vulkan 包，但它们不是本次 EGL/GLES2/Qt OpenGL
  交叉链接目标的必需项，构建和门禁不受影响。

验证脚本不会安装软件，也不会运行 AArch64 图形程序。

## 6. ARM64 真实设备验收命令

先把 ELF 和与目标镜像匹配的 Qt/FFmpeg/GL 运行库部署到真实 ARM64 盒子，再按设备
实际图形栈选择命令。

纯 EGL/OpenGL ES：

```bash
./rtmp_monitor_opengl_egl_smoke
```

Qt EGLFS：

```bash
QT_QPA_PLATFORM=eglfs ./rtmp_monitor_qt_opengl_smoke
```

Qt Wayland 或 X11：

```bash
QT_QPA_PLATFORM=wayland ./rtmp_monitor_qt_opengl_smoke
QT_QPA_PLATFORM=xcb ./rtmp_monitor_qt_opengl_smoke
```

设备验收必须记录硬件型号、系统镜像、GPU 驱动、实际 QPA、EGL/GL ES vendor、
renderer、version、窗口显示和退出码。只有这些命令在真实设备成功后，才能表述为
“ARM64 OpenGL 运行通过”。

## 7. 后续边界

- 先完成 Week 4 留下的两项 600 秒性能资格测试，再决定是否把生产渲染路径抽象为
  OpenGL 或继续优化 QPainter/QImage。
- 若长测证明 RGB 转换或 QImage 拷贝是瓶颈，再设计 FFmpeg YUV 三纹理上传；本原型不能
  直接作为 YUV 或零拷贝实现。
- ARM64 侧必须先通过真实 QPA、EGL/OpenGL ES 和一路软件解码，再评估厂商 VPU/GPU
  互操作。
- 不使用 WSL2、QEMU 或交叉链接成功替代真实盒子的图形、网络和长期稳定性验收。
