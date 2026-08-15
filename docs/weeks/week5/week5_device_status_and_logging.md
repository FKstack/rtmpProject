# Week 5：设备状态、重连与分层日志

> 历史记录：本文引用的 Week 4/16 路本机测试脚本已经退役；生产代码和 CTest 未删除。参见[遗留脚本索引](../../archive/legacy_test_scripts.md)。

> 文档分类：Week 5 实现与测试。

## 1. 实现结论

Week 5 保留 Week 3/4 的独立网络线程、共享解码池和稳定 `StreamId`，新增统一状态、
3 秒自动重连，并把原来混合的日志输出拆为：

- `UserMessageService → LogPanel`：只显示普通用户事件；
- `LogManager::logSystem() → system.jsonl`：保存技术运行信息；
- `LogManager::logAudit() → audit.jsonl`：保存重要用户操作。

系统日志和审计日志不会进入底部面板，用户消息也不会直接复制到底层文件。

## 2. 状态与结构化错误

```text
Disconnected
    → Connecting
    → Playing
    → Error
    → Reconnecting
    → Connecting
```

`FFmpegPlayer` 发出：

```cpp
stateChanged(DeviceStatus);
errorOccurred(const PlaybackError &);
reconnectScheduled(int consecutiveFailures, int delayMs);
```

`PlaybackError` 包含稳定错误类别、底层错误码、技术描述和可恢复性。Manager 附加
`StreamId` 后转发，Controller 使用错误类别生成用户提示，完整技术内容只进入
`system.jsonl`。

默认每 3 秒重连，`--max-reconnect-failures 0` 表示无限。非零上限达到后停留在
`Error`，右键重新连接会创建新会话并清零连续失败计数。

## 3. 用户事件面板

底部 Dock 标题为“事件消息”，默认显示，可从“视图 → 事件消息”恢复。面板：

- 最多保留 5000 条；
- 支持暂停自动滚动和清空显示；
- 不显示等级、模块、事件名、错误码或底层库文本；
- 相同设备、事件和失败原因默认 60 秒内只显示一次。

示例：

```text
[20:15:03] 摄像头 01 连接成功
[20:15:10] 摄像头 03 连接失败，请检查网络连接
[20:15:20] 已删除摄像头 02
```

## 4. 系统日志

系统日志用于开发、测试和运维，包含模块、事件、设备 ID、脱敏 URL、错误类别、
底层错误码、重试次数、线程标识和扩展 JSON 字段。

等级为：

```text
Trace < Debug < Info < Warning < Error < Critical
```

Debug 构建默认 `Debug`，Release 默认 `Info`，模块可以在 INI 中单独覆盖。重复状态
和连接错误按默认 60 秒窗口聚合。

## 5. 审计日志

审计日志不使用系统日志等级，当前记录：

- 用户新增设备的成功或失败；
- 用户删除设备的成功、失败或取消；
- 用户手动重新连接及其结果。

每条记录包含操作者、动作、对象类型、对象 ID、结果、原因、修改前后值和来源。
当前没有应用登录系统，因此操作者使用本机 OS 用户名，失败时回退为 `local-user`。

登录、退出、设备编辑、用户配置、权限、导入导出、清除数据、恢复默认和升级动作
已经在公共枚举中预留，但本周不虚构这些尚不存在的业务功能。

## 6. 文件、轮转与清理

默认目录：

```text
QStandardPaths::AppLocalDataLocation/logs/
  system.jsonl
  audit.jsonl
```

| 通道 | 单文件 | 历史文件 | 保留天数 | 总空间 | 队列 |
|---|---:|---:|---:|---:|---:|
| 系统 | 10 MiB | 5 | 14 天 | 64 MiB | 4096 |
| 审计 | 10 MiB | 20 | 180 天 | 256 MiB | 1024 |

初始化和轮转时清理超期或超出总空间的历史文件。队列满时系统日志优先丢弃低等级
记录；审计使用独立队列。退出默认最多刷新 2 秒，文件异常不会使播放功能崩溃。

## 7. 脱敏

进入队列前统一处理 URL、消息、结构化字段和审计前后值：

- 删除 URL 用户名、密码、query 和 fragment；
- 最后一级流密钥替换为 `***`；
- `password`、`token`、`secret`、`privateKey` 和 `credential` 等字段替换为
  `***`。

例如：

```text
rtmp://user:password@example.com:1935/live/private-key?token=secret
```

保存为：

```text
rtmp://example.com:1935/live/***
```

## 8. 配置与命令行

默认配置文件：

```text
QStandardPaths::AppConfigLocation/rtmp-monitor.ini
```

优先级为命令行、INI、编译模式默认值。

