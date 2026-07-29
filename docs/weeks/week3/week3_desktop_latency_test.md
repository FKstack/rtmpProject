# 第三周：桌面实况端到端延迟测试

> 文档分类：Week 3 实现与测试。

本文说明如何把 Windows 副屏的真实桌面画面推送到 `RtmpMonitor`，并使用毫秒时钟测量从“桌面发生变化”到“Camera 01 显示变化”的端到端延迟。

## 1. 本次实测结论

测试日期：2026-07-26。

在本机回环网络、nginx-rtmp 和低延迟 x264 参数下，本次 10 个有效样本的结果如下：

| 指标 | 延迟 |
|---|---:|
| 最小值 | 92 ms |
| 平均值 | 117.4 ms |
| 中位数 | 117.5 ms |
| P95（nearest-rank，10 个样本） | 156 ms |
| 最大值 | 156 ms |
| 极差 | 64 ms |

因此，在本次机器和参数下，可以把当前项目的本地桌面实况延迟概括为：

> 典型延迟约 **0.12 秒**，本轮观察范围为 **0.09～0.16 秒**。

这是完整显示链路的结果，不是单独某个 FFmpeg 函数的耗时。当前结果满足建议的本地预览验收线：中位数不高于 200 ms、P95 不高于 300 ms。这两个数值是本项目的工程目标，并不是所有监控产品都必须采用的行业标准。

## 2. 测到的延迟包含什么

本测试的源画面不是 MP4 文件，而是副屏上持续变化的真实 Windows 桌面。测试链路如下：

```mermaid
flowchart LR
    A["副屏 SOURCE 毫秒时钟<br/>真实桌面绘制"] -->
    B["FFmpeg gdigrab<br/>桌面采集"]
    B --> C["libx264<br/>H.264 低延迟编码"]
    C --> D["FLV / RTMP"]
    D --> E["本机 nginx-rtmp"]
    E --> F["FFmpegPlayer<br/>拉流、解封装、H.264 解码"]
    F --> G["sws_scale<br/>YUV → RGB888"]
    G --> H["QImage / Qt 事件投递"]
    H --> I["VideoWidget<br/>Camera 01 绘制"]
```

最终数字包含：

- Windows 桌面绘制和 `gdigrab` 等待下一帧的时间；
- H.264 编码时间；
- FLV 封装、RTMP 发送和 nginx-rtmp 转发时间；
- `FFmpegPlayer` 的网络读取、FLV 解封装和 H.264 解码时间；
- `sws_scale` 转换为 RGB888 的时间；
- 最新帧邮箱、Qt UI 线程投递和 `VideoWidget::paintEvent()` 绘制时间；
- 显示刷新造成的一帧量级等待。

它不包含真实摄像头的传感器曝光、摄像头内部 ISP、硬件编码器、交换机或远端网络。因此，本结果代表“当前 PC 本地回环桌面实况”的基线，不能直接当作未来远端摄像头部署的最终延迟。

## 3. 为什么使用两个时钟

副屏显示蓝色的 `SOURCE` 时钟。它会被 FFmpeg 当作真实桌面画面采集。

主屏顶部显示绿色的 `REFERENCE` 时钟，它不在采集区域内，代表截图发生时的当前时间。主屏下方的 Qt 程序显示经过整个 RTMP 链路后的蓝色 `SOURCE` 时钟。

对主屏进行一次截图后，两个时间存在于同一张图片中，所以不受人工按秒表的反应速度影响：

```text
端到端延迟 = REFERENCE 当前时刻 - Camera 01 中的 SOURCE 时刻
```

例如本次第 4 个样本：

```text
REFERENCE = 21:50:34.029
SOURCE    = 21:50:33.934

延迟 = 34.029 s - 33.934 s = 0.095 s = 95 ms
```

跨分钟或跨小时计算时，应先把两个时间都换算为当天累计毫秒，不能只减最后三位毫秒。

## 4. 本次环境和参数

### 4.1 软件与路径

