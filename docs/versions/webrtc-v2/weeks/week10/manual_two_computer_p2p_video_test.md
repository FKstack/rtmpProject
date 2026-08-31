# Week 10 从零双电脑 P2P 视频测试：当前电脑发送，公司电脑接收

> 适用版本：`RtmpMonitor 0.2.0-beta.1` Week 10 Windows x64 候选包。
>
> 推荐拓扑：当前电脑运行 `publisher/offer`，公司电脑运行 `viewer/answer`。
>
> 当前资格边界：候选包已经完成两个本机干净副本的 2/2 闭环，但真实公司电脑、物理网络和你的
> 自定义视频尚未在本机自动替你验证。本文就是这些外部测试的执行手册。

## 教程契约

### 你最终会得到什么

完成本文后：

1. 当前电脑从一个 MP4 文件读取 H.264 视频；
2. 两台电脑通过手工搬运 Offer/Answer JSON 完成 WebRTC 信令；
3. 视频媒体通过两台电脑之间的 WebRTC UDP 链路传输；
4. 公司电脑显示 viewer 窗口，并产生 `media_received`、`frame_decoded`、`frame_presented` 证据；
5. 测试结束后，临时会话文件和进程可以明确清理。

本教程不实现 WSS 自动信令、TURN Relay、用户身份系统、MQTT 控车或 RTMP fallback。MQTT 仍是以后
设备控制的独立通道，本次视频测试不会发送任何设备控制命令。

### 读者与环境假设

- 两台电脑都是 Windows x64，且允许运行这个内部、未正式发布的测试包；
- 你能在两台电脑上打开 Windows PowerShell 5.1；
- 你能通过公司允许的文件传输方式在两台电脑之间复制 ZIP 和 JSON；
- 如果两台电脑不在同一局域网，双方都有权使用一个获授权的 STUN 服务；
- 不要求你理解 SDP、ICE 或 H.264 内部结构，本文在用到时解释。

### 已验证与未验证

| 项目 | 当前状态 |
| --- | --- |
| Week 10 候选包、DLL、Qt 插件 | 已验证 |
| 同机两个全新展开副本的 publisher/viewer 闭环 | 2/2 已验证 |
| 包内 1280×720、30 FPS、H.264 Baseline 样本 | 已验证 |
| 当前电脑到公司电脑的物理网络 | 未验证，需执行本文 |
| 你自己的视频文件 | 未验证，需先满足第 12 节格式要求 |
| TURN Relay | 当前版本未实现 |

## 本章完成后你将看到什么

公司电脑会出现标题为 WebRTC viewer 的播放窗口。成功路径的控制台事件顺序大致为：

```text
runtime_ready
ice_config_loaded             # 仅 STUN 模式
ice_gathering_completed
description_exported
connected
media_received                # 接收端
frame_decoded                 # 接收端
frame_presented               # 接收端
completed
```

事件之间可能有等待，JSON 字段顺序也可能不同。稳定通过条件是事件存在、viewer 确实显示动态画面，
而不是耗时或输出逐字一致。

## 前置检查点

- 仓库分支：`Beta`；
- 候选包版本：`0.2.0-beta.1`；
- 候选包 source commit：`392d9aa`；
- 当前包位置：仓库下 `out/packages/webrtc-week10/`；
- 不要只复制 EXE，必须复制完整 ZIP。

## 预计时间

- 阅读与包核对：10～15 分钟；
- ZIP 传输和两端展开：取决于公司文件传输速度；
- 第一次 sample 闭环：15～30 分钟；
- 自定义视频实验：10～20 分钟；
- 总计：通常 30～60 分钟，不包含申请公司网络或 STUN 授权的等待时间。

## 本章在最终项目中的位置

```text
Week 10 本机软件与候选包资格
    → 本章：两台真实 Windows + 手工文件信令 + 单路可视 P2P
        → 后续：WSS 自动信令 + TURN + 设备身份/MQTT 控制绑定
```

本章使用现有候选包建立外部环境证据，不修改生产源码。它能证明当前两台电脑是否可以工作，但不把
一次成功扩大成所有企业网络或完整产品资格。

