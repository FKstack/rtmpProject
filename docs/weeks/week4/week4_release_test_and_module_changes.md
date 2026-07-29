# Week 4：16 路模块变更与测试操作记录

> 文档分类：Week 4 实现与测试。

## 1. 文档目的

本文记录四路基线扩展到 16 路后实际修改的模块、PowerShell 测试脚本的启动模型、
2026-07-28 在当前 Windows 11 双屏电脑上执行的测试，以及仍未覆盖的边界。

架构和完整人工验收步骤见
[week4_sixteen_stream_validation.md](week4_sixteen_stream_validation.md)。四路版本仍作为
历史基线保留在 [week4_multi_stream_playback.md](week4_multi_stream_playback.md)。

## 2. 模块变更

### 2.1 应用与动态连接

- `ConnectionDialog`：校验设备名称和 `rtmp://` URL，生成最小未占用的 Camera 编号。
- `StreamConnectionController`：以稳定 `StreamId` 维护 UI 与媒体层绑定，处理添加、
  重连、移除和命令行预装。
- `MainWindow`：普通启动显示空状态页；工具栏和中央按钮都可添加连接。
- `VideoGridWidget`：支持 0～16 路动态布局、删除和重排。
- `VideoWidget`：提供单路右键重连/移除入口，并保留拖拽与全屏行为。

### 2.2 媒体管线

- `FFmpegPlayer` 的网络线程只执行 RTMP 打开、解复用和压缩包入队。
- 每路有独立网络 `QThread`、会话代次、重连状态和有界压缩包队列；单路连接失败不
  阻塞其他流。
- `DecodeWorkerPool` 默认创建最多 8 个共享 worker。同一流固定到一个 worker，
  `AVCodecContext` 不跨线程并发。
- 每个调度片最多处理 4 个包或 5 ms；每路队列最多 45 包或 4 MiB。溢出后丢弃积压、
  flush 解码器并等待关键帧。
- 网格按最高 15 FPS、最大 640×360 转换 RGB；全屏按最高 30 FPS、最大 1280×720。
- UI 用统一定时器消费每路单帧邮箱，旧帧直接覆盖。

### 2.3 管理、指标与关闭

- `MultiStreamPlaybackManager` 支持动态 add/remove/restart/start/stop，并转发稳定
  `StreamId` 的帧、状态、错误和指标。
- `--metrics-file` 每秒原子输出不含 URL 的 JSON。
- 关闭时先同时中断网络和队列，再等待网络线程与解码任务，最后关闭共享 worker；
  不调用 `QThread::terminate()`。
- `--latency-marker` 仅在测试模式解析副屏 40 位时间标记。标记保持 100 ms，解析器
  用 CRC-8、10 秒时间窗和有界几何候选兼容混合 DPI；生产路径不执行该逻辑。

## 3. 测试脚本启动模型

### 3.1 为什么旧版 `Start` 看起来卡死

Windows PowerShell 5.1 的 `Start-Process` 在重定向多个长驻子进程时会使用线程池回调；
默认线程池过小时，连续启动 16 个 FFmpeg 可能在中途阻塞。此外，固定 launcher
stdout/stderr 文件会让重复启动争用同一文件，外层自动化工具也可能继续等待后代进程
树，因此用户看到长时间“无输出”。

### 3.2 当前实现

- 外层 `Start`、`StartStream`、`StartStreams` 和 `StartApp` 只创建带 GUID 的隐藏
  launcher 并立即返回。
- launcher 内部把线程池最低 worker 数提高到 64，然后后台启动 nginx、FFmpeg 和
  客户端。
- 每个后台进程分别重定向 stdout/stderr 到 `out/logs/...`。
- 本次进程记录写到 `out/runtime/.../pids.json`；停止前核验 PID、进程名、绝对路径
  和启动时间。
- `Status` 从 launcher result JSON 报告 `starting/succeeded/failed`。
- 健康检查最长 55 秒，每 5 秒至少输出一次；失败打印日志最后 100 行并返回非零。
- `Stop` 只处理本次 PID，重复执行安全；测试前已存在的 nginx 只复用、不停止。

## 4. 预录视频测试

操作顺序：

```powershell
$s = ".\scripts\test_16_stream_video.ps1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action Check
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action Prepare
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action Start
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action Status
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action Test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action StopStream -StreamNumber 3
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action Status
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action StartStream -StreamNumber 3
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action Test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $s -Action Stop
```