| 参数 | 说明 |
|---|---|
| `--max-reconnect-failures <0..1000>` | 自动重连失败上限 |
| `--log-level <trace..critical>` | 覆盖系统最低等级 |
| `--log-dir <path>` | 覆盖系统和审计日志目录 |
| `--log-config <path>` | 覆盖日志 INI 路径 |

完整 INI 示例与维护说明见
[日志体系架构](../../guides/architecture/logging_architecture.md)。

## 9. 自动测试

Windows Debug 当前包含 8 个 CTest 目标，新增覆盖：

- 系统/审计文件分离，系统等级不关闭审计；
- Trace/Critical、模块等级配置、轮转、保留数量、天数和总体空间；
- 队列溢出汇总、关闭刷新、目录创建失败不崩溃；
- URL、文本、JSON 和审计前后值脱敏；
- 用户文案映射、60 秒重复抑制和恢复后重置；
- 新增、删除、连接失败和手动重连的三通路集成；
- 面板不再包含等级筛选或技术内容；
- `PlaybackError` 通过播放器和多路管理器携带正确 `StreamId`。

真实登录和配置修改没有集成测试，因为项目当前没有对应业务模块。

## 10. 程序运行时，日志文件究竟保存在哪里

### 10.1 Windows 默认目录

当前程序设置了：

```cpp
QApplication::setApplicationName("RtmpMonitor");
QApplication::setOrganizationName("RtmpProject");
```

日志目录使用 Qt 的 `QStandardPaths::AppLocalDataLocation`。在当前 Windows
开发环境中，默认位置是：

```text
C:\Users\<用户名>\AppData\Local\RtmpProject\RtmpMonitor\logs
```

不要把 `<用户名>` 原样输入命令。PowerShell 可以通过环境变量找到当前用户的目录：

```powershell
$logRoot = Join-Path $env:LOCALAPPDATA 'RtmpProject\RtmpMonitor\logs'
$logRoot
Get-ChildItem -LiteralPath $logRoot
```

需要在资源管理器中打开时执行：

```powershell
Invoke-Item -LiteralPath $logRoot
```

如果目录还不存在，通常表示当前版本的程序尚未成功启动过，或者启动时使用了
`--log-dir` 指定其他目录。程序初始化日志系统时会自动创建默认目录。

其他操作系统不应照抄 Windows 路径。代码仍然使用
`QStandardPaths::AppLocalDataLocation` 获取平台规定的应用数据目录。需要固定测试
位置时，可以直接覆盖：

```powershell
.\out\build-windows-x64\debug\rtmp_monitor.exe `
  --log-dir .\out\week5-log-manual