| 项目 | 本次值 |
|---|---|
| 项目目录 | `E:\rtmpProject` |
| Qt 程序 | `out\build-windows-x64\debug\rtmp_monitor.exe` |
| FFmpeg 命令行 | `E:\DevTools\ffmpeg-8.1.2-full_build\bin\ffmpeg.exe` |
| FFmpeg 版本 | 8.1.2 full build |
| nginx-rtmp | `E:\DevTools\nginx-rtmp` |
| nginx 版本 | 1.28.0，带 nginx-rtmp-module |
| RTMP URL | `rtmp://127.0.0.1:1935/live/camera001` |
| 主屏 | 1920×1080，坐标 `(0, 0)` |
| 采集副屏 | 1536×864，坐标 `(1920, 0)` |

命令行 FFmpeg 使用带 `libx264` 的 GPL full build来生成测试流；Qt 播放器本身仍链接项目已经配置的 FFmpeg 8.1.2 LGPL 动态开发库。测试推流工具的许可配置不改变播放器的链接方式。

### 4.2 编码参数

测试脚本请求：

- `gdigrab` 桌面采集；
- 60 fps 输入请求；
- 1536×864；
- `libx264`；
- `ultrafast`；
- `zerolatency`；
- Constrained Baseline；
- `yuv420p`；
- GOP 为 30；
- 禁止 B 帧；
- 1 个参考帧；
- 4 Mbit/s CBR 上限；
- 500 Kbit VBV buffer；
- FLV/RTMP 输出。

FFmpeg 日志显示本轮实际处理速度约为 30 fps、约 1.76 Mbit/s。虽然命令请求 60 fps，但 `gdigrab`、副屏刷新率和当前机器吞吐共同把实际新帧速率限制在约 30 fps。因此本轮时间分辨率约为一帧 33 ms。

## 5. 十个有效样本

| 样本 | REFERENCE | Camera 01 中的 SOURCE | 延迟 |
|---:|:---:|:---:|---:|
| 1 | 21:50:30.588 | 21:50:30.449 | 139 ms |
| 2 | 21:50:31.809 | 21:50:31.684 | 125 ms |
| 3 | 21:50:32.917 | 21:50:32.791 | 126 ms |
| 4 | 21:50:34.029 | 21:50:33.934 | 95 ms |
| 5 | 21:50:35.121 | 21:50:35.029 | 92 ms |
| 6 | 21:50:36.231 | 21:50:36.105 | 126 ms |
| 7 | 21:50:37.339 | 21:50:37.229 | 110 ms |
| 8 | 21:50:38.447 | 21:50:38.338 | 109 ms |
| 9 | 21:50:39.528 | 21:50:39.432 | 96 ms |
| 10 | 21:50:40.651 | 21:50:40.495 | 156 ms |

排序后的延迟为：

```text
92, 95, 96, 109, 110, 125, 126, 126, 139, 156 ms
```

平均值计算：

```text
(139 + 125 + 126 + 95 + 92 + 126 + 110 + 109 + 96 + 156) / 10
= 117.4 ms
```

## 6. 如何重复测试

测试工具为：

```text
scripts/test_desktop_latency.ps1
```

脚本要求两块显示器。副屏作为采集源，Qt 播放器留在主屏，避免出现“播放器采集自己”的无限镜像。

### 6.1 测试前准备

1. 保存正在编辑的文件。
2. 关闭或最小化主屏上的密码、聊天、邮件等敏感内容。
3. 确认第二块显示器处于“扩展”模式。
4. 确认 Debug 程序已经构建。

```powershell
cd E:\rtmpProject

powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\scripts\test_desktop_latency.ps1 `
    -Action Check
```

`Check` 会验证：

- FFmpeg 是否存在；
- 是否支持 `gdigrab`；
- 是否支持 `libx264`；
- nginx-rtmp 是否存在；
- Qt 和 FFmpeg DLL 是否存在；
- 是否检测到主屏和副屏。

### 6.2 启动实况链路

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\scripts\test_desktop_latency.ps1 `
    -Action Start
