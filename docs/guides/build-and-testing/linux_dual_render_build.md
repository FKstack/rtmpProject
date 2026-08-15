# Linux 双路径渲染构建指南（RASTER / GLES3 / AUTO）

> 日期：2026-08-08
> 依据：`docs/architecture/embedded_device_rendering_strategy.md` §16～§23 的落地实现
> 适用范围：Linux ARM64 交叉构建（WSL2）；Windows 行为不变

## 1. 三种构建模式

根 `CMakeLists.txt` 新增字符串缓存项：

```cmake
-DRTMP_MONITOR_LINUX_RENDER_MODE=AUTO   # AUTO / RASTER / GLES3
```

| 模式 | CMake 行为 | 运行时行为 |
|---|---|---|
| `RASTER` | 只 `find_package(Qt6 COMPONENTS Widgets)`；不查找、不链接 Qt OpenGL/OpenGLWidgets、EGL、GLES；不编译 `OpenGLGridRenderer.cpp`、`VideoOpenGLCanvas.cpp`、EGL 冒烟与 RGB 原型 | 只允许 CPU；`--renderer=opengl` 记录"本构建未包含 GL"并安全回退 |
| `GLES3` | 必须找到 ARM64 sysroot 内的 Qt OpenGL/OpenGLWidgets、EGL、GLESv2，缺失即配置失败；保留 sysroot 防逃逸检查 | `auto`/`opengl` 尝试真实 ES3 Context，任一必需能力失败回退 CPU 并给出可诊断原因 |
| `AUTO` | `QUIET` 探测 GL 依赖；齐全则双后端，否则自动退化为 RASTER-only 并打印摘要 | 有合格 ES3 用 GL，否则 CPU |

编译期事实通过生成的 `RtmpMonitorBuildConfig.h` 暴露为
`RTMP_MONITOR_HAS_OPENGL`（0/1）与 `RTMP_MONITOR_LINUX_RENDER_MODE`；
业务代码不使用平台宏。配置结束会打印构建摘要（目标架构、模式、是否编译
GLES3、sysroot、EGL/GLES 路径）。

## 2. 运行时后端选择链路

```text
main()
  ├─ Windows：Desktop GL 3.3 SurfaceFormat（行为不变）
  └─ Linux：
       1. LinuxApplicationBootstrap::configureSurfaceFormat()（QApplication 之前）
          RASTER 构建 / --renderer=cpu / linuxfb 候选 → 不请求 ES 3.0
       2. QApplication 创建后 LinuxRenderingPolicy::decide()
          依据 RTMP_MONITOR_HAS_OPENGL、--renderer、QGuiApplication::platformName()
          linuxfb → CPU；eglfs → GL 候选 + 单 GL 顶层窗口约束
       3. LinuxRendererFactory::rendererPreferenceFor() 映射为 RendererPreference
          CPU 决策强制 Cpu（host 不再尝试 GL）；GLES3 决策保留 auto/opengl 语义
       4. VideoOpenGLCanvas::initializeGL() 采集 EmbeddedGlCapabilities
          （实际 Context 版本、vendor/renderer/version、MAX_TEXTURE_SIZE、
          纹理单元数、R8/RG8 试分配、UNPACK_ROW_LENGTH、FBO 完整性、Shader smoke）
       5. qualifyEmbeddedGlCapabilities() 纯判定；任一必需项失败 → CPU 回退 +
          可诊断 fallbackReason
```

`EmbeddedGlCapabilities` 与 `LinuxRenderingPolicy` 都是纯逻辑，分别在
`rtmp_monitor_embedded_gl_capabilities_test` 与
`rtmp_monitor_linux_rendering_policy_test` 中跨平台回归。

## 3. EGLFS 全屏：复用主画布

`FullscreenPresentationMode`（`include/common/ui/FullscreenPresentationMode.h`）：

- `TemporaryWindowCanvas`：Windows 及经验证的 Wayland/X11，保留现有
  `FullscreenVideoWindow` 临时第二画布；
- `ReuseMainCanvas`：QPA 为 `eglfs` 时自动生效。`MainWindow` 不再隐藏自己，
  `VideoGridWidget::enterInCanvasFullscreen()` 把主画布 Snapshot 切成单路
  （Contain、30 FPS），隐藏各视频格覆盖层；Esc 或双击退出，恢复网格
  Snapshot 与 15 FPS。全屏期间 `refreshRenderSnapshot()` 只刷新单路快照；
  当前全屏流被解绑时自动退出全屏。

