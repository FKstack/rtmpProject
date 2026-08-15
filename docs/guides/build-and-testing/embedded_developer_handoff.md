# 嵌入式二次开发交接与设备环境模板

> 适用版本：RtmpMonitor `v0.1.0-alpha.1`
>
> 发布定位：Windows Development Preview；ARM Linux Engineering Preview
>
> 当前证据：Windows+WSL2 最小 SRS 链路已独立复验；4/16 路与 600 秒结果属于
> 历史测试证据；ARM 真机、真实摄像头和网页预览现场结果均为 `[需要验证]`。

本文供嵌入式接收方在开始二次开发前填写。未知信息必须写 `[需要验证]`，不得用
WSL2 交叉构建、QEMU 或其他板卡的数据代替目标设备事实。

## 1. 接收范围与现有入口

本轮交付源码、CMake、Windows/Linux ARM64 统一开发入口、底层构建/验证脚本、SRS
最小配置和文档；不提交第三方 `.lib/.dll/.so`，也不把内部静态库承诺为稳定 SDK/ABI。

开始前阅读：

1. [Windows x64 与 Linux ARM64 跨平台构建](cross_platform_build.md)
2. [Linux 双路径渲染构建与板级资格](linux_dual_render_build.md)
3. [SRS 新手完全指南](srs_beginner_guide.md)
4. [SRS 异常恢复与发布门禁](srs_failure_recovery.md)

现有入口：

| 目的 | 仓库入口 |
| --- | --- |
| Windows 检查/依赖/构建/CTest | `scripts/setup_windows_dev.ps1` |
| Linux ARM64 检查/安装/双模式门禁 | `scripts/setup_linux_arm64_dev.sh` |
| Windows FFmpeg 底层安装 | `scripts/setup_ffmpeg_windows_dev.ps1` |
| ARM64 sysroot/Qt 底层安装 | `scripts/setup_arm64_build_env.sh` |
| ARM64 FFmpeg/OpenGL 检查 | `scripts/setup_linux_arm64_dev.sh --action test --render-mode both` |
| RASTER/GLES3 双路径构建 | `CMakePresets.json` 与 `linux_dual_render_build.md` |
| SRS ARM 构建与链路验证 | `scripts/srs/build_srs_arm64.sh`、`scripts/srs/verify_srs_chain.sh` |
| 板级最大路数资格 | `scripts/qualify_embedded_device.sh` |

## 2. 目标板硬件档案

| 字段 | 接收方填写 |
| --- | --- |
| 厂商与板卡型号/硬件版本 | `[需要验证]` |
| SoC/CPU 型号、架构、核心数 | `[需要验证]` |
| 内存容量与可用内存 | `[需要验证]` |
| GPU 型号与驱动版本 | `[需要验证]` |
| VPU/硬件解码器与厂商接口 | `[需要验证]` |
| 存储类型、容量和可用空间 | `[需要验证]` |
| 网口/Wi-Fi、链路速率与目标拓扑 | `[需要验证]` |
| 温度传感器位置与降频阈值 | `[需要验证]` |

## 3. 系统、工具链与 ABI 档案

| 字段 | 接收方填写 |
| --- | --- |
| Linux 发行版、镜像版本/校验值 | `[需要验证]` |
| 内核版本与厂商补丁 | `[需要验证]` |
| glibc 或 musl 及版本 | `[需要验证]` |
| ELF 动态加载器路径 | `[需要验证]` |
| libstdc++/libgcc 版本 | `[需要验证]` |
| 厂商 SDK/sysroot 名称与版本 | `[需要验证]` |
| C/C++ 编译器、版本和 target triple | `[需要验证]` |
| CMake、Ninja/Make 版本 | `[需要验证]` |
| Qt 版本、构建参数和目标 ABI | `[需要验证]` |
| FFmpeg 版本、许可证和 configure 参数 | `[需要验证]` |
| SRS 版本、构建模式（native/cross） | `[需要验证]` |

必须在目标板记录以下命令结果摘要，不把大段原始输出提交到仓库：

