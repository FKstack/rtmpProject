# Week 4：新对话公开交接文档

> 文档分类：Week 4 实现与交接。

> 实现基线：`25b88b1`
> 基线提交：`feat: add dynamic 16-stream playback and validation`
> 文档用途：让新的开发对话快速了解当前实现、验证状态、遗留工作和优化顺序。

## 1. 首先要知道的结论

1. Week 1～3 已完成 RTMP 链路、动态视频网格和单路 FFmpeg 播放。
2. Week 4 已从四路播放基线扩展为 0～16 路动态连接。
3. 普通启动不自动连接摄像头，而是显示“添加新的连接”空状态页。
4. 命令行重复传入 1～16 个 `--url` 时，可一次预装对应数量的连接。
5. 每路拥有独立 RTMP/解复用线程；16 路共享按流亲和的固定解码 worker 池。
6. Windows 自动回归、16 路视频短窗口和 30 秒双屏实况测试已经通过。
7. Linux ARM64 已通过交叉构建门禁，但没有在真实 ARM64 盒子上完成播放和性能验收。
8. 600 秒视频与实况性能资格测试尚未执行，不能宣称“全部性能门槛已通过”。
9. 在获得长测数据前，不应直接引入 OpenGL、D3D11VA 或 ARM64 VPU。

## 2. 新对话建议阅读顺序

1. 本文：当前事实、未完成事项和下一步命令。
2. [README](../../../README.md)：构建、启动、脚本入口和仓库边界。
3. [项目计划](../../roadmap/project_plan.md)：整体路线和后续阶段。
4. [Week 4 模块与测试记录](week4_release_test_and_module_changes.md)：本轮实际改动和
   短窗口测试数据。
5. [16 路架构与验收](week4_sixteen_stream_validation.md)：线程、队列、指标、人工验收
   和性能硬门槛。
6. [四路历史基线](week4_multi_stream_playback.md)：从单路扩展到四路时的历史设计。
7. [跨平台构建](../../guides/build-and-testing/cross_platform_build.md)：Windows x64
   和 Linux ARM64 构建边界。

`docs/project_handoff.md` 是被 Git 忽略的本地材料，可能仍包含旧版本信息。新的对话应
以本文、当前代码、README 和 Git 状态为准。

## 3. Git 与仓库基线

生成本文前的已验证状态：

```text
master         -> 25b88b1
week4          -> 25b88b1
origin/master  -> 25b88b1
origin/week4   -> 25b88b1
```

新对话开始时先执行：

```powershell
git status --short --branch
git log -5 --oneline --decorate
```

如果提交号已经变化，应先阅读新提交和工作区差异，不要直接假设本文仍完整描述最新
状态。工作区中已有的未提交修改属于用户，必须保留并逐项辨析。

## 4. 当前用户功能

### 4.1 动态连接

- 0 路时显示中央“添加新的连接”按钮，工具栏有同名入口。
- 连接对话框填写设备名称和 `rtmp://` URL。
- 默认名称使用最小未占用的 `Camera NN`。
- 名称不能为空；名称和 URL 在当前会话内不能重复。
- 对话框只做本地校验，提交后异步连接，不阻塞 UI。
- 最多允许 16 路。

### 4.2 视频格操作

- 视频格根据数量在空状态、1×1 至 4×4 之间动态布局。
- 右键“重新连接”只重启当前流。
- 右键“断开并移除”停止该流并动态重排。
- 拖拽交换完整 `VideoWidget`，标题、状态和画面一起移动。
- 双击进入单路全屏；`Esc`、再次双击或控制栏按钮退出。
- 播放器与控件使用稳定 `StreamId` 绑定，不依赖当前网格索引。

### 4.3 命令行

```powershell
.\out\build-windows-x64\debug\rtmp_monitor.exe `
  --decode-threads 8 `
  --metrics-file .\out\metrics.json `
  --url rtmp://127.0.0.1:1935/live/camera001
```

- `--url`：可重复 1～16 次；不提供时显示空状态页。
- `--decode-threads`：范围 1～16；默认取逻辑核心数一半并限制为 1～8。
- `--metrics-file`：每秒原子输出不含 URL 的 JSON 指标。
- `--latency-marker`：仅供双屏测试脚本使用，正常运行不要启用。

## 5. 当前播放架构

```text
每路 RTMP/解复用 QThread
        |
        v