## 步骤 0：确认无需重新构建，先验证候选包 CLI

### 当前问题

用户需要运行现有 Week 10 功能，而不是重新编译或继续开发。第一步必须确认使用的是完整、已验证的
候选包，避免从构建目录单独复制 EXE 后出现 DLL 或 Qt 插件弹窗。

### 修改文件

- 生产源码：不修改；
- 候选包：后续只会创建 `session-exchange`、`incoming-staging`、`manual-logs` 和本机
  `local-config`；
- 自定义视频实验：只在发送端备份并替换展开副本中的 `webrtc-assets/sample.mp4`。

### 代码修改

无代码补丁。本章所有变化都在用户拥有的展开目录中，原始 ZIP 保持不变，可以随时重新展开恢复。

### 构建

不需要构建。使用已经通过 Week 10 Release ON 打包和双副本闭环的
`RtmpMonitor-0.2.0-beta.1-windows-x64.zip`。

### 运行或测试

在候选包根目录执行：

```powershell
& '.\rtmp_monitor_webrtc_client.exe' --help
```

### 观察证据

帮助首行应为 `Usage: rtmp_monitor_webrtc_client [options]`，并列出媒体角色、信令角色、source、ICE
模式和 timeout。Week 10 本机包测试已对两个干净展开副本验证此入口；公司电脑仍需现场重复。

### 通过条件

1. 命令退出码为 0；
2. 没有缺 DLL、入口点或 Qt platform plugin 弹窗；
3. 包根存在 `package-manifest.json`、客户端 EXE、`platforms/qwindows.dll` 和验证 sample；
4. 若失败，停止后续网络测试，先重新展开完整 ZIP。

## 1. 先理解当前链路

```text
当前电脑 MP4
    ↓ H.264 demux + Annex-B
publisher / SendOnly PeerConnection
    ↓ WebRTC UDP（Direct）
viewer / ReceiveOnly PeerConnection
    ↓ H.264 decode + mailbox + Qt canvas
公司电脑 viewer 窗口

Offer/Answer JSON：通过你授权的文件传输方式来回搬运
MQTT：本次不参与视频链路
```

Offer/Answer 文件只负责让两端知道如何建立连接，视频帧不会被写进这些 JSON。文件中仍含 SDP、ICE
candidate、地址、端口和 fingerprint，属于临时敏感信令材料：不要打开后复制到聊天、Git、普通日志
或测试报告，只在两台测试电脑之间传输，测试后删除。

### `--media-role` 与 `--signaling-role`

这两个参数是相互独立的：

| 参数 | 本教程取值 | 含义 |
| --- | --- | --- |
| 当前电脑 `--media-role` | `publisher` | 读取本地 MP4 并发送视频 |
| 公司电脑 `--media-role` | `viewer` | 接收、解码并显示视频 |
| 当前电脑 `--signaling-role` | `offer` | 先生成 Offer，等待 Answer |
| 公司电脑 `--signaling-role` | `answer` | 等待 Offer，再生成 Answer |

`offer` 不等于发送端，`answer` 也不等于接收端；只是本教程采用了最容易理解的组合。

### `--ice-mode host` 与 `--ice-mode stun`

- 两台电脑在同一个可互通局域网：先用 `host`；不需要 STUN 配置。
- 当前电脑和公司电脑在不同网络：使用 `stun`；双方都要配置获授权 STUN。
- STUN 只帮助发现可用于 Direct 的地址，不转发视频。
- 当前 Week 10 没有 TURN。如果企业防火墙、CGNAT 或 NAT 类型阻止 Direct，结果会是连接失败；这不
  能通过重复搬运 JSON 修复，需要后续 TURN 产品能力或更合适的授权网络环境。

## 2. 找到并传输完整候选包

在当前电脑仓库根目录打开 PowerShell：

```powershell
$RepoRoot = (Get-Location).Path
$Zip = Join-Path $RepoRoot `
  'out\packages\webrtc-week10\RtmpMonitor-0.2.0-beta.1-windows-x64.zip'

