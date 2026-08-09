# RtmpMonitor

RtmpMonitor 是一个使用 C++17、Qt 6 Widgets 和 FFmpeg 的多路 H.264/RTMP
监控客户端，目标平台为 Windows x86_64 和 Linux ARM64。当前版本支持在一次会话中
动态添加、重连和移除最多 16 路连接，并针对 16 路 720p30 软件解码引入了网络读取、
共享解码池和 UI 展示节奏三层解耦。

完整路线见[项目规划](docs/roadmap/project_plan.md)，四路版本的历史基线见
[首批四路播放](docs/weeks/week4/week4_multi_stream_playback.md)，当前实现和验收方法见
[16 路架构与验收](docs/weeks/week4/week4_sixteen_stream_validation.md)，本次变更和实测记录见
[Week 6 产品级 OpenGL 验证总览](docs/weeks/week6/week6_opengl_environment_and_validation.md)，
生产实现见[产品级视频渲染框架](docs/architecture/video_rendering_framework.md)。

## 当前状态

| 阶段 | 状态 | 结果 |
|---|:---:|---|
| Week 1：RTMP 链路 | 已完成 | nginx-rtmp、FFmpeg/ffplay 验证与排障 |
| Week 2：动态多宫格 | 已完成 | 0～16 路、拖拽交换、单路全屏 |
| Week 3：单路播放 | 已完成 | FFmpeg 拉流、H.264 解码、重连、最新帧背压 |
| Week 4A：四路播放 | 已完成 | 四路独立线程、故障隔离、人工实况验收 |
| Week 4B：动态 16 路 | 功能回归通过 | 动态连接、网络/解码解耦、8-worker 池、指标与验收脚本 |
| Week 5：状态与日志 | 已完成 | 统一设备状态、3 秒重连、用户事件、系统日志和独立审计日志 |
| 产品级视频渲染 | 正式门禁通过 | 单画布 YUV OpenGL、临时全屏画布、CPU 回退；CLI 默认已切换为 `auto` |
| Windows 16 路验收 | 600 秒 A/B 通过 | OpenGL CPU 降低 69.08%、显示 14.91 FPS；双屏最差流 P95 196 ms |
| 监控级显示 | 功能回归通过，当前负载待正式复测 | 普通紧凑网格、标题覆盖和 F11 监控墙；最终布局短测有一项 frame age 门禁超出 0.3 ms |
| Linux ARM64 | OpenGL 交叉构建门禁 | 主程序、EGL/ES3、生产渲染和测试均为 AArch64 ELF；真实 QPA/GPU/播放仍需目标盒子 |
| SRS Server 接入 | Windows+WSL2 最小链路通过 | 2026-08-09 独立复验了 VS Preset 构建、SRS 6.0.184、1935/回环 1985、Windows/WSL 双侧推拉流和安全停止；ARM 实机、真实摄像头仍待验证 |

> 2026-08-05 的四组 600 秒结果验证了产品 Renderer，但早于 2026-08-08 的最终
> 监控墙布局。当前布局已完成 Windows Debug CTest 12/12 和 120 秒快速对照；快速对照
> 中 OpenGL CPU 降低 91.66%、显示 14.914 FPS，但 latest frame age P95 为 52 ms，
> 超过 51.7 ms 门槛 0.3 ms，因此不能把旧长测重新表述成当前布局的正式认证。

## Git 与公开内容边界

仓库提交的是可复查、可复现且已经脱敏的项目资产。提交前应按下表区分：

| 可以提交 | 不得提交 |
|---|---|
| C++ 源码、头文件、CMake、QSS、测试和不含凭据的自动化脚本 | `out/`、`build/`、Visual Studio/CMake 用户缓存和编译产物 |
| 架构、指南、路线图、ADR、已验证且脱敏的项目记忆 | `docs/project_handoff.md`、`docs/local/` 和个人工作笔记 |
| 删除绝对用户目录、URL、PID 和原始日志后的汇总 JSON | 原始性能报告、截图、录屏、日志、dump、抓包和延迟采样 |
| `.env.example` 或 `*.example.conf` 中的明显占位值 | `.env`、`ov.conf`、`ovcli.conf`、API Key、Token、密码、私钥和客户端数据库 |
| localhost 测试地址、无鉴权的示例流名和可由参数覆盖的工具路径 | 含用户名、密码、Token 或签名参数的真实 RTMP/HTTP URL |
| 公开构建所需的小型文本配置和说明 | 测试视频、YUV/RGB 原始帧、FFmpeg/nginx 本机安装目录内容 |

自动化脚本中的 Qt、MSVC、FFmpeg 和 nginx 默认路径只是本机开发示例，均应通过参数覆盖；
不得把真实鉴权信息写入这些默认值。`docs/memory/` 只保存经源码、配置或测试验证的脱敏
事实；个人绝对路径、未经验证的推测和大段运行日志不能进入长期记忆。上述禁止项由
[`.gitignore`](.gitignore) 提供第一层保护，提交前仍必须检查 `git diff --cached`，不能只
依赖忽略规则。

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
- `--renderer <auto|opengl|cpu>`：选择渲染后端；默认 `auto`，GL 初始化失败时自动回退 CPU；可显式 `cpu` 诊断/回滚。
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
- 按 `F11` 进入沉浸式监控墙，隐藏窗口与应用 chrome；`Esc` 或再次按 `F11` 原样恢复。
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

