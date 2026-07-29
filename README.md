# RtmpMonitor

RtmpMonitor 是一个使用 C++17、Qt 6 Widgets 和 FFmpeg 的多路 H.264/RTMP
监控客户端，目标平台为 Windows x86_64 和 Linux ARM64。当前版本支持在一次会话中
动态添加、重连和移除最多 16 路连接，并针对 16 路 720p30 软件解码引入了网络读取、
共享解码池和 UI 展示节奏三层解耦。

完整路线见[项目规划](docs/roadmap/project_plan.md)，四路版本的历史基线见
[首批四路播放](docs/weeks/week4/week4_multi_stream_playback.md)，当前实现和验收方法见
[16 路架构与验收](docs/weeks/week4/week4_sixteen_stream_validation.md)，本次变更和实测记录见
[Week 6 OpenGL 环境与原型验证](docs/weeks/week6/week6_opengl_environment_and_validation.md)。

## 当前状态

| 阶段 | 状态 | 结果 |
|---|:---:|---|
| Week 1：RTMP 链路 | 已完成 | nginx-rtmp、FFmpeg/ffplay 验证与排障 |
| Week 2：动态多宫格 | 已完成 | 0～16 路、拖拽交换、单路全屏 |
| Week 3：单路播放 | 已完成 | FFmpeg 拉流、H.264 解码、重连、最新帧背压 |
| Week 4A：四路播放 | 已完成 | 四路独立线程、故障隔离、人工实况验收 |
| Week 4B：动态 16 路 | 功能回归通过 | 动态连接、网络/解码解耦、8-worker 池、指标与验收脚本 |
| Week 5：状态与日志 | 已完成 | 统一设备状态、3 秒重连、用户事件、系统日志和独立审计日志 |
| Week 6：OpenGL 原型 | 双平台门禁通过 | 保留 QPainter/QImage 默认路径；Windows 实际运行，ARM64 完成交叉链接与 ELF 检查 |
| Windows 16 路验收 | 短窗口通过 | 视频故障隔离与 30 秒双屏实况通过；10 分钟性能资格测试仍需执行 |
| Linux ARM64 | OpenGL 交叉构建门禁 | 主程序、EGL/GLES2 与 Qt OpenGL 原型均为 AArch64 ELF；真实 QPA/GPU/播放仍需目标盒子 |

## 使用方式

普通启动时不自动连接摄像头，而是显示中央“添加新的连接”按钮：

```powershell
.\out\build-windows-x64\debug\rtmp_monitor.exe
```

连接对话框要求非空且当前会话内唯一的设备名称，以及合法的 `rtmp://` URL。
成功提交后立即创建视频格并异步连接，不会在对话框中阻塞网络探测。视频格右键菜单
支持“重新连接”和“断开并移除”。

测试或无人值守启动可重复传入 1～16 个 `--url`。第 N 个地址绑定 `Camera NN`：

```powershell
.\out\build-windows-x64\debug\rtmp_monitor.exe `
  --decode-threads 8 `
  --metrics-file .\out\metrics.json `
  --url rtmp://127.0.0.1:1935/live/camera001 `
  --url rtmp://127.0.0.1:1935/live/camera002
```

参数：

- `--url <rtmp-url>`：可重复 1～16 次；超过 16 次会在创建窗口前报错。
- `--decode-threads <1..16>`：共享解码 worker 数；默认取逻辑核心数的一半并限制为
  1～8。
- `--metrics-file <path>`：每秒原子写入不含 URL 的 JSON 指标。
- `--latency-marker`：仅测试时解析双屏脚本的时间标记；正常运行不要启用。
- `--max-reconnect-failures <0..1000>`：最大连续失败次数；默认 `0` 表示无限重试。
- `--log-level <trace|debug|info|warning|error|critical>`：覆盖最低系统日志级别。
- `--log-dir <path>`：覆盖轮转日志目录；默认使用系统应用数据目录。
- `--log-config <path>`：覆盖默认日志 INI 配置文件路径。

## 动态 UI

- 0 路时显示空状态页；工具栏和中央按钮均可打开连接对话框。
- 连接成功后动态创建真实 `VideoWidget`；最多 16 路。
- 名称和 URL 在当前会话中不能完全重复。
- 拖拽移动完整控件对象。播放器、标题、状态、指标和画面通过稳定 `StreamId`
  绑定，不依赖网格位置。
- 双击任意一路进入全屏；`Esc`、再次双击或控制栏按钮退出。
- 右键“重新连接”只影响该路；“断开并移除”停止该路后动态重排。

| 路数 | 布局 |
|---:|:---:|
| 0 | 空状态 |
| 1 | 1×1 |
| 2 | 1×2 |
| 3～4 | 2×2 |
| 5～6 | 2×3 |
| 7～9 | 3×3 |
| 10～12 | 3×4 |
| 13～16 | 4×4 |

## 设备状态、重连与日志

每一路使用同一个 `DeviceStatus` 状态机：

```text
Disconnected -> Connecting -> Playing
                         \-> Error -> Reconnecting -> Connecting