Test-Path -LiteralPath $Zip
Get-Item -LiteralPath $Zip | Select-Object Name,Length,LastWriteTime
```

第一行必须显示 `True`。然后通过公司允许的文件传输方式，把这个完整 ZIP 复制到公司电脑。不要只传
`rtmp_monitor_webrtc_client.exe`，否则会缺少 Qt、FFmpeg、libdatachannel、SRTP、OpenSSL、平台插件
和样本文件。

本教程不要求生成 SHA 或内容哈希。两台电脑通过 manifest 中的版本和 `sourceCommit` 判断是否使用
同一候选构建。

## 3. 在两台电脑分别展开 ZIP

### 3.1 当前电脑：发送端

```powershell
$SenderBase = Join-Path $env:USERPROFILE 'Desktop\RtmpMonitor-WebRTC-Sender'
New-Item -ItemType Directory -Force -Path $SenderBase | Out-Null
Expand-Archive -LiteralPath $Zip -DestinationPath $SenderBase -Force

$SenderCandidates = @(Get-ChildItem -LiteralPath $SenderBase -Directory |
  Where-Object {
    Test-Path -LiteralPath (Join-Path $_.FullName 'package-manifest.json')
  })
if ($SenderCandidates.Count -ne 1) {
  throw '没有找到唯一的候选包根目录。'
}
$SenderRoot = $SenderCandidates[0].FullName
Set-Location -LiteralPath $SenderRoot
Write-Host "发送端包根：$SenderRoot"
```

### 3.2 公司电脑：接收端

先把 `$ReceiverZip` 改成公司电脑上实际 ZIP 路径：

```powershell
$ReceiverZip = Read-Host '请输入公司电脑上的候选 ZIP 完整路径'
$ReceiverBase = Join-Path $env:USERPROFILE 'Desktop\RtmpMonitor-WebRTC-Receiver'
New-Item -ItemType Directory -Force -Path $ReceiverBase | Out-Null
Expand-Archive -LiteralPath $ReceiverZip -DestinationPath $ReceiverBase -Force

$ReceiverCandidates = @(Get-ChildItem -LiteralPath $ReceiverBase -Directory |
  Where-Object {
    Test-Path -LiteralPath (Join-Path $_.FullName 'package-manifest.json')
  })
if ($ReceiverCandidates.Count -ne 1) {
  throw '没有找到唯一的候选包根目录。'
}
$ReceiverRoot = $ReceiverCandidates[0].FullName
Set-Location -LiteralPath $ReceiverRoot
Write-Host "接收端包根：$ReceiverRoot"
```

## 4. 核对两台电脑使用同一候选包

分别在两台电脑的包根目录执行：

```powershell
$PackageInfo = Get-Content -LiteralPath '.\package-manifest.json' -Raw |
  ConvertFrom-Json
$PackageInfo | Select-Object version,sourceCommit