```

这时两个日志文件会写到项目的 `out\week5-log-manual`。`out` 已被 Git 忽略，
适合临时测试，但正式运行一般使用默认应用数据目录。

### 10.2 每次启动是否会生成一套新文件

不会。当前实现不会按进程或日期为每次运行创建新目录，而是向两个活动文件持续
追加：

```text
system.jsonl
audit.jsonl
```

因此同一个 `system.jsonl` 可能包含多次程序运行。系统日志使用下面两个事件标识
一次正常运行的边界：

```text
module=application, event=startup
module=application, event=shutdown
```

如果程序崩溃或被强制终止，最后一个 `startup` 后面可能没有对应的 `shutdown`。
这本身也是排查异常退出的重要线索。

活动文件达到大小上限后才会轮转为 `.1`，原 `.1` 继续变为 `.2`。所以：

```text
system.jsonl      当前正在追加，内容最新
system.jsonl.1    最近一个历史文件
system.jsonl.2    更早的历史文件
...
```

`audit.jsonl` 使用相同规则。数字越大，文件越旧。

### 10.3 容易混淆的三种“日志”

| 看到的位置 | 内容 | 是否是程序本地日志 |
|---|---|---|
| 软件底部“事件消息” | 面向普通用户的连接、断开、新增、删除等提示 | 否，只在内存中保留，退出即消失 |
| 默认应用数据目录中的 `system.jsonl` / `audit.jsonl` | 当前程序的系统和审计记录 | 是 |
| `out/.../logs` 下的 `.log` | 测试脚本、FFmpeg、nginx 或进程标准输出 | 不是同一套日志 |

底部面板最多保存 5000 条 `UserMessage`，它不是系统调试控制台，也不会将内容
自动写入 JSONL。

项目早期版本曾写入：

```text
rtmp-monitor.jsonl
```

如果默认目录中仍然存在这个文件，它是旧版遗留文件。当前代码不会继续更新它，
当前版本应阅读 `system.jsonl` 和 `audit.jsonl`。确认程序已经退出且不再需要旧
记录后，可以手动归档或删除旧文件。

## 11. 本地日志有哪些类型

### 11.1 系统运行日志：`system.jsonl`

系统日志用于开发、测试和现场排障，回答的问题是：

> 程序内部发生了什么，发生在哪个模块、哪台设备，底层结果是什么？

系统日志可以记录技术信息，包括状态切换、重连次数、底层错误码和经过脱敏的
连接地址。它有六个等级：

| 等级 | 用途 | 正式环境是否常见 |
|---|---|---|
| `trace` | 最细的执行轨迹 | 默认不写 |
| `debug` | 开发调试信息 | Release 默认不写 |
| `info` | 启动、退出、连接建立等正常关键事件 | 常见 |
| `warning` | 可恢复异常、连接中断、重试 | 常见 |
| `error` | 当前操作失败或严重异常 | 必须关注 |
| `critical` | 不可恢复的关键故障 | 必须优先处理 |

Debug 构建默认最低等级是 `debug`，Release 构建默认最低等级是 `info`。例如设置
最低等级为 `warning` 后，`trace/debug/info` 不再写入，但
`warning/error/critical` 仍然保留。

默认资源保护参数为：

| 项目 | 默认值 |
|---|---:|
| 单个活动文件上限 | 10 MiB |
| 历史文件数量 | 5 个，不含当前活动文件 |
| 保留时间 | 14 天 |
| 当前目录系统日志总量上限 | 64 MiB |
| 异步队列容量 | 4096 条 |

### 11.2 审计日志：`audit.jsonl`

审计日志用于回答：

> 谁在什么时间，对什么对象执行了什么重要操作，结果如何？

审计不是比 `critical` 更高的等级，而是完全独立的日志通道。即使把系统日志最低
等级设为 `critical`，审计记录也不会因此关闭。

当前实际接入的重要操作包括：

- 新增设备：`ADD_DEVICE`；
- 删除设备：`REMOVE_DEVICE`；
- 用户手动重连：`MANUAL_RECONNECT`。

代码还预留了登录、退出、修改设备、配置、权限、导入导出、清理、恢复默认和升级
等审计动作，但项目当前没有这些业务界面时，不会凭空生成对应记录。

默认资源保护参数为：

| 项目 | 默认值 |
|---|---:|
| 单个活动文件上限 | 10 MiB |
| 历史文件数量 | 20 个，不含当前活动文件 |
| 保留时间 | 180 天 |
| 当前目录审计日志总量上限 | 256 MiB |
| 独立异步队列容量 | 1024 条 |

审计比普通系统日志保留得更久。系统和审计都有独立队列和文件线程，不会因为其中
一个文件较慢而直接在媒体线程中执行磁盘写入。

### 11.3 为什么底部事件消息不是第三种文件

`UserMessageService` 产生的内容只用于界面，例如：

```text
摄像头 03 连接失败，请检查网络连接
```

它刻意删除了协议、端口、FFmpeg 错误和底层错误码。系统日志中同一事件则可能记录：

```text
module=media
event=stream_error
deviceName=Camera 03
fields.errorCode=...
fields.nativeErrorCode=...
```

两段内容服务于不同读者，不能互相复制。因此当前只有系统和审计两种 JSONL 文件，
不存在“用户消息日志文件”。

## 12. JSONL 文件和字段怎么理解

### 12.1 什么是 JSONL

JSONL（JSON Lines）的规则很简单：

- 一行就是一条完整 JSON 记录；
- 下一行是下一条记录；
- 文件按 UTF-8 保存；
- 不需要把整个文件一次性加载进内存。

示意：

```json
{"channel":"system","timestampUtc":"2026-07-29T01:00:00.123Z","level":"info","module":"application","event":"startup","message":"RtmpMonitor started.","threadId":"1234","repeatedCount":1}
{"channel":"system","timestampUtc":"2026-07-29T01:00:01.456Z","level":"info","module":"device","event":"status_changed","message":"Device status changed.","deviceId":"3","deviceName":"Camera 03","fields":{"state":"playing"},"threadId":"1234","repeatedCount":1}
```

文件里的时间使用 UTC，末尾通常带 `Z`。底部事件面板为了便于普通用户阅读，会
转换为本地时间显示。

### 12.2 系统日志字段

| 字段 | 含义 |
|---|---|
| `schemaVersion` | JSON 结构版本，当前为 1 |
| `channel` | 固定为 `system` |
| `timestampUtc` | UTC 毫秒时间 |
| `level` | `trace` 到 `critical` |
| `module` | 产生事件的模块，如 `application`、`device`、`media` |
| `event` | 稳定的机器可读事件名，如 `startup`、`stream_error` |
| `message` | 给开发和运维人员看的技术描述 |
| `threadId` | 记录调用发生时的线程标识 |
| `deviceId` | 稳定 `StreamId` 的文本形式；无设备上下文时省略 |
| `deviceName` | 当时使用的设备显示名 |
| `url` | 已删除凭据、查询参数并遮蔽流密钥的地址 |
| `fields` | 每种事件自己的扩展 JSON 字段 |
| `repeatedCount` | 聚合后代表的重复次数，普通记录为 1 |

排查设备状态时，最常看的组合是：

```text
timestampUtc + deviceId + event + fields.state
```

排查媒体错误时，最常看的组合是：

```text
timestampUtc + deviceId + event + message + fields
```

### 12.3 审计日志字段

| 字段 | 含义 |
|---|---|
| `schemaVersion` | JSON 结构版本，当前为 1 |
| `channel` | 固定为 `audit` |
| `timestampUtc` | 操作记录时间 |
| `actor` | 操作者；当前通常是本机 OS 用户名 |
| `action` | `ADD_DEVICE`、`REMOVE_DEVICE` 等动作 |
| `targetType` | 对象类型，当前设备为 `Camera` |
| `targetId` | 对象的稳定 ID；失败较早时可能使用输入名称 |
| `result` | `SUCCESS`、`FAILURE` 或 `CANCELLED` |
| `reason` | 失败或取消的原因摘要 |
| `before` | 删除或修改前的重要值 |
| `after` | 新增或修改后的重要值 |
| `source` | 操作来源，当前界面操作为 `local-ui` |

`before` 和 `after` 也经过脱敏。这里不应出现明文密码、完整 Token、私钥或包含
鉴权信息的完整 URL。

## 13. 如何阅读日志文件

下面命令都假设 PowerShell 当前目录是项目根目录，但默认日志目录与项目目录无关。

### 13.1 先定义路径并确认文件存在

```powershell
$logRoot = Join-Path $env:LOCALAPPDATA 'RtmpProject\RtmpMonitor\logs'
$systemLog = Join-Path $logRoot 'system.jsonl'
$auditLog = Join-Path $logRoot 'audit.jsonl'