本机结果：

- `Check` 通过；16 个缓存素材均可复用。
- `Start` 在 8 秒内返回。
- 16/16 publisher、16/16 playing、8 个解码 worker。
- 解码约 29.3～30.3 FPS；网格展示约 10.1～13.1 FPS。
- 压缩包队列为 0；应用工作集约 193 MiB；UI 最大间隔约 106 ms。
- 停止 Camera 03 后为 15/16，只有 Camera 03 重连；其余 15 路继续播放。
- 恢复 Camera 03 后重新达到 16/16，`Test` 通过。
- `Stop` 后无 `rtmp_monitor` 残留；PID 安全清理重复执行可接受。

这是一组功能与短窗口性能结果，不等同于 600 秒资格报告。

## 5. 双屏实况延迟测试

当前屏幕：主屏 1920×1080，副屏 1536×864，副屏位于主屏右侧。

分阶段操作：

```powershell
$l = ".\scripts\test_16_stream_live_latency.ps1"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $l -Action Check
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $l -Action Start
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $l -Action Status
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $l -Action Capture
powershell.exe -NoProfile -ExecutionPolicy Bypass -File $l -Action Stop
```

30 秒自动短验收：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_live_latency.ps1 `
  -Action RunAutomated -DurationSeconds 30 -WarmupSeconds 5 `
  -CaptureIntervalSeconds 10
```

实测结果：

| 指标 | 结果 |
|---|---:|
| 播放流 | 16/16 |
| 有源到显示延迟样本 | 16/16 |
| 每路 P50 | 111～117 ms |
| 每路 P95 | 169～177 ms |
| 单样本最大值 | 275 ms |
| 应用平均 CPU | 43.3% |
| 峰值工作集 | 194.7 MiB |
| UI 最大调度间隔 | 169 ms |
| 自动报告 | 通过 |
| 清理后 `rtmp_monitor` | 0 |

审计截图确认主屏存在完整 4×4 网格，Camera 01～16 均显示副屏源时钟；参考时钟固定
在主屏上方居中。截图、JSON、Markdown 和日志保存在被 Git 忽略的
`out/16-stream-live-latency`、`out/logs` 和 `out/runtime`。

## 6. 自动化与跨平台回归

Windows：

```powershell
ctest --test-dir out\build-windows-x64\debug -C Debug --output-on-failure
```

结果为 4/4 通过，总耗时 22.00 秒：

- UI smoke；
- 动态 0～16 路网格；
- FFmpegPlayer 生命周期；
- 多路管理、隔离、退出和指标。

Linux ARM64：

```bash
cmake --build --preset Linux-ARM64-Debug --parallel 8
file out/build-linux-arm64/debug/rtmp_monitor*
readelf -h out/build-linux-arm64/debug/rtmp_monitor
```

主程序与四个测试目标均为 `ELF 64-bit LSB`、`ARM aarch64`，ELF Machine 为
`AArch64`。

## 7. 上传边界

允许提交：

- `src/`、`include/`、`tests/`；
- `CMakeLists.txt` 和 CMake 配置；
- `scripts/`；
- README 与公开设计/测试文档。

禁止提交：

- `out/`、`build/`、CMake/Ninja/MSVC 产物；
- 测试视频、截图、录屏、原始 RGB/YUV、日志、PID、指标和自动报告；
- FFmpeg、Qt、nginx 二进制及本机 SDK；
- `.env`、密钥、证书、凭据和本地 IDE 配置；
- `docs/project_handoff.md` 与 `docs/local/`。

`.gitignore` 已覆盖上述内容。本次提交前还必须执行 `git status --short`、
`git diff --check` 和已跟踪文件敏感信息检查。

## 8. 尚未完成的资格项

- 600 秒预录视频性能硬门槛。
- 600 秒双屏实况延迟硬门槛。
- Camera 12 人工右键移除并重新添加。
- Camera 01/16 人工拖拽交换。
- Camera 08/16 人工全屏和退出。
- 健康、部分重连、全部失败三种状态的人工 5 秒关闭计时。
- 真实 Linux ARM64 盒子的 QPA、播放、VPU 和长期性能。

因此本次结果可以表述为“实现完成，自动回归与短窗口功能/性能测试通过”，不能表述
为“所有环境绝对无 bug”或“10 分钟性能资格已经通过”。