```

脚本会：

1. 必要时启动 nginx-rtmp；
2. 在副屏显示蓝色 `SOURCE` 毫秒时钟；
3. 在主屏顶部显示绿色 `REFERENCE` 毫秒时钟；
4. 用 FFmpeg 捕获副屏并推送 H.264/RTMP；
5. 启动 `rtmp_monitor.exe`；
6. 让 Camera 01 显示延迟后的副屏。

等待约 5 秒，使首次连接、H.264 关键帧和窗口布局稳定。此时也可以在副屏移动鼠标或窗口，主屏 Camera 01 应显示同样的动作。

### 6.3 采集样本

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\scripts\test_desktop_latency.ps1 `
    -Action Capture `
    -SampleCount 10 `
    -SampleIntervalMs 1000
```

截图和运行日志保存在：

```text
out\desktop-latency\
```

主要文件：

| 文件 | 用途 |
|---|---|
| `sample-*.png` | 同时包含 REFERENCE 和播放器 SOURCE 的原始样本 |
| `samples.json` | 截图文件和采集时间清单 |
| `ffmpeg.log` | 桌面采集、编码、码率和实际 fps |
| `state.json` | 本轮路径、进程、屏幕和编码参数 |

`out/` 已被 Git 忽略。截图可能包含桌面内容，不应提交到仓库。

### 6.4 停止和清理

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\scripts\test_desktop_latency.ps1 `
    -Action Stop
```

脚本会停止它启动的 Qt 程序、FFmpeg 和两个时钟。如果 nginx 在测试之前没有运行，脚本还会请求 nginx 优雅退出；测试前已经存在的 nginx 不会被停止。

可额外确认没有残留：

```powershell
Get-Process ffmpeg,rtmp_monitor -ErrorAction SilentlyContinue
Get-NetTCPConnection -LocalPort 1935 -State Listen -ErrorAction SilentlyContinue
```

## 7. 如何解释结果

### 7.1 不要只取一个样本

视频链路按帧工作，截图可能刚好落在新帧绘制之前或之后。单个结果会随采集相位变化。至少使用 10 个样本；要做正式性能基线，建议采集 30～100 个样本并报告中位数和 P95。

### 7.2 本轮测量精度

本轮实际约 30 fps，一帧约 33 ms。时钟虽然每 10 ms 更新一次，但显示、`gdigrab` 和 Qt 绘制仍受帧刷新约束。因此不要把 `117.4 ms` 理解为微秒级精确值，更合理的表达是：

```text
典型约 120 ms，单次结果存在约一帧量级的量化误差。
```

### 7.3 本地结果不等于远程摄像头

真实部署还会增加：

- 摄像头曝光和图像处理；
- 摄像头硬件编码缓存；
- 交换机、Wi-Fi、路由器或公网时延；
- 服务端跨机器转发；
- 丢包重传和抖动缓冲。

后续测试远端设备时，可让源端画面显示与接收端同步的 NTP/PTP 时钟，或者使用同一台高速摄像机同时拍摄源屏和接收屏。不能简单拿接收端当前时间减去未经校时的摄像头时间。

## 8. 延迟升高时的排查顺序

1. 查看 `ffmpeg.log` 中 `speed` 是否稳定接近 `1x`，实际 fps 是否持续下降。
2. 确认仍使用 `ultrafast`、`zerolatency`、`-bf 0` 和较小 VBV buffer。
3. 把桌面采集分辨率降到 1280×720，判断是否为采集或编码吞吐不足。
4. 检查 nginx 与播放器是否仍在同一台机器，排除网络变量。
5. 检查 `FFmpegPlayer` 是否仍设置 `fflags=nobuffer`、较小 `probesize` 和 `analyzeduration`。
6. 检查 UI 是否被其他高负载窗口长期占用。
7. 若 CPU 采集成为瓶颈，再比较 Windows Graphics Capture/Desktop Duplication 与硬件编码器；硬件编码器也必须显式关闭 B 帧和 lookahead。

## 9. 已知环境提示

当前用户 PowerShell profile 仍引用已经不存在的：

```text
H:\ANACONDA3\conda.exe
```

部分宿主命令结束时可能打印 `CommandNotFoundException`。本轮 FFmpeg、Qt、截图和统计均已成功，该 profile 提示没有改变测试结果。建议以后单独清理 PowerShell profile，但不要把它和 RTMP 延迟问题混在一起排查。