Test-Path -LiteralPath $systemLog
Test-Path -LiteralPath $auditLog
Get-ChildItem -LiteralPath $logRoot
```

如果启动时使用了 `--log-dir`，请把 `$logRoot` 改成对应目录。例如：

```powershell
$logRoot = Resolve-Path '.\out\week5-log-manual'
```

### 13.2 查看最后 20 条系统日志

```powershell
Get-Content -LiteralPath $systemLog -Encoding UTF8 -Tail 20
```

直接显示的是紧凑 JSON。它适合快速确认文件是否继续增长，但不适合长时间逐字段
阅读。

### 13.3 程序运行时实时跟踪

先启动程序，再在另一个 PowerShell 窗口执行：

```powershell
Get-Content -LiteralPath $systemLog -Encoding UTF8 -Tail 20 -Wait
```

`-Tail 20` 先显示已有的最后 20 行，`-Wait` 会等待新内容并持续打印。结束跟踪按
`Ctrl+C`，它只停止 PowerShell 的查看命令，不会停止播放器。

审计日志也可以同样跟踪：

```powershell
Get-Content -LiteralPath $auditLog -Encoding UTF8 -Tail 10 -Wait
```

### 13.4 把最后一条 JSON 转换成易读形式

```powershell
Get-Content -LiteralPath $systemLog -Encoding UTF8 |
  Select-Object -Last 1 |
  ConvertFrom-Json |
  Format-List *
```

`ConvertFrom-Json` 把一行 JSON 转换成 PowerShell 对象，之后可以按字段筛选。

### 13.5 只看 Error 和 Critical

```powershell
Get-Content -LiteralPath $systemLog -Encoding UTF8 |
  ForEach-Object { $_ | ConvertFrom-Json } |
  Where-Object { $_.level -in @('error', 'critical') } |
  Format-Table timestampUtc, module, event, deviceId, message -AutoSize
```

Warning 经常代表可自动恢复的断流。如果也要查看，将第一行筛选条件改成：

```powershell
$_.level -in @('warning', 'error', 'critical')
```

### 13.6 按模块、事件或设备筛选

只看媒体模块：

```powershell
Get-Content -LiteralPath $systemLog -Encoding UTF8 |
  ForEach-Object { $_ | ConvertFrom-Json } |
  Where-Object module -eq 'media' |
  Format-Table timestampUtc, event, deviceId, deviceName, message -AutoSize
```

只看自动重连安排：

```powershell
Get-Content -LiteralPath $systemLog -Encoding UTF8 |
  ForEach-Object { $_ | ConvertFrom-Json } |
  Where-Object event -eq 'reconnect_scheduled' |
  Format-Table timestampUtc, deviceId, deviceName, fields -AutoSize
```

只看设备 ID 为 3 的记录：

```powershell
Get-Content -LiteralPath $systemLog -Encoding UTF8 |
  ForEach-Object { $_ | ConvertFrom-Json } |
  Where-Object deviceId -eq '3' |
  Format-Table timestampUtc, level, module, event, message -AutoSize