@(
  '.\rtmp_monitor_webrtc_client.exe',
  '.\webrtc-assets\sample.mp4',
  '.\platforms\qwindows.dll',
  '.\datachannel.dll',
  '.\avcodec-62.dll'
) | ForEach-Object {
  [pscustomobject]@{Path=$_;Exists=(Test-Path -LiteralPath $_)}
}
```

两台电脑必须都显示：

```text
version      0.2.0-beta.1
sourceCommit 392d9aaaf42f81a75d70ef80cffb21367e5e102e
```

五个 `Exists` 都必须是 `True`。如果不一致，停止测试，重新传同一个 ZIP；不要混用旧 EXE 或旧 DLL。

可以再执行真实 CLI 自检：

```powershell
& '.\rtmp_monitor_webrtc_client.exe' --help
```

退出码应为 0，帮助中应出现 `--media-role`、`--signaling-role`、`--source`、`--ice-mode` 和
`--timeout-ms`。

## 5. 第一次必须先用包内样本

不要一开始就替换成你的视频。先使用已经验证的：

```text
webrtc-assets\sample.mp4
```

它是 1280×720、30 FPS、H.264 Constrained Baseline、Level 3.1、零 B 帧。先用它打通，可以把
“网络/信令问题”和“自定义视频格式问题”分开。只有本节样本成功后，再做第 12 节。

## 6. 选择网络模式

### 6.1 同一局域网

在两台电脑后续窗口中都设置：

```powershell
$IceMode = 'host'
```

前提是两台电脑的局域网允许终端之间互通。访客 Wi-Fi、无线客户端隔离或企业 VLAN 仍可能阻断。

### 6.2 不同网络：当前电脑到公司电脑

在两台电脑后续窗口中都设置：

```powershell
$IceMode = 'stun'
```

然后在两台电脑各自的包根目录执行下面命令。STUN 地址必须由公司或测试负责人授权；不要把真实地址
写进仓库或本教程。

```powershell
$StunUrl = Read-Host '请输入获授权的 STUN URL，格式为 stun:主机:端口'
if ($StunUrl -notmatch '^stun:[^\s<>]+$') {
  throw 'STUN URL 格式无效。'
}
$ConfigRoot = Join-Path (Get-Location).Path 'local-config'
New-Item -ItemType Directory -Force -Path $ConfigRoot | Out-Null
[ordered]@{
  schemaVersion = 1
  stunUrl = $StunUrl
} | ConvertTo-Json | Set-Content -LiteralPath (
  Join-Path $ConfigRoot 'ice-runtime.json'
) -Encoding ASCII
```

配置只应留在各自电脑的 `local-config`。不要把它随 Offer/Answer 一起传输，也不要提交 Git。

## 7. 准备会话目录和四个 PowerShell 窗口

每台电脑准备两个窗口：

- 运行窗口：保持客户端运行并观察 JSONL；
- 文件窗口：查看和搬运 Offer/Answer。

确认旧客户端已经退出后，在当前电脑发送端的“文件窗口”执行。如果这是新窗口，先按上一步显示的
结果填写发送端包根：

```powershell
$SenderRoot = Read-Host '请输入发送端包根完整路径'
$SenderExchange = Join-Path $SenderRoot 'session-exchange'
$SenderIncoming = Join-Path $SenderRoot 'incoming-staging'
New-Item -ItemType Directory -Force -Path `
  $SenderExchange,$SenderIncoming | Out-Null
Get-ChildItem -LiteralPath $SenderExchange -File -Filter '*.json' `
  -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem -LiteralPath $SenderIncoming -File -Filter '*.json' `
  -ErrorAction SilentlyContinue | Remove-Item -Force
```

在公司电脑接收端的“文件窗口”执行：

```powershell
$ReceiverRoot = Read-Host '请输入接收端包根完整路径'
$ReceiverExchange = Join-Path $ReceiverRoot 'session-exchange'
$ReceiverIncoming = Join-Path $ReceiverRoot 'incoming-staging'
New-Item -ItemType Directory -Force -Path `
  $ReceiverExchange,$ReceiverIncoming | Out-Null
Get-ChildItem -LiteralPath $ReceiverExchange -File -Filter '*.json' `
  -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem -LiteralPath $ReceiverIncoming -File -Filter '*.json' `
  -ErrorAction SilentlyContinue | Remove-Item -Force
```

这些命令只删除各自包内两个明确目录的旧 JSON。不要把递归删除命令指向桌面、包根或用户目录，也不要
删除 `local-config`。

## 8. 先启动公司电脑接收端

在公司电脑的“运行窗口”执行。不同网络使用 `$IceMode='stun'`；同一局域网使用 `host`。

```powershell
$ReceiverRoot = Read-Host '请输入接收端包根完整路径'
Set-Location -LiteralPath $ReceiverRoot
$env:QT_QPA_PLATFORM = 'windows'
$IceMode = 'stun'
New-Item -ItemType Directory -Force -Path '.\manual-logs' | Out-Null

& '.\rtmp_monitor_webrtc_client.exe' `
  --media-role viewer `
  --signaling-role answer `
  --ice-mode $IceMode `
  --timeout-ms 300000 `
  2> '.\manual-logs\receiver.stderr.txt' |
  Tee-Object -FilePath '.\manual-logs\receiver.stdout.jsonl'
```

