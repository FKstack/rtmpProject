# Windows 真实摄像头帧率与延迟资格测试

> 音频扩展：省略音频参数时仍按原有 `-an` 视频-only链路运行；使用 `--audio-device "<DirectShow 音频设备>"` 发布摄像头/麦克风声音，或使用 `--synthetic-audio` 发布 48 kHz 单声道资格音。两者互斥。音频固定为 AAC-LC 64 kbit/s，并继续使用同一 RTMP/SRS 地址。端到端音频验收及脱敏报告流程见 [低延迟单向音频流](../../architecture/low_latency_audio_stream.md)。

## 结论来源

```text
权威链：摄像头/合成源 -> 时间与源序号标记 -> FFmpeg H.264 -> SRS
      -> RtmpMonitor schema v4 -> report.json/report.md

视觉链：RtmpMonitor（左）+ 同一标记帧的本地参考窗口（右）
      -> 60 FPS gdigrab/NVENC -> 离线按 PTS 和源序号配对
```

资格结论只来自 schema v4 与源端 JSONL。视觉录像用于独立复核，不能覆盖权威指标。测试工具由
`RTMP_MONITOR_BUILD_VALIDATION_TOOLS` 控制，默认在 Windows 开发构建生成，但没有安装规则。

固定资格输入是 `USB2.0 HD UVC WebCam` 的 MJPEG 1280×720@30。60 FPS 只使用 `testsrc2`
合成源探测能力。音频关闭，发布固定为 H.264/YUV420P、1 秒 GOP、无 B 帧。

## 构建、自检与环境检查

```powershell
cmake --preset Qt-Release --fresh
cmake --build out/build-windows-x64/release --parallel
ctest --test-dir out/build-windows-x64/release --output-on-failure

powershell -NoProfile -ExecutionPolicy Bypass -File scripts/camera_validation.ps1 `
  -Action Check -Distro <WSL发行版>

powershell -NoProfile -ExecutionPolicy Bypass -File scripts/camera_validation.ps1 `
  -Action SelfTest -BuildDir out/build-windows-x64/release
```

`Check` 验证 DirectShow 设备和 720p30 模式、libx264/NVENC、SRS 端口、开发工具、磁盘和
gdigrab。SRS 未运行不会使单纯的 `Check` 失败；`Run` 会在需要时调用现有安全脚本启动，并只停止
本轮启动的实例。`SelfTest` 覆盖 CRC、32 位时间回绕、序号、缩放、损坏标记、旧格式兼容、H.264
压缩和显示帧率策略。

`Run` 启动后会自动将 RtmpMonitor 放在左半屏，将“标记写入后、编码发布前”的同帧摄像头参考
窗口放在右半屏；不会进入监控墙全屏。普通指标运行也能人工观察左右差异，只有显式使用
`-RecordVisualEvidence` 才会额外启动 60 FPS 录屏。

## 快速运行与正式矩阵

```powershell
# 120 秒单路诊断
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/camera_validation.ps1 `
  -Action Run -StreamCount 1 -WarmupSeconds 20 -DurationSeconds 120 `
  -DisplayFps 30 -SourceFps 30 -SourceKind Camera -Distro <WSL发行版>

# 1/4/8 正式门禁、16 路能力项；可追加合成 60 FPS 能力矩阵
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/camera_validation.ps1 `
  -Action RunMatrix -Distro <WSL发行版> -IncludeSynthetic60
```

正式矩阵顺序执行 1、4、8 路各 20 秒预热加 600 秒采样，再执行 16 路 120 秒；16 路短测通过后
才升级到 600 秒。`-IncludeSynthetic60` 继续探测 1/4/8/16 路并输出
`recommendedMaxStreamsAt60`。

产品 CLI 支持 `--display-fps=auto|15|30|60`：Windows `auto` 为 30，Linux ARM64 `auto`
暂时为 15；普通网格和两种全屏使用同一目标。显式 60 不会静默降档，输入源也必须实际达到 60。

## 状态与安全停止

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/camera_validation.ps1 -Action Status
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/camera_validation.ps1 -Action Stop -Distro <WSL发行版>
```

`Stop` 只处理状态文件记录的 PID，并同时核对可执行文件绝对路径和启动时间。PID 被复用、路径不符或
启动时间不符时不会终止该进程。已健康运行且不属于本轮的 SRS 不受影响。

## 视觉证据与离线分析

单路 `Run` 增加 `-RecordVisualEvidence` 后，脚本使用 `--validation-layout` 隐藏产品 chrome，自动
摆放左右窗口，将两个 Win32 client rect 写入 `rois.json`，并以 60 FPS NVENC 录制。正式性能运行
默认不录屏，避免录制负载污染结论。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/camera_validation.ps1 `
  -Action Analyze -InputVideo comparison.mp4 `
  -LeftRoi 0,0,960,540 -RightRoi 960,0,960,540 `
  -OutputDirectory out/camera-validation/manual-analysis
```

未传 ROI 时按左右等分推断。分析器按视频 PTS 解码两侧序号，计算每秒独立内容 FPS、重复帧、长间隔
以及同一源序号的相对延迟 P50/P95/最大值。只有录像不低于 60 FPS、时长不低于 30 秒、两侧标记
识别率都不低于 90% 时，`gateEligible` 才为 `true`。

用户提供的 1280×720 手机拍屏样例已实测为 25 FPS、4.92 秒、123 帧，左右标记识别率为 0，
`gateEligible=false`。它只能表达需求，不能确定优化结论。

## 产物与门禁

每次运行写入 `out/camera-validation/<run-id>/`：`state.json`、脱敏配置、源端/客户端 JSONL、
可选的 `comparison.mp4` 和 `rois.json`、`video-analysis.json`、`report.json/report.md`、截图与日志。
报告不保存完整 RTMP URL、凭据或个人绝对路径。报告直接使用累计分位数，不对每秒 P95 再求平均。

1/4/8 路 720p30 正式门禁检查采集/发布/解码/显示 FPS、95% 稳态秒、显示保留比例、呈现间隔、
端到端与内部延迟、标记识别率、序号缺口、队列增长、重连、不支持帧、OpenGL fallback，以及
paint/upload/GPU P95。`report.json.bottleneck` 按源端、解码队列、显示邮箱和 renderer 顺序归因。

## 当前验证边界

- 已验证：Windows Debug 全量 CTest 20/20、环境 `Check`、共享标记与帧率策略测试、样例视频被正确拒绝。
- 已验证：真实摄像头单路并排诊断通过（20 秒预热、15 秒采样）：采集 30.067、发布 30.533、解码/显示 30.068 FPS，保留率 100%，标记识别率 100%，源延迟 P50/P95 为 61/102 ms，OpenGL 无回退。该短测不是 600 秒正式门禁。
- 已验证：第二轮单路 120 秒快速运行通过。采集/发布/解码/显示分别为 30.000/29.967/30.038/29.963 FPS，P95/最大呈现间隔 36/85 ms，源延迟 P50/P95/最大值 59/104/152 ms；采集丢帧、背压丢帧和源序号缺口均为 0。首轮发现的 2 帧瞬时背压通过仍然有界的 8 帧节拍队列消除。
- 待执行：1/4/8 路各 600 秒正式矩阵尚未执行。
- 待执行：16 路摄像头能力项、720p60 合成能力矩阵、CPU 1/8 路诊断 A/B。
- Linux ARM64 档位仍由真实板卡资格测试决定，Windows 数据不能外推到 ARM。