```

建议优先使用 `deviceId`，因为用户可以修改显示名称，而 `StreamId` 在当前绑定生命
周期内保持稳定。

### 13.7 只看本次程序运行

启动测试前，在同一个 PowerShell 窗口记录 UTC 时间：

```powershell
$testStartedAtUtc = [DateTime]::UtcNow
$testStartedAtUtc
```

测试后筛选这个时间点之后的系统记录：

```powershell
Get-Content -LiteralPath $systemLog -Encoding UTF8 |
  ForEach-Object { $_ | ConvertFrom-Json } |
  Where-Object {
    [DateTime]::Parse($_.timestampUtc).ToUniversalTime() -ge $testStartedAtUtc
  } |
  Format-Table timestampUtc, level, module, event, deviceId, message -AutoSize
```

也可以先找出所有启动和退出边界：

```powershell
$records = Get-Content -LiteralPath $systemLog -Encoding UTF8 |
  ForEach-Object { $_ | ConvertFrom-Json }

$records |
  Where-Object {
    $_.module -eq 'application' -and
    $_.event -in @('startup', 'shutdown')
  } |
  Format-Table timestampUtc, event, message -AutoSize
```

再取最后一次启动之后的记录：

```powershell
$latestStart = $records |
  Where-Object {
    $_.module -eq 'application' -and $_.event -eq 'startup'
  } |
  Select-Object -Last 1

$records |
  Where-Object {
    [DateTime]::Parse($_.timestampUtc) -ge
      [DateTime]::Parse($latestStart.timestampUtc)
  } |
  Format-Table timestampUtc, level, module, event, deviceId, message -AutoSize
```

### 13.8 阅读审计操作

查看新增、删除和手动重连：

```powershell
$importantActions = @(
  'ADD_DEVICE',
  'REMOVE_DEVICE',
  'MANUAL_RECONNECT'
)

Get-Content -LiteralPath $auditLog -Encoding UTF8 |
  ForEach-Object { $_ | ConvertFrom-Json } |
  Where-Object { $_.action -in $importantActions } |
  Format-Table timestampUtc, actor, action, targetType, targetId, result, reason `
    -AutoSize
```

只看失败和取消：

```powershell
Get-Content -LiteralPath $auditLog -Encoding UTF8 |
  ForEach-Object { $_ | ConvertFrom-Json } |
  Where-Object { $_.result -in @('FAILURE', 'CANCELLED') } |
  Format-List timestampUtc, actor, action, targetId, result, reason, before, after
```

### 13.9 连同轮转文件一起阅读

系统日志最多保留 5 个历史文件。以下命令按“最旧到最新”合并读取：

```powershell
$systemHistory = 5..1 |
  ForEach-Object { Join-Path $logRoot "system.jsonl.$_" } |
  Where-Object { Test-Path -LiteralPath $_ }

$systemFiles = @($systemHistory) + $systemLog
$allSystemRecords = $systemFiles |
  ForEach-Object {
    Get-Content -LiteralPath $_ -Encoding UTF8
  } |
  ForEach-Object { $_ | ConvertFrom-Json }

$allSystemRecords |
  Format-Table timestampUtc, level, module, event, deviceId, message -AutoSize
```

审计日志把 `5..1` 改成 `20..1`，文件名改为 `audit.jsonl` 即可。

### 13.10 使用什么工具阅读

| 工具 | 适合场景 |
|---|---|
| VS Code | 搜索文本、查看少量记录、比较轮转文件 |
| 记事本 | 临时确认文件内容，不适合大文件和复杂筛选 |
| PowerShell | 实时跟踪、解析 JSON、按字段和时间筛选，最推荐 |

虽然日志已经执行敏感信息脱敏，但仍可能包含设备名称、操作者、状态时间和网络主机
信息。把日志发送给其他人之前，应再次搜索 `password`、`token`、`secret`、
`privateKey` 等关键词，并确认分享范围。

## 14. 日志相关代码有哪些类

### 14.1 总体数据流

```mermaid
flowchart LR
    Player["FFmpegPlayer<br/>DeviceStatus / PlaybackError"]
    Manager["MultiStreamPlaybackManager<br/>附加 StreamId"]
    Controller["StreamConnectionController<br/>决定输出通路"]
    UserService["UserMessageService<br/>文案映射与去重"]
    Panel["LogPanel<br/>内存中的事件消息"]
    LogManager["LogManager<br/>分流、过滤、脱敏"]
    System["system.jsonl"]
    Audit["audit.jsonl"]

    Player --> Manager --> Controller
    Controller --> UserService --> Panel
    Controller -->|"logSystem()"| LogManager --> System
    Controller -->|"logAudit()"| LogManager --> Audit
```

关键原则是：播放器只报告结构化状态和错误，不直接操作窗口或文件；Controller
知道事件来自自动流程还是用户操作，因此由它决定是否需要用户消息和审计记录。