```

连接或读流失败后默认每 3 秒重试。收到有效视频并恢复播放后，连续失败计数清零；
设置非零 `--max-reconnect-failures` 后，达到上限的单路停留在 `Error`，可以通过视频格
右键“重新连接”重新启动，不影响其他设备。

主窗口底部“事件消息”面板只显示普通用户可以理解的连接、新增、删除和操作失败
提示，可从“视图 → 事件消息”关闭或恢复。技术错误不会进入该面板。

本地日志使用两套独立 JSON Lines 文件：

- `system.jsonl`：开发和运维诊断，受系统日志等级控制；
- `audit.jsonl`：记录操作者、动作、对象和结果，不受系统日志等级影响。

两类文件均由独立有界队列异步写入，并分别执行大小轮转、历史数量、保留天数和
总空间限制。URL、消息及结构化字段中的账号、密码、Token、私钥和末级流密钥会在
入队前统一脱敏。完整设计和配置见
[日志体系架构](docs/guides/architecture/logging_architecture.md)。

## 播放架构

每一路保留独立的阻塞式 RTMP/解复用 `QThread`，因此一个地址的 DNS、连接、读取
或重连不会占用通用解码 worker，也不会阻塞其他流。网络线程只把带会话代次和接收
时间的压缩包放入有界队列。

所有流共享 `DecodeWorkerPool`：

- 默认最多 8 个 worker，同一路固定分配到一个 worker。
- 每个 `AVCodecContext` 只在其固定 worker 上执行，FFmpeg 内部解码线程数为 1。
- 每轮最多处理 4 个包或 5 ms，然后重新排队，防止单路独占。
- 每路队列上限为 45 包或 4 MiB；溢出时清理积压、flush 解码器并等待下一关键帧。

解码后的 YUV 仅按展示节奏转换为 RGB888：

- 网格：最高 15 FPS、最大 640×360。
- 全屏：最高 30 FPS、最大 1280×720，且不超过源分辨率。
- UI 使用统一定时器轮询每路最新帧邮箱；每路最多保留一张待展示图。

关闭时先同时中断全部网络线程和队列，再等待网络和各流解码任务退出，最后关闭共享
worker；不使用 `QThread::terminate()`。

## 构建

### Windows x86_64

要求 Visual Studio 2022、Qt 6.6.1 MSVC x64、CMake 3.21+，以及与当前工程匹配的
FFmpeg 8.1.2 LGPL 开发库。已配置当前机器的预设时：

```powershell
cmake -S . -B out\build-windows-x64\debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DBUILD_TESTING=ON `
  -DCMAKE_PREFIX_PATH=E:\QT6\6.6.1\msvc2019_64
cmake --build out\build-windows-x64\debug
ctest --test-dir out\build-windows-x64\debug -C Debug --output-on-failure
```

普通 PowerShell 未初始化 MSVC 时，先执行 Visual C++ 环境脚本，或在 Developer
PowerShell 中构建。不能混用 MinGW Qt 和 MSVC 产物。