带 session-id 和接收时间的有界压缩包队列
        |
        v
按流亲和的共享 DecodeWorkerPool
        |
        v
按展示节奏执行 YUV -> RGB888
        |
        v
每路单帧邮箱
        |
        v
UI 统一定时器 -> VideoWidget
```

### 5.1 网络与故障隔离

- 每路独立执行阻塞式 `avformat_open_input` 和 `av_read_frame`。
- 网络线程不占用通用解码 worker。
- 单路断线、超时和退避重连不阻塞其他流。
- 停止时通过 FFmpeg interrupt callback 中断网络调用，不使用强制终止线程。

### 5.2 解码池与公平性

- 当前 16 逻辑线程开发机默认使用 8 个解码 worker。
- 同一流固定分配到同一个 worker，保证 `AVCodecContext` 不并发访问。
- FFmpeg 解码器内部线程数设为 1，避免 16 个解码器重复扩张线程。
- 每次最多处理 4 个包或运行 5 ms，然后重新排队。

### 5.3 背压

- 每路最多保存 45 个压缩包或 4 MiB。
- 达到任一上限后清理积压、标记不连续并等待下一关键帧。
- 解码器在不连续恢复时执行 flush。
- 解码后只保留一张待展示图，旧图直接覆盖。

### 5.4 展示节奏

- 网格模式：最高 15 FPS，转换尺寸最大 640×360。
- 全屏模式：最高 30 FPS，转换尺寸最大 1280×720。
- 输出不会放大超过源分辨率。
- UI 线程只轮询最新帧邮箱，不接收无限排队的逐帧信号。

### 5.5 关闭顺序

1. 同时请求全部网络线程停止。
2. 停止并唤醒全部压缩包队列。
3. 等待网络线程和各流当前解码任务退出。
4. 关闭共享解码 worker。
5. 不调用 `QThread::terminate()`。

## 6. 主要模块职责

- `StreamConnectionController`：连接对话框、命令行预装和 UI/媒体绑定。
- `ConnectionDialog`：名称与 RTMP URL 输入、默认值和本地校验。
- `MultiStreamPlaybackManager`：动态流生命周期、稳定 ID、指标和 UI 帧调度。
- `FFmpegPlayer`：单路网络、包队列、解码状态和最新帧邮箱。
- `DecodeWorkerPool`：固定 worker、按流亲和和公平任务调度。
- `PlaybackTypes`：`StreamId`、连接信息、展示目标、帧和指标数据类型。
- `VideoGridWidget`：0～16 路动态布局、拖拽和移除。
- `VideoWidget`：单路画面、状态、右键动作和全屏入口。

媒体层当前主要公开接口包括：

```cpp
StreamId addStream(const QString &displayName, const QString &rtmpUrl);
bool removeStream(StreamId streamId);
bool restartStream(StreamId streamId);
void startStream(StreamId streamId);
void stopStream(StreamId streamId);
void startAll();
void stopAll();
void setPresentationTarget(
    StreamId streamId,
    const QSize &viewportSize,
    bool fullscreen
);
StreamMetrics streamMetrics(StreamId streamId);
```

后续扩展应继续使用 `StreamId`，不要重新用网格索引作为业务身份。

## 7. 当前自动测试

CTest 目标：

| 目标 | 覆盖内容 |
|---|---|
| `rtmp_monitor_ui_smoke_test` | 样式、拖拽和全屏 |
| `rtmp_monitor_dynamic_grid_test` | 空状态、连接对话框、0～16 路和动态移除 |
| `rtmp_monitor_ffmpeg_player_test` | 播放器生命周期、重连和有界退出 |
| `rtmp_monitor_multi_stream_test` | 稳定 ID、解码池、故障隔离、指标和批量退出 |

Windows 回归命令：

```powershell
ctest --test-dir out\build-windows-x64\debug `
  -C Debug --output-on-failure
```

已记录结果：4/4 通过，总耗时 22.00 秒。

Linux ARM64 门禁：

```bash
cmake --build --preset Linux-ARM64-Debug --parallel 8
file out/build-linux-arm64/debug/rtmp_monitor
readelf -h out/build-linux-arm64/debug/rtmp_monitor
```