### 14.2 类与数据类型的职责

| 类或类型 | 源码入口 | 负责什么 |
|---|---|---|
| `LogConfiguration` | [`LogConfiguration.h`](../../../include/common/logging/LogConfiguration.h)、[`LogConfiguration.cpp`](../../../src/common/logging/LogConfiguration.cpp) | 生成默认配置，从 INI 读取目录、等级、轮转和队列参数 |
| `LoggingOptions` | [`LogTypes.h`](../../../include/common/logging/LogTypes.h) | 整个日志系统的配置集合 |
| `LogFileOptions` | [`LogTypes.h`](../../../include/common/logging/LogTypes.h) | 单个通道的文件大小、历史数量、天数、总空间和队列容量 |
| `LogContext` | [`LogTypes.h`](../../../include/common/logging/LogTypes.h) | 携带设备 ID、设备名和待脱敏 URL |
| `SystemLogEntry` | [`LogTypes.h`](../../../include/common/logging/LogTypes.h) | 一条系统日志在内存中的结构 |
| `AuditRecord` | [`LogTypes.h`](../../../include/common/logging/LogTypes.h) | 一条审计记录在内存中的结构 |
| `AuditAction` / `AuditResult` | [`LogTypes.h`](../../../include/common/logging/LogTypes.h) | 限定审计动作和结果，避免业务代码随意拼写 |
| `LogManager` | [`LogManager.h`](../../../include/common/logging/LogManager.h)、[`LogManager.cpp`](../../../src/common/logging/LogManager.cpp) | 接收系统/审计记录，执行等级过滤、重复聚合、脱敏和异步投递 |
| `SensitiveDataSanitizer` | [`SensitiveDataSanitizer.h`](../../../include/common/logging/SensitiveDataSanitizer.h)、[`SensitiveDataSanitizer.cpp`](../../../src/common/logging/SensitiveDataSanitizer.cpp) | 脱敏 URL、文本、JSON 字段和审计前后值 |
| `AsyncJsonlSink` | [`LogManager.cpp`](../../../src/common/logging/LogManager.cpp) | 内部实现；在独立线程消费有界队列，写 JSONL、轮转、清理并尝试故障恢复 |
| `UserMessageService` | [`UserMessageService.h`](../../../include/common/logging/UserMessageService.h)、[`UserMessageService.cpp`](../../../src/common/logging/UserMessageService.cpp) | 把 `UserEvent` 映射成大众中文并抑制重复消息 |
| `LogPanel` | [`LogPanel.h`](../../../include/common/ui/LogPanel.h)、[`LogPanel.cpp`](../../../src/common/ui/LogPanel.cpp) | 只显示 `UserMessage`，负责暂停滚动、清空和 5000 条上限 |
| `StreamConnectionController` | [`StreamConnectionController.cpp`](../../../src/common/app/StreamConnectionController.cpp) | 接收设备结果，分别调用用户消息、系统日志和审计接口 |

### 14.3 推荐阅读顺序

初学者不要直接从较长的 `LogManager.cpp` 中间开始。推荐顺序：

1. 阅读 `LogTypes.h`，先认识日志记录和配置的数据形状。
2. 阅读 `UserMessageTypes.h`，理解用户消息为什么不含技术错误。
3. 阅读 `LogManager.h`，只看对外接口 `initialize()`、`logSystem()`、
   `logAudit()` 和 `shutdown()`。
4. 阅读 `StreamConnectionController.cpp` 中对这些接口的调用，理解“何时记录”。
5. 阅读 `LogConfiguration.cpp`，理解默认值和 INI 覆盖。
6. 最后阅读 `LogManager.cpp` 中的 `AsyncJsonlSink`，研究线程、队列、轮转和失败
   恢复。

## 15. 如何用项目脚本测试 Week 5 日志

所有命令都应在项目根目录 `E:\rtmpProject` 中执行。真实流测试依赖本机配置的 Qt、
MSVC、FFmpeg 和 nginx-rtmp 路径；如果安装位置不同，需要使用脚本参数覆盖。

### 15.1 先理解四种测试分别证明什么

| 测试 | 能证明什么 | 不能单独证明什么 |
|---|---|---|
| 日志相关 CTest | 分流、过滤、轮转、清理、脱敏、用户文案和 Controller 决策 | 真实网络断流 |
| 四路人工测试 | 单路断流、3 秒重连、界面提示和真实系统日志 | 16 路长时间资源上限 |
| 16 路视频测试 | 多路稳定性、故障注入、日志增长和重复抑制 | 用户审计操作是否正确 |
| RTMP 链路或延迟脚本 | 外部服务、推流、拉流或端到端延迟 | 应用日志分层是否正确 |