OpenGL 环境、WGL/Qt 运行和完整回归：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_week6_opengl.ps1
```

该脚本把临时目录、应用数据和报告重定向到 `out/week6-opengl`，并拒绝 C 盘可写
输出路径；只读使用已安装的 Windows SDK。

### Linux ARM64

在 WSL2 Ubuntu 22.04 或 Linux 构建机上：

```bash
bash scripts/verify_ffmpeg_arm64_env.sh
sudo bash scripts/setup_arm64_build_env.sh
bash scripts/verify_opengl_arm64_env.sh
cmake --preset Linux-ARM64-Debug
cmake --build --preset Linux-ARM64-Debug
file out/build-linux-arm64/debug/rtmp_monitor
aarch64-linux-gnu-readelf -h out/build-linux-arm64/debug/rtmp_monitor
```

交叉构建只能证明二进制和依赖 ABI 正确，不能代替目标盒子的 QPA、窗口系统、VPU、
网络、持续播放和性能测试。

## 自动测试

当前 CTest 目标：

| 目标 | 主要覆盖 |
|---|---|
| `rtmp_monitor_ui_smoke_test` | 样式、拖拽对象交换、全屏转移与恢复 |
| `rtmp_monitor_dynamic_grid_test` | 空状态、连接对话框、0～16 路、动态移除和右键入口 |
| `rtmp_monitor_ffmpeg_player_test` | URL、幂等停止、失败重连和有界退出 |
| `rtmp_monitor_multi_stream_test` | 16 个稳定 ID、解码池分配、故障隔离、批量退出和指标 JSON |
| `rtmp_monitor_logging_test` | 系统/审计分流、配置、脱敏、队列、轮转和清理 |
| `rtmp_monitor_user_message_test` | 大众文案映射、重复抑制和未来登录事件 |
| `rtmp_monitor_connection_controller_test` | 新增、删除、连接失败和手动重连的三通路集成 |
| `rtmp_monitor_log_panel_test` | 用户事件面板、暂停、清空、容量上限和安全状态文本 |
| `rtmp_monitor_opengl_windows_smoke` | Win32/WGL 隐藏上下文、GL 信息、清屏和缓冲交换 |
| `rtmp_monitor_qt_opengl_smoke` | `VideoRenderWidget` RGB 纹理上传、绘制和自动退出 |

可选真实流 CTest 使用分号分隔的 16 个地址：

```powershell
$env:RTMP_MONITOR_TEST_URLS = (
  1..16 | ForEach-Object {
    "rtmp://127.0.0.1:1935/live/camera{0:D3}" -f $_
  }
) -join ";"
ctest --test-dir out\build-windows-x64\debug -C Debug `
  -R rtmp_monitor_multi_stream_test --output-on-failure
```

## 16 路验收脚本

所有长驻测试进程均由脚本后台启动。视频脚本把 stdout/stderr 保存到
`out/logs/16-stream-video/`，把经过 PID、路径、进程名和启动时间校验的记录保存到
`out/runtime/16-stream-video/pids.json`。测试产物均被 Git 忽略。

### 预录视频功能与性能

在独立的 `powershell.exe -NoProfile` 外部终端中运行：

```powershell
$script = ".\scripts\test_16_stream_video.ps1"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script -Action Check
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script -Action Prepare
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script -Action Start
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script -Action Status
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script -Action Test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $script -Action Stop
```

`Start` 只提交一个隐藏的、带唯一运行编号的启动器并立即返回；nginx、16 个 FFmpeg
publisher 和客户端由启动器在后台创建，stdout/stderr 分别写入 `out/logs`，PID 写入
`out/runtime`。本机实测 `Start` 在 8 秒内返回。随后用 `Status` 查看启动器的
`starting/succeeded/failed` 结果；`Test` 的健康循环最多 55 秒，每 5 秒报告一次进度。
失败时脚本打印相关日志最后 100 行并返回非零。

完整 10 分钟测试：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_video.ps1 `
  -Action RunAutomated -DurationSeconds 600 -WarmupSeconds 20
```

### 双屏实况延迟

要求 Windows 扩展桌面；副屏为 1536×864 源时钟，主屏为应用和参考时钟：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_live_latency.ps1 -Action Check

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_live_latency.ps1 -Action Start

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_live_latency.ps1 -Action Status

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_live_latency.ps1 -Action Capture

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_live_latency.ps1 -Action Stop
```

`Start` 同样通过隐藏启动器后台提交源时钟、参考时钟、单个 FFmpeg tee publisher 和
客户端，本机实测约 5 秒返回。完整 10 分钟报告使用：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_live_latency.ps1 `
  -Action RunAutomated -DurationSeconds 600 -WarmupSeconds 20
```

总控入口：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run_16_stream_automated_tests.ps1 `
  -Action Run -Suite All