```bash
uname -a
getconf GNU_LIBC_VERSION || true
file ./rtmp_monitor
readelf -h ./rtmp_monitor
readelf -l ./rtmp_monitor
ldd ./rtmp_monitor
```

## 4. 图形、显示与输入档案

| 字段 | 接收方填写 |
| --- | --- |
| 实际 QPA：linuxfb/eglfs/Wayland/X11 | `[需要验证]` |
| Qt platform plugin 路径与依赖 | `[需要验证]` |
| EGL vendor/version | `[需要验证]` |
| OpenGL ES version、renderer、shader language | `[需要验证]` |
| 屏幕分辨率、刷新率、方向和色深 | `[需要验证]` |
| 鼠标、键盘、触摸设备与校准方式 | `[需要验证]` |
| 字体包、中文显示和 fallback 字体 | `[需要验证]` |
| 窗口系统/合成器及全屏恢复行为 | `[需要验证]` |

Renderer 选择规则：

- 没有可靠 ES 3.0 Context/Shader/FBO：使用 `RASTER`，不得强制 OpenGL。
- GLES3 冒烟和真实 framebuffer 验证通过：可使用 `GLES3`。
- EGLFS 单屏全屏复用主画布，不创建第二个 OpenGL 顶层窗口。

## 5. 摄像头与码流档案

每一种摄像头配置分别填写，不要把实验室合成流当成真实摄像头结论。

| 字段 | 接收方填写 |
| --- | --- |
| 摄像头厂商/型号/固件 | `[需要验证]` |
| 视频编码与 profile/level | `[需要验证]` |
| 分辨率与帧率 | `[需要验证]` |
| 平均/峰值码率与码率控制方式 | `[需要验证]` |
| GOP、IDR 周期、B 帧 | `[需要验证]` |
| SPS/PPS 发送方式与时间戳行为 | `[需要验证]` |
| 音频编码、采样率、声道和码率 | `[需要验证]` |
| RTMP app/streamKey 规则 | `[需要验证]` |
| 摄像头到 SRS 的 LAN 路径 | `[需要验证]` |

## 6. 构建、部署与资格结果

| 门禁 | RASTER | GLES3 |
| --- | --- | --- |
| configure/build | `[需要验证]` | `[需要验证]` |
| AArch64 ELF 与解释器 | `[需要验证]` | `[需要验证]` |
| 依赖均来自目标 sysroot/设备 | `[需要验证]` | `[需要验证]` |
| QPA 启动、中文、输入、全屏 | `[需要验证]` | `[需要验证]` |
| 一路真实 RTMP 播放 | `[需要验证]` | `[需要验证]` |
| 断网、停推、SRS/应用重启恢复 | `[需要验证]` | `[需要验证]` |
| 1/2/4/8/16 路逐级资格 | `[需要验证]` | `[需要验证]` |
| CPU、内存、FPS、帧龄、温度 | `[需要验证]` | `[需要验证]` |
| 长时间稳定性与最终资源状态 | `[需要验证]` | `[需要验证]` |

运行 `scripts/qualify_embedded_device.sh` 后记录：

```text
测试日期：             [需要验证]
测试 commit/tag：      [需要验证]
Renderer：             [需要验证]
测试流参数：           [需要验证]
门槛配置：             [需要验证]
recommendedMaxStreams: [需要验证]
失败原因/停止档位：    [需要验证]
```

## 7. 交接完成条件与安全边界

- 只对已填写的板卡型号、镜像、依赖、Renderer 和最大路数作出承诺。
- 交叉构建只证明编译/链接边界，不证明 QPA、GPU/VPU、真实播放或性能。
- `.env`、密码、Token、私钥、完整鉴权 URL、生产摄像头配置和原始日志不得入库。
- 个人 Qt、SDK、sysroot 绝对路径只写在本机忽略配置或接收方私有档案中。
- 无法复现的步骤、依赖差异和文档问题应形成可审查 Issue，不只保留在聊天记录。