因此推荐顺序是：先跑 Unit，再做四路人工断流，最后按需要运行 16 路长时间测试。

### 15.2 自动构建并运行全部 CTest

先只检查环境：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run_16_stream_automated_tests.ps1 `
  -Action Check `
  -Suite Unit
```

这个步骤检查脚本、Visual C++ 环境和构建目录，不执行真实 16 路推流。

执行 Debug 构建和完整 CTest：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\run_16_stream_automated_tests.ps1 `
  -Action Run `
  -Suite Unit
```

脚本会在需要时配置 CMake，然后执行：

```text
cmake --build ...
ctest --output-on-failure
```

当前与日志直接相关的四个测试是：

| CTest 目标 | 主要验证内容 |
|---|---|
| `rtmp_monitor_logging_test` | 系统/审计分离、等级、脱敏、轮转、清理、队列和异常目录 |
| `rtmp_monitor_user_message_test` | 大众文案、错误映射、60 秒抑制和登录预留事件 |
| `rtmp_monitor_connection_controller_test` | 新增、删除、连接失败和手动重连的三通路决策 |
| `rtmp_monitor_log_panel_test` | 面板只显示用户消息、清空、暂停和 5000 条上限 |

`Suite Unit` 会运行全部 CTest，而不仅是这四个目标，这样可以同时确认日志改动没有
破坏播放器、网格和多路管理器。

### 15.3 只运行日志相关 CTest

已经完成 Debug 构建时，可以直接运行：

```powershell
ctest `
  --test-dir .\out\build-windows-x64\debug `
  -C Debug `
  -R 'rtmp_monitor_(logging|user_message|connection_controller|log_panel)_test' `
  --output-on-failure
```

如果失败，控制台会直接显示断言信息。还可以查看 CTest 保存的最后一次完整输出：

```powershell
Get-Content `
  -LiteralPath .\out\build-windows-x64\debug\Testing\Temporary\LastTest.log `
  -Encoding UTF8 `
  -Tail 100
```

单元测试使用临时目录，不会把测试记录混入正式的
`%LOCALAPPDATA%\RtmpProject\RtmpMonitor\logs`。

### 15.4 四路真实断流和重连测试

先在当前 PowerShell 窗口记录本轮开始时间：

```powershell
$testStartedAtUtc = [DateTime]::UtcNow
```

检查环境：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_week4_multi_stream.ps1 `
  -Action Check
```

启动 nginx、四路带编号的推流和 Qt 程序：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_week4_multi_stream.ps1 `
  -Action Start
```

查看脚本管理的进程状态：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_week4_multi_stream.ps1 `
  -Action Status
```

此时应确认 Camera 01～04 都进入播放。脚本没有传入 `--log-dir`，所以 Qt 程序仍把
系统和审计日志写入默认应用数据目录，而不是
`out\week4-multi-stream-manual`。

停止 Camera 03 的推流：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_week4_multi_stream.ps1 `
  -Action StopCamera03
```

观察至少一个 3 秒重连周期，验收以下内容：

- Camera 03 清除旧画面并显示断开或重连提示；
- Camera 01、02、04 继续播放；
- 底部事件消息使用大众语言，不出现 RTMP、FFmpeg、端口或错误码；
- `system.jsonl` 出现 Camera 03 的 `stream_error`、`status_changed` 和
  `reconnect_scheduled`；
- 连续失败不会每次都向底部面板重复显示相同消息；
- 这是脚本从应用外部停止 publisher，不是用户修改应用配置，所以
  `audit.jsonl` 不应新增设备操作记录。

查看本轮 Camera 03 的系统日志：

```powershell
$logRoot = Join-Path $env:LOCALAPPDATA 'RtmpProject\RtmpMonitor\logs'
$systemLog = Join-Path $logRoot 'system.jsonl'

Get-Content -LiteralPath $systemLog -Encoding UTF8 |
  ForEach-Object { $_ | ConvertFrom-Json } |
  Where-Object {
    [DateTime]::Parse($_.timestampUtc).ToUniversalTime() -ge
      $testStartedAtUtc -and
    $_.deviceName -eq 'Camera 03'
  } |
  Format-Table timestampUtc, level, event, message, fields -AutoSize
```

恢复 Camera 03：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_week4_multi_stream.ps1 `
  -Action StartCamera03
```

程序应在最多约 8 秒内重新得到画面。系统日志中应看到
`status_changed` 的 `fields.state=playing`，底部面板显示一次连接恢复消息。

最后安全停止本脚本拥有的进程：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_week4_multi_stream.ps1 `
  -Action Stop
```

正常关闭后，系统日志应有 `application/shutdown`。`Stop` 只处理状态文件中经过
PID、进程名、路径和启动时间核验的进程。

### 15.5 用户操作与审计日志测试

四路程序运行时，在界面中分别执行：

1. 使用“新增连接”添加一台设备；
2. 对该设备右键选择“重新连接”；
3. 使用界面删除该设备，并在确认框中确认。

然后读取本轮审计日志：

```powershell
$auditLog = Join-Path $logRoot 'audit.jsonl'