此时 viewer 窗口可能已经打开但没有画面，控制台应出现 `runtime_ready`，随后等待 Offer。这是正常
状态。不要关闭这个窗口。

如果使用 `host`，把上面 `$IceMode = 'stun'` 改为 `$IceMode = 'host'`。

## 9. 再启动当前电脑发送端

在当前电脑的“运行窗口”执行：

```powershell
$SenderRoot = Read-Host '请输入发送端包根完整路径'
Set-Location -LiteralPath $SenderRoot
$env:QT_QPA_PLATFORM = 'windows'
$IceMode = 'stun'
New-Item -ItemType Directory -Force -Path '.\manual-logs' | Out-Null

& '.\rtmp_monitor_webrtc_client.exe' `
  --media-role publisher `
  --signaling-role offer `
  --source sample `
  --ice-mode $IceMode `
  --timeout-ms 300000 `
  2> '.\manual-logs\sender.stderr.txt' |
  Tee-Object -FilePath '.\manual-logs\sender.stdout.jsonl'
```

发送端完成 ICE gathering 后，会在 `session-exchange` 生成一个名称以 `.offer.json` 结尾的文件，并
输出 `description_exported`。这个运行窗口也要保持打开。

## 10. 把 Offer 传到公司电脑

在当前电脑的“文件窗口”执行：

```powershell
$Offer = Get-ChildItem -LiteralPath $SenderExchange -File `
  -Filter '*.offer.json' | Sort-Object LastWriteTime -Descending |
  Select-Object -First 1
if (-not $Offer) { throw '发送端还没有生成 Offer。' }
$Offer | Select-Object Name,Length,LastWriteTime
```

用获授权的文件传输方式，把这个文件复制到公司电脑的：

```text
<接收端包根>\incoming-staging\
```

等待文件传输界面明确显示完成，再在公司电脑“文件窗口”执行：

```powershell
$ReceivedOffer = Get-ChildItem -LiteralPath $ReceiverIncoming -File `
  -Filter '*.offer.json' | Sort-Object LastWriteTime -Descending |
  Select-Object -First 1
if (-not $ReceivedOffer) { throw '接收端 staging 中没有完整 Offer。' }
Move-Item -LiteralPath $ReceivedOffer.FullName `
  -Destination $ReceiverExchange
```

先传入 staging、完成后再移动，可以避免 Answerer 读到尚未传完的半个 JSON。

## 11. 把 Answer 传回当前电脑

公司电脑读到 Offer 后，会在自己的 `session-exchange` 生成同一会话的 `.answer.json`。在公司电脑
“文件窗口”执行：

```powershell
$Answer = Get-ChildItem -LiteralPath $ReceiverExchange -File `
  -Filter '*.answer.json' | Sort-Object LastWriteTime -Descending |
  Select-Object -First 1
if (-not $Answer) { throw '接收端还没有生成 Answer。' }
$Answer | Select-Object Name,Length,LastWriteTime
```

把它复制到当前电脑的：

```text
<发送端包根>\incoming-staging\
```

传输完成后，在当前电脑“文件窗口”执行：

```powershell
$ReceivedAnswer = Get-ChildItem -LiteralPath $SenderIncoming -File `
  -Filter '*.answer.json' | Sort-Object LastWriteTime -Descending |
  Select-Object -First 1
if (-not $ReceivedAnswer) { throw '发送端 staging 中没有完整 Answer。' }
Move-Item -LiteralPath $ReceivedAnswer.FullName `
  -Destination $SenderExchange
```

发送端应用 Answer 后，双方应进入 `connected`。随后发送端输出 `publishing`，公司电脑依次收到媒体、
解码并呈现画面。包内样本较短，Answer 放入发送端后应立即观察公司电脑窗口。

## 12. 判断第一次测试是否真正成功

不要只看 `connected`。完整通过条件如下：

### 发送端

- 出现 `connected`；
- 出现 `publishing`；
- 最终 `completed` 中 `accessUnits`、`sentAccessUnits` 大于 0；
- `sendFailures` 为 0。

### 接收端