主程序和四个测试目标均已生成为 ELF64/AArch64。该结果不能替代真实 ARM64 设备运行。

## 8. 16 路测试脚本

### 8.1 预录视频

脚本：`scripts/test_16_stream_video.ps1`

主要 Action：

```text
Check
Prepare
Start
Status
Test
StopStream / StartStream
StopStreams / StartStreams
StartApp
RunAutomated
Stop
```

基本操作：

```powershell
$s = ".\scripts\test_16_stream_video.ps1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action Check
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action Prepare
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action Start
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action Status
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action Test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action Stop
```

已记录的短窗口结果：

- 16/16 publisher、16/16 playing、8 workers。
- 解码约 29.3～30.3 FPS。
- 网格展示约 10.1～13.1 FPS。
- Camera 03 停止只影响该路，恢复后重新达到 16/16。
- 最终无测试客户端残留。

展示 FPS 的部分短窗口低于正式 12～15 FPS 门槛，必须由 600 秒报告判断是否是稳定
问题，不能只根据该快照直接重构渲染层。

### 8.2 双屏实况

脚本：`scripts/test_16_stream_live_latency.ps1`

主要 Action：

```text
Check
Start
Status
Capture
RunAutomated
Stop
```

已记录的 30 秒结果：

| 指标 | 结果 |
|---|---:|
| 播放 | 16/16 |
| 有延迟样本 | 16/16 |
| 每路 P50 | 111～117 ms |
| 每路 P95 | 169～177 ms |
| 最大样本 | 275 ms |
| 应用平均 CPU | 43.3% |
| 峰值工作集 | 194.7 MiB |
| UI 最大间隔 | 169 ms |

### 8.3 脚本进程约束

- nginx、FFmpeg、时钟和客户端必须通过 `Start-Process` 后台启动。
- 外层 `Start` 只提交隐藏的唯一 launcher，本机验证在 10 秒内返回。
- stdout/stderr 分别写入被忽略的 `out/logs`。
- PID 和启动时间记录写入被忽略的 `out/runtime`。
- `Test` 健康检查最多 55 秒，并至少每 5 秒输出进度。
- 不使用 `Start-Process -Wait`、`Wait-Process`、`Read-Host` 或无截止循环。
- `Stop` 只清理 PID、路径、进程名和启动时间全部匹配的本次测试进程。
- 失败时保留日志并输出最后 100 行。

新对话不得在前台直接启动 nginx、FFmpeg、播放器或其他长驻服务。

## 9. 尚未完成的验收

### 9.1 必须先完成

- 600 秒预录视频性能资格测试。
- 600 秒双屏实况延迟资格测试。
- Camera 12 右键重连、移除和通过对话框重新添加。
- Camera 01 与 Camera 16 拖拽交换。
- Camera 08 与 Camera 16 全屏、退出和展示帧率变化。
- 健康、Camera 03 重连、全部流失败三种状态的 5 秒关闭计时。

### 9.2 目标硬件

- 在真实 Linux ARM64 盒子验证 Qt QPA 插件。
- 验证 Wayland、X11 或 EGLFS 中设备实际采用的一种。
- 验证 16 路 RTMP 网络、软件解码、显示和长期稳定性。
- 记录 CPU、内存、温度、帧率、延迟和 VPU 可用性。

## 10. 600 秒性能门槛

预热后连续运行 10 分钟：

| 指标 | 门槛 |
|---|---:|
| 解码 FPS | 每路至少 95% 采样窗口 ≥27 |
| 网格展示 | 12～15 FPS |
| 源到显示延迟 | 每路 P95 ≤750 ms |
| 单样本延迟 | ≤1500 ms |
| 应用平均 CPU | ≤85% |
| 峰值工作集 | ≤2048 MiB |
| 队列 | 超过 75% 不连续超过 5 秒 |
| UI 最大调度间隔 | <500 ms |
| Camera 03 恢复 | ≤8 秒 |
| 三种关闭状态 | ≤5 秒 |

运行入口：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_video.ps1 `
  -Action RunAutomated -DurationSeconds 600 -WarmupSeconds 20

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_live_latency.ps1 `
  -Action RunAutomated -DurationSeconds 600 -WarmupSeconds 20