解码后的 YUV 不再按 UI 视口缩放或提前转换为 RGB：

- `VideoFrame` 保留 YUV420P/NV12 plane、stride、PTS、time base、代次和颜色描述。
- 每路 `LatestFrameMailbox` 容量固定为 1；新帧覆盖旧帧，不产生逐帧 Qt 事件。
- 网格由一个画布以 15 FPS 合成；全屏使用不共享 GL 对象的临时画布并以 30 FPS 调度。
- OpenGL 使用持久 YUV 纹理和 `glTexSubImage2D`；CPU 后端作为自动 fallback 和显式诊断/回滚路径。

关闭时先同时中断全部网络线程和队列，再等待网络和各流解码任务退出，最后关闭共享
worker；不使用 `QThread::terminate()`。

## 构建

### Windows x86_64

要求 Visual Studio 2022、Qt 6.6.1 MSVC x64、CMake 3.21+，以及由 vcpkg
`x64-windows` 提供的 FFmpeg 8.1.2 LGPL 开发库。开发启动以 Visual Studio 为准：

1. 选择 `Qt-Debug` 配置预设；
2. 首次配置或修改 vcpkg 后执行“删除缓存并重新配置”；
3. 将 `rtmp_monitor.exe` 设为启动项并按 F5。

命令行只作为 Developer PowerShell 中的构建/测试入口：

```powershell
cmake --preset Qt-Debug --fresh
cmake --build out\build-windows-x64\debug
ctest --test-dir out\build-windows-x64\debug --output-on-failure
```

`CMakePresets.json` 的公共 Windows 基类通过 `VCPKG_ROOT` 定位 toolchain；本机
`CMakeUserPresets.json` 保存 Qt/vcpkg 的个人路径且不提交。普通 PowerShell 未初始化
MSVC 时使用 Developer PowerShell。不能混用 MinGW Qt 和 MSVC 产物，也不要把双击
未部署 Qt DLL 的 EXE 当作开发期标准启动方式。

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
| `rtmp_monitor_ui_smoke_test` | 样式、拖拽对象交换、临时全屏画布与恢复 |
| `rtmp_monitor_dynamic_grid_test` | 空状态、连接对话框、0～16 路、动态移除和右键入口 |
| `rtmp_monitor_ffmpeg_player_test` | URL、幂等停止、失败重连和有界退出 |
| `rtmp_monitor_multi_stream_test` | 16 个稳定 ID、解码池分配、故障隔离、批量退出和指标 JSON |
| `rtmp_monitor_logging_test` | 系统/审计分流、配置、脱敏、队列、轮转和清理 |
| `rtmp_monitor_user_message_test` | 大众文案映射、重复抑制和未来登录事件 |
| `rtmp_monitor_connection_controller_test` | 新增、删除、连接失败和手动重连的三通路集成 |
| `rtmp_monitor_log_panel_test` | 用户事件面板、暂停、清空、容量上限和安全状态文本 |
| `rtmp_monitor_opengl_windows_smoke` | Win32/WGL 隐藏上下文、GL 信息、清屏和缓冲交换 |
| `rtmp_monitor_qt_opengl_smoke` | `VideoRenderWidget` RGB 纹理上传、绘制和自动退出 |
| `rtmp_monitor_opengl_grid_renderer_smoke` | 生产 YUV420P Shader、纹理上传与 framebuffer 像素容差 |
| `rtmp_monitor_video_render_core_test` | 帧所有权、stride、颜色、邮箱并发、Dirty 与 contain/cover |

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

### 2026-08-04 Week 6 产品 OpenGL 结果

- Windows WGL 与 Qt OpenGL 冒烟目标均实际运行通过；GPU 为 NVIDIA GeForce
  RTX 3060 Laptop GPU，OpenGL 4.6.0 NVIDIA 591.86。
- Windows Debug 完整 CTest 12/12 通过，总耗时 75.48 秒；生产 framebuffer 8 个
  YUV420P/NV12、BT.601/709/2020 NCL、Limited/Full 用例全部通过，最低 PSNR
  46.0896 dB、最大 MAE 0.8889、最大 P99 通道误差 2。
- 用户录像所示“4 路已连接但主网格黑屏、进入全屏后才一起显示”已修复。根因是 Qt 6
  拒绝带 lambda 的 `Qt::UniqueConnection`，首帧没有刷新 Snapshot；现已改为成员槽并通过
  动态网格 29/29、完整 CTest 和四路无双击实机验证。
- WSL2 ARM64 sysroot 补齐 `libqt6opengl6-dev:arm64` 后，主程序、
  EGL/ES3 冒烟目标与生产 Qt OpenGL 渲染均通过 ELF64/AArch64 和动态依赖门禁。