Get-Content -LiteralPath $auditLog -Encoding UTF8 |
  ForEach-Object { $_ | ConvertFrom-Json } |
  Where-Object {
    [DateTime]::Parse($_.timestampUtc).ToUniversalTime() -ge
      $testStartedAtUtc
  } |
  Format-Table timestampUtc, actor, action, targetId, result, reason, source `
    -AutoSize
```

应分别看到：

```text
ADD_DEVICE
MANUAL_RECONNECT
REMOVE_DEVICE
```

同时确认：

- 底部面板显示“已新增……”“正在重新连接……”“已删除……”等用户文案；
- 系统日志保留 `connection_added`、`manual_reconnect`、
  `connection_removed` 等技术事件；
- 审计记录包含 `actor`、`targetId`、`result` 和 `source=local-ui`；
- 新增成功表示会话绑定建立成功，不保证第一帧已经播放；
- 程序启动时通过 `--url` 预装的四路连接不属于用户界面操作，不写
  `ADD_DEVICE` 审计。

如果在删除确认框中选择取消，应看到 `REMOVE_DEVICE/CANCELLED`，设备仍然保留。

### 15.6 16 路视频压力与故障注入

首先检查工具和素材：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_video.ps1 `
  -Action Check
```

第一次运行或素材改变后准备 16 路预编码素材：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_video.ps1 `
  -Action Prepare
```

分阶段人工运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_video.ps1 `
  -Action Start

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_video.ps1 `
  -Action Status

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_video.ps1 `
  -Action Test
```

`Test` 最多等待 55 秒，要求应用、16 个 publisher、监听端口、16 条指标和
16 路 Playing 同时健康。

对第 3 路注入故障并恢复：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_video.ps1 `
  -Action StopStream `
  -StreamNumber 3

powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_video.ps1 `
  -Action StartStream `
  -StreamNumber 3
```

这适合观察：

- 单路故障是否影响其他 15 路；
- 系统日志是否只在状态变化时写入；
- 连续重连是否被聚合；
- 长时间运行后日志文件是否保持在轮转和总空间限制内。

停止并安全清理：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_video.ps1 `
  -Action Stop
```

一键自动验收会启动 16 路、采样指定时长，并在中途自动停止和恢复 Camera 03：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\test_16_stream_video.ps1 `
  -Action RunAutomated `
  -DurationSeconds 600 `
  -WarmupSeconds 20
```

这个测试适合验证多路稳定性和日志限频，但它不会替代
`rtmp_monitor_logging_test` 对轮转、清理、脱敏和异常目录的确定性单元测试。

脚本输出中的：

```text
out\logs\16-stream-video
```

保存 FFmpeg、nginx、应用 stdout/stderr 等测试辅助日志。应用自己的
`system.jsonl` 和 `audit.jsonl` 仍在默认应用数据目录。

### 15.7 另外两个脚本的边界

历史脚本 `test_16_stream_live_latency.ps1`（见[遗留脚本索引](../../archive/legacy_test_scripts.md)）
主要验证双屏实况源到显示延迟、CPU、内存和 UI 调度间隔。它会启动应用并间接产生
系统日志，但不能单独证明用户消息、审计和日志轮转正确。

历史脚本 `verify_rtmp_chain.ps1` 主要验证 nginx、
FFmpeg 推流和 FFmpeg 拉流链路。它的错误日志属于外部服务或 FFmpeg，不是
RtmpMonitor 的 `system.jsonl`。

遇到“程序连接不上”时，可以先用 `verify_rtmp_chain.ps1` 判断问题在外部链路还是
应用内部，再结合 `system.jsonl` 排查；不要把外部工具输出误认为应用系统日志。

### 15.8 推荐的 Week 5 完整验收顺序

```text
1. run_16_stream_automated_tests.ps1 -Action Check -Suite Unit
2. run_16_stream_automated_tests.ps1 -Action Run -Suite Unit
3. test_week4_multi_stream.ps1 -Action Check
4. 记录 $testStartedAtUtc
5. Start → StopCamera03 → 查看系统日志 → StartCamera03
6. 在 UI 中执行新增、手动重连、删除 → 查看审计日志
7. test_week4_multi_stream.ps1 -Action Stop
8. 有性能验收需求时再运行 16 路测试
```

这样可以依次回答：

- 日志库本身是否正确；
- 真实断流是否触发正确状态和系统日志；
- 普通用户是否只看到大众语言；
- 用户操作是否进入独立审计文件；
- 多路长时间运行是否会被日志 I/O 或日志增长拖垮。
