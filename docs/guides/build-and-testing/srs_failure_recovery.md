# SRS 异常恢复与发布门禁验证指南

> 文档分类：构建与验证。对应 `docs/srs_server_integration_plan.md` Phase 7。
>
> 最近独立复验：2026-08-09。平台：Windows 11 + WSL2 Ubuntu-22.04-New
>（mirrored networking）、SRS 6.0.184（`v6.0-r0`）、Windows Debug 客户端。

## 1. 恢复策略基线

```text
默认无限重试（--max-reconnect-failures=0）-> 依赖既有 FFmpegPlayer 3 秒重连
重试上限达到（非零配置）                    -> 默认由用户手动“重新连接”
Server Healthy 上升沿自动 restartStream     -> 本阶段未启用，标记 [需要验证]
```

不得在 Server Unavailable 时调用 `stopAll()`；本阶段未对
`StreamConnectionController` 增加 Healthy 上升沿逻辑。

## 2. 已验证的故障矩阵（真实 SRS + 真实客户端）

| # | 注入故障 | 观察结果 | 结论 |
|---|---|---|---|
| 1 | 4 路播放中精确停止自有 SRS（`srs_dev_wsl.ps1 -Action Stop`） | 全部流离开 Playing 进入重连/错误；UI 清旧帧；监控约 10 秒（3 次失败防抖）后报 `unavailable` | 通过 |
| 2 | 重启 SRS 并恢复 publisher（ffmpeg 无客户端重试，真实摄像头自行重推） | 默认无限重试下 4/4 在 20 秒内恢复 Playing；监控再次报 `healthy`；不要求重启 Qt 应用 | 通过 |
| 3 | 未知 python3 进程占用 TCP 1935 | `-Action Start` 拒绝启动，退出码 3，打印监听者 PID/进程信息，**未杀**该进程（事后验证其仍存活） | 通过 |
| 4 | 健康 API 不可达（apiBaseUrl 指向无监听端口），RTMP 正常 | 4/4 继续 Playing，监控报 `degraded`；播放器不依赖健康 API | 通过 |
| 5 | 单路 publisher 中断（16 路中停 camera03） | 其余 15 路继续 Playing；恢复后 camera03 在 20 秒内自动 Playing | 通过 |
| 6 | 应用退出（WM_CLOSE） | exitCode=0；无 rtmp_monitor 进程残留；无 FFmpeg/Qt 网络线程残留；监控 timer/reply 全部释放 | 通过 |

补充事实：

- 监控防抖参数：2 秒轮询、1500 ms 单次超时、3 次失败转 Unavailable、
  2 次成功转 Healthy；状态不逐次抖动（单元测试 flap 场景覆盖）。
- WSL2 mirrored networking 下，连接到已关闭的本机端口不会立刻 RST，
  单次探测按 1.5 秒超时耗尽，故 SRS 停止到 `unavailable` 事件约 10 秒。
- 恢复 15 秒目标按“SRS 就绪 + publisher 已推流”计时；ffmpeg publisher
  在 SRS 停止后会退出，不会自动重推，测试脚本在重启 SRS 后重启 publisher。

## 3. 进程所有权规则（脚本强制）

- `srs_dev_wsl.ps1` 状态文件 `out/srs/srs-dev-wsl.state.json` 记录 RunId、
  WSL PID、`/proc/<pid>/exe` 解析路径、配置路径和启动时间。
- `Stop` 对每一次 `SIGQUIT`/`SIGKILL` 都在同一个 WSL 操作中重新校验
  `/proc/<pid>/exe` 与 cmdline；不匹配时
  退出码 4 且不发送任何信号；无状态文件时不做任何进程操作。
- 验证脚本只停止自己启动且身份匹配的 publisher/SRS；从不按进程名批量
  结束进程，从不接管未知 SRS/nginx/Docker/FFmpeg。

## 4. 发布门禁状态

| 门禁 | 状态 |
|---|---|
| Windows 完整 CTest | 17/17 通过（2026-08-09 独立全新配置后复验，85.29 秒） |
| 真实流测试（1/4/16 路） | 通过 |
| SRS 崩溃/重启恢复 | 通过（见 §2 #1、#2） |
| 未知 1935 占用不杀 | 通过（见 §2 #3） |
| ARM64 RASTER/GLES3 交叉构建 | 通过（编译/链接门禁；非实机） |
| ARM 真机构建、systemd 与恢复 | `[需要验证]`，等待目标板 |
| 非零 `--max-reconnect-failures` 达到上限后 Server 恢复时的行为 | `[需要验证]`：默认无限重试已覆盖常规场景；Healthy 上升沿自动 restart 未启用 |

## 5. 复现命令

```powershell
# 基线：构建 + 完整 CTest
cmake --build out\build-windows-x64\debug --config Debug
ctest --test-dir out\build-windows-x64\debug -C Debug --output-on-failure

# SRS 生命周期
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\srs\srs_dev_wsl.ps1 -Action Start
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\srs\srs_dev_wsl.ps1 -Action Stop

# 端到端链路（含 10 分钟保活）
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\srs\verify_srs_chain.ps1 -Action Verify -SoakSeconds 600
```

故障注入脚本为一次性验证脚本（`out/srs/test_phase7_recovery.ps1`，被 Git
忽略），其场景已固化在本指南 §2 的结果矩阵中；如产品化需要常驻故障注入
入口，再将其提升为 `scripts/srs/test_srs_failure_recovery.ps1`。

## 6. 与其他文档的关系

- 部署与脚本入口：[RTMP 链路验证 §15](rtmp_chain_verification.md)
- ARM 部署边界：[跨平台构建 §9](cross_platform_build.md)
- 方案与 `[需要验证]` 清单：`docs/srs_server_integration_plan.md` 第 15 节