- 出现 `connected`；
- 出现 `media_received`；
- 出现 `frame_decoded`；
- 出现 `frame_presented`；
- viewer 窗口显示动态、非黑、比例正常的画面；
- 最终 `completed` 中 `decoded=true`、`presented=true`。

可以在双方进程结束后只筛选安全事件名称：

```powershell
Select-String -LiteralPath '.\manual-logs\sender.stdout.jsonl' `
  -Pattern '"event":"(connected|publishing|completed|failed)"'
```

公司电脑执行：

```powershell
Select-String -LiteralPath '.\manual-logs\receiver.stdout.jsonl' `
  -Pattern '"event":"(connected|media_received|frame_decoded|frame_presented|completed|failed)"'
```

本项目是原生 Qt/libdatachannel 客户端，不是浏览器。Chrome 的 `webrtc-internals` 看不到这两个
PeerConnection；应以客户端事件和实际 viewer 画面为准。

## 13. 换成你自己的视频

当前客户端没有 `--file` 参数。`--source sample` 固定读取发送端 EXE 同级的：

```text
webrtc-assets\sample.mp4
```

因此使用自己的文件时，只在当前电脑发送端替换这个文件；接收端不需要视频副本。

### 13.1 当前兼容要求

| 项目 | 要求 |
| --- | --- |
| 容器 | MP4 |
| 视频编码 | H.264/AVC，不是 H.265/HEVC、AV1 或 VP9 |
| B 帧 | 必须为 0 |
| 单个 Access Unit | 不超过 4 MiB |
| 时间戳 | PTS/DTS 必须存在且可换算 |
| 推荐资格目标 | 1280×720、30 FPS、Constrained Baseline、Level 3.1、GOP 约 30 |
| 音频 | 当前 WebRTC 测试客户端不发送音频；MP4 中的音频会被忽略 |
| 时长 | 客户端不循环；建议先用 10～60 秒短片，且小于 `--timeout-ms` |

如果不满足，发送端可能报告 `h264_required`、`b_frames_unsupported`、`read_failure` 或
`bitstream_filter_failure`。当前客户端不会替你转码；先使用符合要求的文件，或在应用外用已获授权的
媒体工具预处理。本教程不把外部转码器加入产品运行依赖。

如果你的电脑已经有 `ffprobe`，可以只读检查；候选包本身不附带这个命令：

```powershell
ffprobe -v error -select_streams v:0 `
  -show_entries stream=codec_name,profile,level,width,height,avg_frame_rate,has_b_frames `
  -of json '你的实际视频完整路径'
```

至少确认 `codec_name` 是 `h264`、`has_b_frames` 是 `0`。

### 13.2 在发送端安全替换

先确认双方客户端已退出，再在当前电脑执行：

```powershell
$CustomVideo = Read-Host '请输入你的视频完整路径'
if (-not (Test-Path -LiteralPath $CustomVideo -PathType Leaf)) {
  throw '自定义视频不存在。'
}
$VerifiedSample = Join-Path $SenderRoot 'webrtc-assets\sample.mp4'
$SampleBackup = Join-Path $SenderRoot `
  'webrtc-assets\sample.week10-verified.mp4'
if (-not (Test-Path -LiteralPath $SampleBackup -PathType Leaf)) {
  Copy-Item -LiteralPath $VerifiedSample -Destination $SampleBackup
}
Copy-Item -LiteralPath $CustomVideo -Destination $VerifiedSample -Force
Get-Item -LiteralPath $VerifiedSample | Select-Object Name,Length,LastWriteTime
```

然后清理两端旧会话 JSON，完整重复第 8～12 节。不要复用上一轮 Offer/Answer；每轮都是新的 ICE 和
session。

恢复包内样本：

```powershell
Copy-Item -LiteralPath $SampleBackup -Destination $VerifiedSample -Force
```

## 14. 测试结束后的清理

先正常等待短视频播放结束。需要提前结束时，只关闭本轮两个客户端窗口；不要按进程名批量终止其他
目录中的程序。

确认没有本轮客户端进程后，分别在两台电脑执行各自的明确目录清理命令：

发送端：

```powershell
Get-ChildItem -LiteralPath $SenderExchange -File -Filter '*.json' `
  -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem -LiteralPath $SenderIncoming -File -Filter '*.json' `
  -ErrorAction SilentlyContinue | Remove-Item -Force