- ARM64 结果仅为交叉编译与链接成功；真实 QPA、EGLFS/Wayland/X11、GPU 和视频
  运行仍待目标盒子验收。

这些结果证明当前功能路径和短窗口性能正常，不替代 10 分钟持续门槛，也不替代真实
ARM64 盒子的 QPA、VPU、网络和长期稳定性测试。

### 2026-08-05 Renderer 正式对照

- 16 路 CPU/OpenGL 各 600 秒：平均应用 CPU 4.85%→1.50%，降低 69.08%；平均显示
  12.74→14.91 FPS；frame age P95 46→43 ms，内部延迟 P95 42→40 ms。
- 双屏源到显示 CPU/OpenGL 各 600 秒：最差流 P95 214→196 ms，OpenGL 最大值
  317 ms；OpenGL UI 最大间隔 441 ms、纹理 22,118,400 字节且末 60 秒稳定。
- 8 个 framebuffer 质量用例全部通过；完整结果和正确解读见 Week 6 总览。全部硬门槛
  通过后 CLI 默认已由 `cpu` 切换为 `auto`，显式 CPU 回滚仍保留。

## RTMP Server（SRS 6.0.184）

当前基线 Server 为固定 tag `v6.0-r0` 源码构建的 SRS 6.0.184，作为独立基础设施
运行（Qt 客户端不拥有其进程）。Windows 开发机在 WSL2 Ubuntu 内构建运行；ARM
Linux 首选目标设备本机构建并由 systemd 管理；Docker 固定镜像为烟测/CI 备选。

```powershell
# 检查 / 启动 / 状态 / 快速推拉自测 / 停止（只管理脚本自有进程）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\srs\srs_dev_wsl.ps1 -Action Check
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\srs\srs_dev_wsl.ps1 -Action Start
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\srs\srs_dev_wsl.ps1 -Action Status
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\srs\srs_dev_wsl.ps1 -Action Test
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\srs\srs_dev_wsl.ps1 -Action Stop

# 端到端验收（推流激活、WSL/Windows 双侧 ffprobe、停推消失、同 URL 恢复）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\srs\verify_srs_chain.ps1 -Action Verify
```

最小配置为 `deploy/srs/conf/srs-minimal.conf`：1935 对所有接口推拉 RTMP，
1985 HTTP API 只绑定回环、RAW API 关闭，不启用 HLS/WebRTC/SRT/Callback。
客户端直接以现有 `--url` 拉流，例如
`rtmp_monitor --url rtmp://127.0.0.1:1935/live/camera01`。
新手从
[SRS 新手完全指南](docs/guides/build-and-testing/srs_beginner_guide.md)入手；
完整方案、ARM 部署与逐 Phase 验收见
[SRS Server 接入实施方案](docs/srs_server_integration_plan.md)和
[RTMP 链路验证 §15](docs/guides/build-and-testing/rtmp_chain_verification.md)；
历史 nginx-rtmp 脚本保留未删。

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
- [Week 6 产品级 OpenGL 渲染与验证总览](docs/weeks/week6/week6_opengl_environment_and_validation.md)
- [Week 6 产品视频渲染框架教学篇](docs/weeks/week6/week6_product_rendering_framework_tutorial.md)
- [Week 6 CPU/OpenGL 自动化对照测试实战篇](docs/weeks/week6/week6_renderer_performance_test_guide.md)
- [16 路动态连接、解码架构与验收](docs/weeks/week4/week4_sixteen_stream_validation.md)
- [Week 4 模块变更与测试操作记录](docs/weeks/week4/week4_release_test_and_module_changes.md)
- [四路历史基线](docs/weeks/week4/week4_multi_stream_playback.md)
- [Windows x64 与 Linux ARM64 构建](docs/guides/build-and-testing/cross_platform_build.md)
- [第三周播放器](docs/weeks/week3/week3_ffmpeg_player.md)
- [桌面端到端延迟基线](docs/weeks/week3/week3_desktop_latency_test.md)
- [代码规范](docs/guides/development/code_style_guide.md)
- [注释规范](docs/guides/development/comment_style_guide.md)
## 产品级视频渲染后端（2026-08-03）

生产显示链路已从解码线程内的 RGB `QImage` 转换切换为不可变 YUV `VideoFrame`、容量 1
最新帧邮箱和单画布合成。网格目标 15 FPS，全屏使用不共享 GLuint 的临时画布并以 30 FPS
调度；OpenGL 初始化失败时自动使用 CPU/QPainter 回退。

```powershell
.\out\build-windows-x64\debug\rtmp_monitor.exe --renderer=auto
.\out\build-windows-x64\debug\rtmp_monitor.exe --renderer=opengl
.\out\build-windows-x64\debug\rtmp_monitor.exe --renderer=cpu
```

Windows 基线为 OpenGL 3.3 Core，Linux ARM64 基线为 OpenGL ES 3.0。架构、PDF 映射、
Context 生命周期、指标 schema v3 和未完成实机门禁见
[视频渲染框架](docs/architecture/video_rendering_framework.md)及
[渲染架构 ADR](docs/architecture/adr/001-video-rendering-architecture.md)。