`OpenGLGridRenderer` 默认 `TextureRetentionPolicy::KeepRegisteredStreams`：
全屏切换成单路 Snapshot 时，其余已注册流的纹理保留（不再上传），退出全屏
无需集中重传；流被注销（删除/断开）时纹理立即释放；Context lost 仍全部释放。
S0 极低内存设备可通过 `setTextureRetentionPolicy(ReleaseImmediately)` 恢复原
立即释放行为。

## 4. WSL2 交叉构建命令

推荐入口会同时处理环境检查、模式选择、构建目录和 ELF/QEMU 门禁：

```bash
bash scripts/setup_linux_arm64_dev.sh --action all --render-mode both
```

需要单独调用 CMake 时，使用受跟踪的明确模式 Preset：

```bash
cmake --preset Linux-ARM64-RASTER-Debug
cmake --build --preset Linux-ARM64-RASTER-Debug --parallel
cmake --preset Linux-ARM64-GLES3-Debug
cmake --build --preset Linux-ARM64-GLES3-Debug --parallel
```

以下展开命令保留用于厂商 SDK/sysroot 覆盖和排障。

```bash
# 在 WSL2 bash 中执行；sysroot 路径以本机为准
export PKG_CONFIG_SYSROOT_DIR=/opt/rtmp-monitor/sysroots/jammy-arm64
export PKG_CONFIG_LIBDIR=/opt/rtmp-monitor/sysroots/jammy-arm64/usr/local/lib/aarch64-linux-gnu/pkgconfig:/opt/rtmp-monitor/sysroots/jammy-arm64/usr/local/share/pkgconfig

# 纯 CPU/LinuxFB 可部署构建
cmake -S . -B out/build-linux-arm64/raster-debug -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux.cmake \
  -DARM64_SYSROOT=/opt/rtmp-monitor/sysroots/jammy-arm64 \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
  -DRTMP_MONITOR_LINUX_RENDER_MODE=RASTER \
  -DQt6_DIR=/opt/rtmp-monitor/sysroots/jammy-arm64/usr/lib/aarch64-linux-gnu/cmake/Qt6 \
  -DQT_HOST_PATH=/usr -DQT_HOST_PATH_CMAKE_DIR=/usr/lib/x86_64-linux-gnu/cmake
cmake --build out/build-linux-arm64/raster-debug --parallel

# 包含 OpenGL ES 3.0 的构建（同上，模式改为 GLES3，目录改 gles3-debug）
```

每套构建验收：

```bash
file out/build-linux-arm64/<mode>-debug/rtmp_monitor          # ELF64 aarch64
readelf -h ... | grep Machine                                 # AArch64
readelf -d ... | grep NEEDED                                  # RASTER 不得出现
                                                              # Qt6OpenGL*/libEGL/libGLES
```

注意：若 Qt6Gui 自身在 sysroot 中仍间接依赖 GL，这是 Qt 构建事实；只有使用
`-no-opengl` 配置的 Qt sysroot 才能实现整棵依赖闭包零 GL。

## 5. 板级资格脚本

`scripts/qualify_embedded_device.sh` 由用户在真实目标板上手动运行，不得在
CI/WSL2/应用启动时自动执行：

```bash
./scripts/qualify_embedded_device.sh \
    --app /opt/rtmpmonitor/rtmp_monitor \
    --url-template 'rtmp://192.168.1.10:1935/live/cam%02d' \
    --ladder "1 4 9 16" --warmup 20 --sample 120 \
    --max-cpu-percent 85 --max-frame-age-p95-ms 200 \
    --max-temp-millic 85000 \
    --output ./qualification-report
```

- 路数阶梯、预热/采样时长与门槛全部由参数控制，默认阶梯 `1 4 9 16` 只是示例；
- 任一门槛（CPU、温度、内存、进程退出）失败即停止升档，已完成档位报告保留；
- 输出 `device_profile.txt` 设备档案：boardId/imageVersion/qpa/
  qualifiedBackend/testedStreamProfile/outputResolution/recommendedMaxStreams/
  testedAt/reportId；
- `recommendedMaxStreams` 只声明本板实测通过的路数，不写成通用 ARM 能力。

## 6. 证据分层

```text
1. ARM64 交叉编译 + ELF/依赖检查：本仓库可完成（WSL2）
2. WSL2/QEMU 纯逻辑测试：环境允许时记录，不代表 GPU/linuxfb 实机通过
3. 真实板 QPA/GPU/framebuffer/温度/多路性能：由用户在目标板完成（第 5 节）
```