```

接收端：

```powershell
Get-ChildItem -LiteralPath $ReceiverExchange -File -Filter '*.json' `
  -ErrorAction SilentlyContinue | Remove-Item -Force
Get-ChildItem -LiteralPath $ReceiverIncoming -File -Filter '*.json' `
  -ErrorAction SilentlyContinue | Remove-Item -Force
```

STUN 配置是否保留由公司测试政策决定。它只能保存在本机 `local-config`，不得随日志或会话 JSON
带回仓库。公司电脑上的测试包是否删除，也以公司软件与数据保留政策为准。

## 15. 常见问题按现象排查

### 15.1 双击 EXE 没反应

客户端需要参数，应该从 PowerShell 启动。先运行 `--help`。如果出现 Qt platform plugin 或 DLL
入口点弹窗，通常是只复制了 EXE、混用了旧 DLL，或没有从完整 Week 10 ZIP 展开；重新使用完整包。

### 15.2 `ice_config_not_found` 或 `invalid_ice_config`

- 确认双方都在各自 EXE 同级 `local-config\ice-runtime.json` 创建配置；
- JSON 只能包含 `schemaVersion` 和 `stunUrl`；
- `schemaVersion` 必须为 1；
- 地址必须以 `stun:` 开头，不能保留尖括号占位符或空格。

### 15.3 Answerer 一直等待

检查顺序：发送端是否出现 `description_exported`；Offer 是否已经完整传到接收端 staging；是否移动到
接收端的 `session-exchange`；文件名是否仍以 `.offer.json` 结尾；300 秒时限是否已经结束。

### 15.4 `ambiguous_package` 或读到旧会话

某个 `session-exchange` 中存在多份同角色 JSON。停止双方客户端，只清理两端 exchange/incoming 中
的旧 JSON，然后重新启动一轮。不要猜测哪份旧 Answer 可以复用。

### 15.5 双方有 `connected`，公司电脑却没画面

- 只有 `connected`：网络已建立，但媒体没有完成；继续看 `media_received`；
- 有 `media_received`，没有 `frame_decoded`：检查自定义文件编码、B 帧和 H.264 数据；
- 有 `frame_decoded`，没有 `frame_presented`：确认使用完整 Qt 包、`QT_QPA_PLATFORM=windows`，并
  检查 viewer 窗口是否被远程桌面正确显示；
- 先恢复包内已验证 sample。如果 sample 成功而自定义视频失败，问题位于视频格式，不是 P2P 网络。

### 15.6 `srflx_not_observed` 或连接超时

双方没有获得可用于跨网 Direct 的 srflx candidate，或 NAT/防火墙不允许打洞：

1. 检查 STUN 是否获授权且双方配置正确；
2. 不要关闭 Windows 防火墙或擅自修改公司网络类别；
3. 如出现 Windows 防火墙授权提示，只按公司政策允许当前测试程序和适用网络；
4. 如果仍失败，记录为 `physical_lan_environment`/网络环境未通过。当前 Week 10 没有 TURN，不能把
   `NeedsRelay` 当作 Direct 成功。

### 15.7 视频只播放一次就结束

这是当前设计：MP4 source 按原始时间戳播放一轮，不循环。需要再次观看就开启新一轮，并重新交换新的
Offer/Answer。不要复制旧会话文件。

### 15.8 自定义视频超过五分钟

本文命令使用 `--timeout-ms 300000`。可以把双方改为更大的相同值，最大 `600000`，但第一次测试建议
使用 10～60 秒短片。超过十分钟的文件无法在当前 CLI 上形成正常完整播放资格结果。

## 16. 本章小实验：从验证样本迁移到你的 MP4

### 实验类型

重现 + 故障隔离。

### 实验目的

证明“P2P 网络成功”和“自定义媒体兼容”是两个独立检查点，避免自定义视频失败时错误修改防火墙、
STUN 或信令流程。

### 初始状态

- 包内 sample 已按第 8～12 节在公司电脑成功显示；
- 双方客户端已经退出；
- 两端 exchange/incoming 已清理；
- 自定义文件只存在当前电脑。

### 题目

1. 预测你的文件是否满足 H.264、零 B 帧和时长要求；
2. 备份验证样本并替换发送端 `webrtc-assets\sample.mp4`；
3. 创建全新的 Offer/Answer 完成一轮；
4. 若失败，恢复验证样本再跑一轮，判断是网络问题还是视频问题。

### 完整答案

替换和恢复命令见第 13.2 节；运行命令、Offer/Answer 搬运和通过条件分别见第 8～12 节。答案的关键
不是复制旧 JSON，而是每次重新启动双方、产生新的会话文件。

### 实验通过条件

1. 自定义文件成功时，公司电脑有 `frame_presented` 和动态画面；
2. 自定义文件失败但恢复 sample 后成功时，结论是媒体兼容问题；
3. sample 也失败时，先回到 STUN、Offer/Answer 和网络环境，不把问题归因于视频；
4. 无会话 JSON 被提交、上传到聊天或保存在普通测试报告中。

## 17. 本章检查点

- 候选包检查：两端 `version=0.2.0-beta.1`、`sourceCommit=392d9aa...`；
- 运行命令：发送端 `publisher/offer/source sample`，接收端 `viewer/answer`；
- 信令：Offer 从发送端传到接收端，Answer 再传回发送端；
- 媒体：接收端有 `media_received`、`frame_decoded`、`frame_presented` 和真实动态窗口；
- 清理：两端 exchange/incoming 无残留 JSON，客户端进程已退出；
- 验证状态：包与同机闭环已验证；物理双电脑和自定义视频必须由你按本文执行后再填写结果。

## 18. 当前能力与下一步

完成本文可以证明当前两台电脑和当前网络条件下，文件信令驱动的单路 WebRTC P2P 视频链路能够工作。
它仍不是最终产品形态：

- 还需要 WSS 自动交换 Offer/Answer 与 trickle ICE；
- 还需要 TURN 处理不能 Direct 的企业网络、CGNAT 和受限 UDP；
- 还需要设备/操作员身份与 MQTT 控制目标绑定；
- 还需要真实摄像头和物理 LAN 门禁；
- 还需要把当前固定 `sample.mp4` 输入改成经过评审的产品导入或实时摄像头工作流。

所以本次成功应表述为“当前两台电脑、当前网络、当前视频的 WebRTC P2P 人工测试通过”，不能直接表述
为“WebRTC 已在所有公网环境彻底替代 RTMP”。

## 文档验证记录

| 检查对象 | 执行环境与证据 | 结果 | 状态 |
| --- | --- | --- | --- |
| 候选包 CLI | 当前 Windows 候选包执行 `--help` | 参数与本文一致，退出码 0 | 已验证 |
| 包内 sample | `ffprobe` 只读检查 | H.264 Constrained Baseline、720p30、Level 3.1、零 B 帧 | 已验证 |
| 候选包闭环 | Week 10 `package-result.json` | 两个干净展开副本、本地角色闭环 2/2 | 已验证 |
| 教程结构 | `validate_tutorial_structure.py` | 0 结构错误、0 警告 | 已验证 |
| 当前电脑到公司电脑 | 需要真实两台电脑和授权网络 | 尚无现场结果 | 未验证 |
| 用户自定义 MP4 | 需要用户提供实际文件并执行第 13～16 节 | 尚无现场结果 | 未验证 |

## 本章总结

本章从完整候选包开始，把当前电脑固定为 publisher/offer、公司电脑固定为 viewer/answer；双方先按
网络位置选择 host 或获授权 STUN，再通过 staging 原子搬运 Offer 和 Answer。只有接收端同时出现
媒体接收、解码、呈现事件和真实动态窗口，才算视频 P2P 成功。自定义 MP4 必须在 sample 基线成功后
单独替换，以便把网络故障和 H.264 兼容故障分开。当前没有 TURN 和自动 WSS 信令，物理双机结果必须
由你执行后再记录。