```

这两个命令有明确时长，但属于长测。执行期间应持续观察脚本进度，结束后执行对应
`Stop` 并检查残留进程。

## 11. 优化决策顺序

### P0：先取得长测数据

在 600 秒报告完成前不更换媒体架构。先确认瓶颈属于网络、解码、RGB 转换、UI、
内存还是测试源。

### P1：工程化和可观察性

- 增加设备配置持久化，保留普通启动为空状态作为可选模式。
- 增加结构化日志、日志限流和 URL 脱敏。
- 为状态、FPS、队列、重连和最后帧年龄提供用户可见入口。
- 当前已经有播放状态、自动重连和指标，不要实现第二套状态机或重连机制。
- 增加 Windows 构建/测试与 Linux ARM64 交叉构建 CI。

### P2：根据报告处理软件瓶颈

- 若 `decodeFps` 低且 CPU 接近上限：设计可插拔硬件解码接口，再分别实现 Windows
  D3D11VA 和目标 ARM64 VPU 后端。
- 若解码正常但 `displayFps` 低：抽象渲染器，评估 OpenGL YUV 纹理和减少
  YUV→RGB/QImage 拷贝。
- 若队列持续增长：分析 worker 公平性、RTMP 突发和关键帧恢复，不盲目扩大队列。
- 若源延迟高而内部延迟低：检查采集、编码和 RTMP Server，不修改客户端解码池。

### P3：长期产品化

- 长时间运行报告、日志轮转和崩溃诊断。
- 设备鉴权、配置迁移和生产 RTMP Server 部署。
- 目标盒子的温度、功耗和 VPU/GPU 监控。
- 录像、回放或内置 RTMP Server 继续后置。

## 12. 下一对话执行清单

1. 核对 Git 分支、提交和未提交修改。
2. 按第 2 节顺序阅读文档。
3. 确认没有遗留 `rtmp_monitor` 或测试 FFmpeg 进程。
4. 运行 Windows CTest。
5. 执行视频 `Check → Prepare → Start → Status → Test → Stop`。
6. 执行 Camera 03 故障隔离和恢复。
7. 执行双屏 `Check → Start → Status → Capture → Stop`。
8. 完成尚未执行的 UI 人工验收。
9. 在可持续观察的终端运行两个 600 秒测试。
10. 汇总 JSON/Markdown 报告，逐项对照第 10 节门槛。
11. 只有门槛失败且根因明确时，才进入对应优化。
12. 修改后重新运行 Windows CTest、ARM64 构建和 `git diff --check`。

## 13. 仓库与安全边界

可以提交：

- 源码、头文件、CMake 和测试。
- `scripts/` 中不含凭据的测试脚本。
- README 和公开架构、构建、验收文档。

不可提交：

- `out/`、构建目录和 CMake/Ninja/MSVC 产物。
- 测试素材、截图、录屏、日志、PID、指标和自动报告。
- 本机 SDK、Qt、FFmpeg、nginx 二进制及个人路径配置。
- `.env`、密钥、证书、账号和其他凭据。
- 被忽略的 `docs/project_handoff.md` 和 `docs/local/`。

提交前最低检查：

```powershell
git status --short
git diff --check
```

短窗口测试通过只能表述为“当前功能路径和短窗口性能正常”，不能表述为“项目绝对
无 bug”“10 分钟资格已通过”或“真实 ARM64 平台已经验收”。

## 14. 可复制给新对话的开场说明

```text
请先阅读 README.md、docs/README.md、
docs/weeks/week4/week4_conversation_handoff.md、
docs/weeks/week4/week4_release_test_and_module_changes.md 和
docs/weeks/week4/week4_sixteen_stream_validation.md，并检查当前 Git 状态。

项目已完成 0～16 路动态 RTMP 播放、独立网络线程、共享解码池、有界背压、
统一 UI 帧调度、动态移除、拖拽、全屏、重连和指标输出。实现基线为 25b88b1。
Windows CTest 4/4、16 路短窗口视频、30 秒双屏实况和 ARM64 交叉构建已经通过，
但两个 600 秒性能资格测试、若干人工交互场景和真实 ARM64 盒子验收尚未完成。

不要前台启动长驻进程，不要重复实现现有状态机和重连逻辑，不要在没有长测数据时
直接引入 OpenGL 或硬件解码。先完成遗留验收，再根据报告确定优化方向。
```