```

可选套件为 `Unit`、`Video`、`LiveLatency`、`All`。详细门槛、报告字段、手工操作和
结果表见[16 路架构与验收](docs/weeks/week4/week4_sixteen_stream_validation.md)。

### 2026-07-28 本机短窗口结果

- Windows Debug CTest：Week 5 完成后 6/6 通过，总耗时 52.93 秒。
- 预录视频：16/16 publisher、16/16 playing、8 workers；解码约
  29.3～30.3 FPS，展示约 10.1～13.1 FPS，队列为 0；停止 Camera 03 仅该路重连，
  恢复后重新达到 16/16，最终无残留客户端。
- 双屏实况 30 秒：16/16 路均有延迟样本；逐路 P95 为 169～177 ms，最大样本
  275 ms，应用平均 CPU 43.3%，峰值工作集 194.7 MiB，UI 最大间隔 169 ms。
- Linux ARM64：主程序和四个测试目标均重新生成 ELF64/AArch64。

### 2026-07-29 Week 6 OpenGL 结果

- Windows WGL 与 Qt OpenGL 冒烟目标均实际运行通过；GPU 为 NVIDIA GeForce
  RTX 3060 Laptop GPU，OpenGL 4.6.0 NVIDIA 591.86。
- Windows Debug 完整 CTest 10/10 通过，总耗时 75.25 秒。
- WSL2 ARM64 sysroot 补齐 `libqt6opengl6-dev:arm64` 后，主程序、
  EGL/GLES2 冒烟目标与 Qt OpenGL 原型均通过 ELF64/AArch64 和动态依赖门禁。
- ARM64 结果仅为交叉编译与链接成功；真实 QPA、EGLFS/Wayland/X11、GPU 和视频
  运行仍待目标盒子验收。

这些结果证明当前功能路径和短窗口性能正常，不替代 10 分钟持续门槛，也不替代真实
ARM64 盒子的 QPA、VPU、网络和长期稳定性测试。

## 性能验收门槛

以当前 Windows 11、Ryzen 7 6800H、约 16 GiB 内存、16 路 H.264
1280×720@30、GOP 30、YUV420P、无 B 帧为基线，预热后连续 10 分钟：

- 至少 95% 采样窗口中，16 路最低解码 FPS ≥27。
- 网格展示保持 12～15 FPS。
- 每路源到显示延迟 P95 ≤750 ms，单样本最大值 ≤1.5 s。
- `rtmp_monitor` 平均 CPU ≤85%，工作集峰值 ≤2 GiB。
- 队列超过 75% 不得连续超过 5 秒，延迟不得持续增长。
- Camera 03 停止后其余 15 路继续播放，恢复后 8 秒内重新出帧。
- UI 最大调度卡顿 <500 ms。
- 健康、部分重连、全部失败三种状态下均在 5 秒内关闭。

## 仓库边界

可提交：源码、头文件、CMake、测试、`scripts/` 和 `docs/`。

不可提交：`out/`、构建目录、测试素材、日志、PID、指标报告、本机路径配置、FFmpeg/
Qt/nginx 二进制、凭据和 `docs/project_handoff.md`。提交前运行：

```powershell
git status --short
git diff --check
```

## 文档

- [文档分类索引](docs/README.md)
- [项目规划](docs/roadmap/project_plan.md)
- [Week 4～5 代码框架、维护与深入学习指南](docs/guides/architecture/week4_week5_architecture_guide.md)
- [Week 4 多路媒体与并发深度学习](docs/guides/architecture/week4_media_concurrency_deep_dive.md)
- [用户事件、系统日志与审计日志架构](docs/guides/architecture/logging_architecture.md)
- [Week 4 新对话公开交接](docs/weeks/week4/week4_conversation_handoff.md)
- [Week 5 设备状态、日志与重连](docs/weeks/week5/week5_device_status_and_logging.md)
- [Week 6 OpenGL 环境与原型验证](docs/weeks/week6/week6_opengl_environment_and_validation.md)
- [16 路动态连接、解码架构与验收](docs/weeks/week4/week4_sixteen_stream_validation.md)
- [Week 4 模块变更与测试操作记录](docs/weeks/week4/week4_release_test_and_module_changes.md)
- [四路历史基线](docs/weeks/week4/week4_multi_stream_playback.md)
- [Windows x64 与 Linux ARM64 构建](docs/guides/build-and-testing/cross_platform_build.md)
- [第三周播放器](docs/weeks/week3/week3_ffmpeg_player.md)
- [桌面端到端延迟基线](docs/weeks/week3/week3_desktop_latency_test.md)
- [代码规范](docs/guides/development/code_style_guide.md)
- [注释规范](docs/guides/development/comment_style_guide.md)
