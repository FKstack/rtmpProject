<#
.SYNOPSIS
    为 Week 4 四路 RTMP 播放准备分阶段人工验收环境。

.DESCRIPTION
    脚本只自动完成环境检查、nginx-rtmp、四路带标签测试流、Qt 程序、故障注入、
    状态输出和测试进程清理。拖拽交换、双击全屏和 Alt+F4 关闭仍由测试人员手动
    操作，以验证真实鼠标、键盘和窗口交互。

    运行状态和日志保存在 out/week4-multi-stream-manual，此目录已被 Git 忽略。
    Stop 只处理状态文件中记录且 PID、进程名、路径和启动时间都匹配的进程。

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File .\scripts\test_week4_multi_stream.ps1 -Action Check

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File .\scripts\test_week4_multi_stream.ps1 -Action Start

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File .\scripts\test_week4_multi_stream.ps1 -Action StopCamera03

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File .\scripts\test_week4_multi_stream.ps1 -Action Stop
#>

[CmdletBinding()]
param(
    [ValidateSet(
        "Check",
        "Start",
        "Status",
        "StopCamera03",
        "StartCamera03",
        "StopStreams",
        "StartStreams",
        "StartApp",
        "Stop"
    )]
    [string]$Action = "Check",

    [string]$FfmpegPath = "E:\DevTools\ffmpeg-8.1.2-full_build\bin\ffmpeg.exe",
    [string]$NginxRoot = "E:\DevTools\nginx-rtmp",
    [string]$InputFile = "",
    [string]$AppPath = "",
    [string]$OutputRoot = "",
    [string[]]$StreamUrls = @(
        "rtmp://127.0.0.1:1935/live/camera001",
        "rtmp://127.0.0.1:1935/live/camera002",
        "rtmp://127.0.0.1:1935/live/camera003",
        "rtmp://127.0.0.1:1935/live/camera004"
    )
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptVersion = "2026-07-27-v1"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$NginxExe = Join-Path $NginxRoot "sbin\nginx.exe"
$NginxConfigRelativePath = "conf\nginx.conf"
$NginxConfigPath = Join-Path $NginxRoot $NginxConfigRelativePath
$FontPath = "C:\Windows\Fonts\arial.ttf"

if ([string]::IsNullOrWhiteSpace($InputFile)) {
    $InputFile = Join-Path $ProjectRoot "testdata\test.mp4"
}
if ([string]::IsNullOrWhiteSpace($AppPath)) {
    $AppPath = Join-Path $ProjectRoot "out\build-windows-x64\debug\rtmp_monitor.exe"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $ProjectRoot "out\week4-multi-stream-manual"
}

$StatePath = Join-Path $OutputRoot "state.json"

function Write-Step {
    param([Parameter(Mandatory)][string]$Message)

    Write-Host ""
    Write-Host "============================================================" -ForegroundColor DarkGray
    Write-Host $Message -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor DarkGray
}

function Assert-File {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description 不存在：$Path"
    }
}

function Assert-StreamUrls {
    if ($StreamUrls.Count -ne 4) {
        throw "StreamUrls 必须恰好包含四个 RTMP URL，当前数量：$($StreamUrls.Count)。"
    }

    foreach ($urlText in $StreamUrls) {
        $parsed = $null
        if (-not [Uri]::TryCreate($urlText, [UriKind]::Absolute, [ref]$parsed) -or
            $parsed.Scheme -ne "rtmp" -or
            [string]::IsNullOrWhiteSpace($parsed.Host) -or
            [string]::IsNullOrWhiteSpace($parsed.AbsolutePath)) {
            throw "无效的 RTMP URL：$urlText"
        }
    }
}

function ConvertTo-NativeArgument {
    param([AllowEmptyString()][Parameter(Mandatory)][string]$Value)

    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') {
        return $Value
    }

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $backslashCount = 0

    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]92) {
            ++$backslashCount
            continue
        }

        if ($character -eq [char]34) {
            [void]$builder.Append([char]92, $backslashCount * 2 + 1)
            [void]$builder.Append([char]34)
            $backslashCount = 0
            continue
        }

        if ($backslashCount -gt 0) {
            [void]$builder.Append([char]92, $backslashCount)
            $backslashCount = 0
        }
        [void]$builder.Append($character)
    }

    if ($backslashCount -gt 0) {
        [void]$builder.Append([char]92, $backslashCount * 2)
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Join-NativeArguments {
    param([Parameter(Mandatory)][string[]]$Arguments)

    return (($Arguments | ForEach-Object {
        ConvertTo-NativeArgument -Value $_
    }) -join " ")
}

function Test-RtmpPort {
    $client = [System.Net.Sockets.TcpClient]::new()
    try {
        $task = $client.ConnectAsync("127.0.0.1", 1935)
        if (-not $task.Wait(500)) {
            return $false
        }
        return $client.Connected
    }
    catch {
        return $false
    }
    finally {
        $client.Dispose()
    }
}

function Wait-RtmpPort {
    param(
        [Parameter(Mandatory)][bool]$ExpectedListening,
        [int]$TimeoutMs = 10000
    )

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ((Test-RtmpPort) -eq $ExpectedListening) {
            return
        }
        Start-Sleep -Milliseconds 100
    }

    $expectedText = if ($ExpectedListening) { "开始监听" } else { "停止监听" }
    throw "RTMP 端口 1935 未在 ${TimeoutMs}ms 内$expectedText。"
}

function New-ProcessRecord {
    param(
        [Parameter(Mandatory)][System.Diagnostics.Process]$Process,
        [Parameter(Mandatory)][string]$ExecutablePath
    )

    $Process.Refresh()
    return [PSCustomObject]@{
        Pid = $Process.Id
        ProcessName = $Process.ProcessName
        ExecutablePath = [IO.Path]::GetFullPath($ExecutablePath)
        StartedAtUtc = $Process.StartTime.ToUniversalTime().ToString("O")
    }
}

function Get-OwnedProcess {
    param(
        [AllowNull()][object]$Record,
        [Parameter(Mandatory)][string]$ExpectedProcessName
    )

    if ($null -eq $Record -or [int]$Record.Pid -le 0) {
        return $null
    }

    $process = Get-Process -Id ([int]$Record.Pid) -ErrorAction SilentlyContinue
    if ($null -eq $process) {
        return $null
    }

    $expectedNameWithoutExtension =
        [IO.Path]::GetFileNameWithoutExtension($ExpectedProcessName)
    if (-not $process.ProcessName.Equals(
            $expectedNameWithoutExtension,
            [StringComparison]::OrdinalIgnoreCase
        )) {
        return $null
    }

    try {
        $actualPath = [IO.Path]::GetFullPath($process.Path)
        $expectedPath = [IO.Path]::GetFullPath([string]$Record.ExecutablePath)
        if (-not $actualPath.Equals($expectedPath, [StringComparison]::OrdinalIgnoreCase)) {
            return $null
        }

        $expectedStartedAt =
            [DateTime]::Parse(
                [string]$Record.StartedAtUtc,
                [Globalization.CultureInfo]::InvariantCulture,
                [Globalization.DateTimeStyles]::RoundtripKind
            ).ToUniversalTime()
        $actualStartedAt = $process.StartTime.ToUniversalTime()
        if ([Math]::Abs(($actualStartedAt - $expectedStartedAt).TotalSeconds) -gt 1.0) {
            return $null
        }
    }
    catch {
        return $null
    }

    return $process
}

function Clear-ProcessRecord {
    param([Parameter(Mandatory)][object]$Record)

    $Record.Pid = 0
    $Record.StartedAtUtc = $null
}

function Stop-OwnedProcess {
    param(
        [Parameter(Mandatory)][object]$Record,
        [Parameter(Mandatory)][string]$ExpectedProcessName,
        [switch]$TryCloseMainWindow,
        [int]$GracefulTimeoutMs = 5000
    )

    $process = Get-OwnedProcess `
        -Record $Record `
        -ExpectedProcessName $ExpectedProcessName
    if ($null -eq $process) {
        Clear-ProcessRecord -Record $Record
        return
    }

    if ($TryCloseMainWindow) {
        try {
            if ($process.CloseMainWindow() -and $process.WaitForExit($GracefulTimeoutMs)) {
                Write-Host "[通过] 应用已正常关闭：PID $($process.Id)"
                Clear-ProcessRecord -Record $Record
                return
            }
        }
        catch {
            Write-Warning "请求应用正常关闭失败：$($_.Exception.Message)"
        }
    }

    $process = Get-OwnedProcess `
        -Record $Record `
        -ExpectedProcessName $ExpectedProcessName
    if ($null -ne $process) {
        Write-Warning "强制结束本脚本记录的进程：$ExpectedProcessName PID $($process.Id)"
        $process.Kill()
        if (-not $process.WaitForExit(3000)) {
            throw "进程未能退出：$ExpectedProcessName PID $($process.Id)"
        }
    }
    Clear-ProcessRecord -Record $Record
}

function Save-State {
    param([Parameter(Mandatory)][object]$State)

    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
    $State | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $StatePath -Encoding UTF8
}

function Get-State {
    if (-not (Test-Path -LiteralPath $StatePath -PathType Leaf)) {
        throw "找不到测试状态：$StatePath。请先执行 -Action Start。"
    }

    $state = Get-Content -LiteralPath $StatePath -Raw -Encoding UTF8 |
        ConvertFrom-Json
    if ([string]$state.ScriptVersion -ne $ScriptVersion) {
        throw "状态文件版本不匹配。请确认没有活动测试进程后删除：$StatePath"
    }
    return $state
}

function Test-StateHasActiveProcesses {
    param([Parameter(Mandatory)][object]$State)

    if ($null -ne (Get-OwnedProcess -Record $State.App -ExpectedProcessName "rtmp_monitor")) {
        return $true
    }
    foreach ($stream in @($State.Streams)) {
        if ($null -ne (Get-OwnedProcess -Record $stream -ExpectedProcessName "ffmpeg")) {
            return $true
        }
    }
    foreach ($record in @($State.NginxProcesses)) {
        if ($null -ne (Get-OwnedProcess -Record $record -ExpectedProcessName "nginx")) {
            return $true
        }
    }
    return $false
}

function Invoke-Check {
    Write-Step "检查 Week 4 四路人工验收环境"

    Assert-StreamUrls
    Assert-File -Path $FfmpegPath -Description "FFmpeg"
    Assert-File -Path $NginxExe -Description "nginx-rtmp"
    Assert-File -Path $NginxConfigPath -Description "nginx.conf"
    Assert-File -Path $InputFile -Description "测试视频"
    Assert-File -Path $AppPath -Description "Qt Debug 程序"
    Assert-File -Path $FontPath -Description "Arial 字体"

    $ffprobePath = Join-Path (Split-Path -Parent $FfmpegPath) "ffprobe.exe"
    Assert-File -Path $ffprobePath -Description "ffprobe"

    $encoders = (& $FfmpegPath -hide_banner -encoders 2>&1 | Out-String)
    if ($encoders -notmatch "(?m)\blibx264\b") {
        throw "FFmpeg 未检测到 libx264 编码器。"
    }
    $filters = (& $FfmpegPath -hide_banner -filters 2>&1 | Out-String)
    if ($filters -notmatch "(?m)\bdrawtext\b") {
        throw "FFmpeg 未检测到 drawtext 滤镜。"
    }
    $protocols = (& $FfmpegPath -hide_banner -protocols 2>&1 | Out-String)
    if ($protocols -notmatch "(?m)^\s*rtmp\s*$") {
        throw "FFmpeg 未检测到 RTMP 协议。"
    }
    $muxers = (& $FfmpegPath -hide_banner -muxers 2>&1 | Out-String)
    if ($muxers -notmatch "(?m)^\s*E\s+flv\s+") {
        throw "FFmpeg 未检测到 FLV 封装器。"
    }

    $videoInfo = & $ffprobePath `
        -v error `
        -select_streams v:0 `
        -show_entries "stream=codec_name,width,height,pix_fmt,r_frame_rate" `
        -of "default=noprint_wrappers=1" `
        $InputFile
    if ($LASTEXITCODE -ne 0) {
        throw "ffprobe 无法读取测试视频。"
    }

    Add-Type -AssemblyName System.Windows.Forms
    $screens = [System.Windows.Forms.Screen]::AllScreens
    $primary = $screens | Where-Object Primary | Select-Object -First 1

    Write-Host "[通过] FFmpeg：$FfmpegPath"
    Write-Host "[通过] nginx：$NginxExe"
    Write-Host "[通过] Qt 程序：$AppPath"
    Write-Host "[通过] libx264、drawtext、RTMP 和 FLV 可用"
    Write-Host "[信息] 测试视频："
    $videoInfo | ForEach-Object { Write-Host "       $_" }
    Write-Host "[信息] 主屏工作区：$($primary.WorkingArea)"
    foreach ($screen in $screens) {
        Write-Host "[信息] 显示器：$($screen.DeviceName) Primary=$($screen.Primary) Bounds=$($screen.Bounds)"
    }
    Write-Host "[信息] RTMP 1935 当前监听：$(Test-RtmpPort)"

    if (Test-Path -LiteralPath $StatePath -PathType Leaf) {
        try {
            $existingState = Get-State
            Write-Host "[信息] 已存在状态文件；活动进程：$(Test-StateHasActiveProcesses -State $existingState)"
        }
        catch {
            Write-Warning $_.Exception.Message
        }
    }

    Write-Host ""
    Write-Host "环境检查完成。下一步：" -ForegroundColor Green
    Write-Host "  powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Action Start"
}

function New-InitialState {
    $streamRecords = @()
    $colors = @("red", "green", "blue", "orange")
    for ($index = 0; $index -lt 4; ++$index) {
        $cameraNumber = $index + 1
        $streamRecords += [PSCustomObject]@{
            Index = $index
            CameraName = "Camera {0:D2}" -f $cameraNumber
            Label = "CAMERA {0:D3}" -f $cameraNumber
            Color = $colors[$index]
            Url = $StreamUrls[$index]
            Pid = 0
            ProcessName = "ffmpeg"
            ExecutablePath = [IO.Path]::GetFullPath($FfmpegPath)
            StartedAtUtc = $null
            StandardOutputLog = Join-Path $OutputRoot ("camera{0:D3}.stdout.log" -f $cameraNumber)
            StandardErrorLog = Join-Path $OutputRoot ("camera{0:D3}.ffmpeg.log" -f $cameraNumber)
        }
    }

    return [PSCustomObject]@{
        ScriptVersion = $ScriptVersion
        StartedAt = [DateTimeOffset]::Now.ToString("O")
        StoppedAt = $null
        ServerWasRunning = $false
        NginxProcesses = @()
        Paths = [PSCustomObject]@{
            FfmpegPath = [IO.Path]::GetFullPath($FfmpegPath)
            NginxRoot = [IO.Path]::GetFullPath($NginxRoot)
            NginxExe = [IO.Path]::GetFullPath($NginxExe)
            InputFile = [IO.Path]::GetFullPath($InputFile)
            AppPath = [IO.Path]::GetFullPath($AppPath)
            FontPath = [IO.Path]::GetFullPath($FontPath)
            OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
        }
        App = [PSCustomObject]@{
            Pid = 0
            ProcessName = "rtmp_monitor"
            ExecutablePath = [IO.Path]::GetFullPath($AppPath)
            StartedAtUtc = $null
        }
        Streams = $streamRecords
    }
}

function Start-RtmpServerForState {
    param([Parameter(Mandatory)][object]$State)

    if (Test-RtmpPort) {
        $State.ServerWasRunning = $true
        Write-Host "[信息] 1935 已在监听，复用已有 RTMP 服务且 Stop 时不会关闭它。"
        return
    }

    $existingNginx = @(Get-Process nginx -ErrorAction SilentlyContinue)
    if ($existingNginx.Count -gt 0) {
        throw "发现 nginx 进程但 1935 未监听。为避免误杀，请先处理残留 nginx。"
    }

    Write-Step "启动 nginx-rtmp"
    $launchTime = [DateTime]::UtcNow.AddSeconds(-1)
    $arguments = @(
        "-p", "$($State.Paths.NginxRoot)\",
        "-c", $NginxConfigRelativePath
    )
    [void](Start-Process `
        -FilePath $State.Paths.NginxExe `
        -ArgumentList (Join-NativeArguments -Arguments $arguments) `
        -WorkingDirectory $State.Paths.NginxRoot `
        -WindowStyle Hidden `
        -PassThru)
    Wait-RtmpPort -ExpectedListening $true

    $records = @()
    foreach ($process in @(Get-Process nginx -ErrorAction SilentlyContinue)) {
        try {
            if ($process.Path.Equals(
                    [string]$State.Paths.NginxExe,
                    [StringComparison]::OrdinalIgnoreCase
                ) -and
                $process.StartTime.ToUniversalTime() -ge $launchTime) {
                $records += New-ProcessRecord `
                    -Process $process `
                    -ExecutablePath $State.Paths.NginxExe
            }
        }
        catch {
            continue
        }
    }

    if ($records.Count -eq 0) {
        throw "nginx 已监听，但无法记录由本脚本启动的进程。"
    }
    $State.NginxProcesses = $records
    Write-Host "[通过] nginx-rtmp 正在监听 127.0.0.1:1935"
}

function Start-StreamRecord {
    param(
        [Parameter(Mandatory)][object]$State,
        [Parameter(Mandatory)][object]$Stream
    )

    $existing = Get-OwnedProcess -Record $Stream -ExpectedProcessName "ffmpeg"
    if ($null -ne $existing) {
        Write-Host "[信息] $($Stream.CameraName) 已在推流：PID $($existing.Id)"
        return
    }
    if (-not (Test-RtmpPort)) {
        throw "RTMP 端口未监听，不能启动 $($Stream.CameraName)。"
    }

    Clear-ProcessRecord -Record $Stream
    $fontFilterPath = ([string]$State.Paths.FontPath).Replace("\", "/").Replace(":", "\:")
    $videoFilter =
        "scale=1280:-2," +
        "drawbox=x=20:y=20:w=420:h=80:color=$($Stream.Color)@0.78:t=fill," +
        "drawtext=fontfile='$fontFilterPath':text='$($Stream.Label)':" +
        "fontcolor=white:fontsize=42:x=40:y=38"
    $arguments = @(
        "-hide_banner",
        "-loglevel", "warning",
        "-re",
        "-stream_loop", "-1",
        "-i", [string]$State.Paths.InputFile,
        "-an",
        "-vf", $videoFilter,
        "-r", "30",
        "-c:v", "libx264",
        "-preset", "ultrafast",
        "-tune", "zerolatency",
        "-profile:v", "baseline",
        "-pix_fmt", "yuv420p",
        "-bf", "0",
        "-g", "30",
        "-keyint_min", "30",
        "-sc_threshold", "0",
        "-b:v", "2500k",
        "-maxrate", "2500k",
        "-bufsize", "500k",
        "-f", "flv",
        [string]$Stream.Url
    )

    $process = Start-Process `
        -FilePath $State.Paths.FfmpegPath `
        -ArgumentList (Join-NativeArguments -Arguments $arguments) `
        -RedirectStandardOutput $Stream.StandardOutputLog `
        -RedirectStandardError $Stream.StandardErrorLog `
        -WindowStyle Hidden `
        -PassThru
    Start-Sleep -Milliseconds 600
    $process.Refresh()
    if ($process.HasExited) {
        throw "$($Stream.CameraName) 推流进程提前退出；请检查 $($Stream.StandardErrorLog)"
    }

    $record = New-ProcessRecord `
        -Process $process `
        -ExecutablePath $State.Paths.FfmpegPath
    $Stream.Pid = $record.Pid
    $Stream.ProcessName = $record.ProcessName
    $Stream.ExecutablePath = $record.ExecutablePath
    $Stream.StartedAtUtc = $record.StartedAtUtc
    Write-Host "[通过] $($Stream.CameraName) -> $($Stream.Url) PID $($process.Id)"
}

function Start-AllStreamsForState {
    param([Parameter(Mandatory)][object]$State)

    Write-Step "启动四路带标签 H.264/RTMP 测试流"
    foreach ($stream in @($State.Streams)) {
        Start-StreamRecord -State $State -Stream $stream
        Save-State -State $State
    }
}

function Move-AppToPrimaryScreen {
    param([Parameter(Mandatory)][System.Diagnostics.Process]$Process)

    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    while ($Process.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
        $Process.Refresh()
    }
    if ($Process.MainWindowHandle -eq 0) {
        Write-Warning "8 秒内没有取得 Qt 主窗口句柄，无法自动移动到主屏。"
        return
    }

    Add-Type -AssemblyName System.Windows.Forms
    $primary = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    if (-not ("Week4MultiStreamNativeMethods" -as [type])) {
        Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Week4MultiStreamNativeMethods {
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool MoveWindow(
        IntPtr hWnd, int x, int y, int width, int height, bool repaint);
}
"@
    }

    [void][Week4MultiStreamNativeMethods]::MoveWindow(
        $Process.MainWindowHandle,
        $primary.X,
        $primary.Y,
        $primary.Width,
        $primary.Height,
        $true
    )
}

function Start-AppForState {
    param([Parameter(Mandatory)][object]$State)

    $existing = Get-OwnedProcess -Record $State.App -ExpectedProcessName "rtmp_monitor"
    if ($null -ne $existing) {
        Write-Host "[信息] Qt 程序已运行：PID $($existing.Id)"
        return
    }

    Clear-ProcessRecord -Record $State.App
    $arguments = @()
    foreach ($stream in @($State.Streams)) {
        $arguments += "--url"
        $arguments += [string]$stream.Url
    }

    Write-Step "启动四路 Qt 程序"
    $process = Start-Process `
        -FilePath $State.Paths.AppPath `
        -ArgumentList (Join-NativeArguments -Arguments $arguments) `
        -WorkingDirectory (Split-Path -Parent $State.Paths.AppPath) `
        -PassThru
    Start-Sleep -Milliseconds 400
    $process.Refresh()
    if ($process.HasExited) {
        throw "Qt 程序启动后立即退出，退出码：$($process.ExitCode)"
    }

    $record = New-ProcessRecord `
        -Process $process `
        -ExecutablePath $State.Paths.AppPath
    $State.App.Pid = $record.Pid
    $State.App.ProcessName = $record.ProcessName
    $State.App.ExecutablePath = $record.ExecutablePath
    $State.App.StartedAtUtc = $record.StartedAtUtc
    Save-State -State $State
    Move-AppToPrimaryScreen -Process $process
    Write-Host "[通过] Qt 程序已启动到主屏：PID $($process.Id)"
}

function Invoke-Start {
    Invoke-Check
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

    if (Test-Path -LiteralPath $StatePath -PathType Leaf) {
        $oldState = Get-State
        if (Test-StateHasActiveProcesses -State $oldState) {
            throw "已有 Week 4 验收进程正在运行。请先执行 -Action Stop。"
        }
    }

    $state = New-InitialState
    try {
        Start-RtmpServerForState -State $state
        Save-State -State $state
        Start-AllStreamsForState -State $state
        Start-AppForState -State $state
        Save-State -State $state

        Write-Host ""
        Write-Host "四路人工验收环境已经启动。" -ForegroundColor Green
        Write-Host "等待最多 10 秒，确认 Camera 01～04 分别显示 CAMERA 001～004。"
        Write-Host "下一步状态检查："
        Write-Host "  powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Action Status"
    }
    catch {
        Save-State -State $state
        throw
    }
}

function Write-ProcessStatus {
    param(
        [Parameter(Mandatory)][string]$Label,
        [AllowNull()][System.Diagnostics.Process]$Process
    )

    if ($null -eq $Process) {
        Write-Host ("{0,-16} {1}" -f $Label, "[停止]") -ForegroundColor Yellow
        return
    }

    $Process.Refresh()
    $runningFor = [DateTime]::Now - $Process.StartTime
    Write-Host (
        "{0,-16} [运行] PID={1,-6} Uptime={2:hh\:mm\:ss} CPU={3,8:N1}s Memory={4,7:N1}MB" -f
        $Label,
        $Process.Id,
        $runningFor,
        $Process.CPU,
        ($Process.WorkingSet64 / 1MB)
    )
}

function Invoke-Status {
    $state = Get-State
    Write-Step "Week 4 四路验收状态"
    Write-Host "状态文件：$StatePath"
    Write-Host "RTMP 1935：$(if (Test-RtmpPort) { '[监听]' } else { '[未监听]' })"
    Write-Host ""

    $appProcess = Get-OwnedProcess -Record $state.App -ExpectedProcessName "rtmp_monitor"
    Write-ProcessStatus -Label "rtmp_monitor" -Process $appProcess
    foreach ($stream in @($state.Streams)) {
        $process = Get-OwnedProcess -Record $stream -ExpectedProcessName "ffmpeg"
        Write-ProcessStatus -Label $stream.CameraName -Process $process
    }
    foreach ($record in @($state.NginxProcesses)) {
        $process = Get-OwnedProcess -Record $record -ExpectedProcessName "nginx"
        Write-ProcessStatus -Label "nginx" -Process $process
    }

    Write-Host ""
    if ($null -eq $appProcess) {
        Write-Host "[信息] 未检测到本脚本启动的 rtmp_monitor；手动关闭验收通过时应为此状态。"
    }
}

function Stop-StreamRecord {
    param([Parameter(Mandatory)][object]$Stream)

    $process = Get-OwnedProcess -Record $Stream -ExpectedProcessName "ffmpeg"
    if ($null -eq $process) {
        Clear-ProcessRecord -Record $Stream
        Write-Host "[信息] $($Stream.CameraName) 已停止。"
        return
    }

    Write-Host "停止 $($Stream.CameraName)：PID $($process.Id)"
    Stop-OwnedProcess -Record $Stream -ExpectedProcessName "ffmpeg"
}

function Invoke-StopCamera03 {
    $state = Get-State
    $camera03 = @($state.Streams) | Where-Object { [int]$_.Index -eq 2 } |
        Select-Object -First 1
    if ($null -eq $camera03) {
        throw "状态中缺少 Camera 03。"
    }

    Write-Step "注入 Camera 03 单路断流"
    Stop-StreamRecord -Stream $camera03
    Save-State -State $state
    Write-Host "请观察：只有 Camera 03 清黑并进入重连，其他三路应持续播放。" -ForegroundColor Green
}

function Invoke-StartCamera03 {
    $state = Get-State
    $camera03 = @($state.Streams) | Where-Object { [int]$_.Index -eq 2 } |
        Select-Object -First 1
    if ($null -eq $camera03) {
        throw "状态中缺少 Camera 03。"
    }

    Write-Step "恢复 Camera 03 推流"
    Start-StreamRecord -State $state -Stream $camera03
    Save-State -State $state
    Write-Host "请在最多 8 秒内确认 Camera 03 恢复 CAMERA 003 画面。" -ForegroundColor Green
}

function Invoke-StopStreams {
    $state = Get-State
    Write-Step "停止全部四路推流"
    foreach ($stream in @($state.Streams)) {
        Stop-StreamRecord -Stream $stream
    }
    Save-State -State $state
    Write-Host "请观察四格全部清黑并进入各自的重连状态。" -ForegroundColor Green
}

function Invoke-StartStreams {
    $state = Get-State
    Start-AllStreamsForState -State $state
    Save-State -State $state
    Write-Host "四路推流已恢复；请在最多 8 秒内确认全部画面恢复。" -ForegroundColor Green
}

function Invoke-StartApp {
    $state = Get-State
    Start-AppForState -State $state
    Save-State -State $state
    Write-Host "Qt 程序已重新启动；可继续关闭场景验收。" -ForegroundColor Green
}

function Stop-RtmpServerForState {
    param([Parameter(Mandatory)][object]$State)

    if ([bool]$State.ServerWasRunning) {
        Write-Host "[信息] nginx 在测试前已经运行，本次不停止。"
        return
    }

    $ownedNginx = @()
    foreach ($record in @($State.NginxProcesses)) {
        $process = Get-OwnedProcess -Record $record -ExpectedProcessName "nginx"
        if ($null -ne $process) {
            $ownedNginx += $process
        }
    }
    if ($ownedNginx.Count -eq 0) {
        return
    }

    Write-Step "停止本脚本启动的 nginx-rtmp"
    $arguments = @(
        "-p", "$($State.Paths.NginxRoot)\",
        "-c", $NginxConfigRelativePath,
        "-s", "quit"
    )
    try {
        $stopProcess = Start-Process `
            -FilePath $State.Paths.NginxExe `
            -ArgumentList (Join-NativeArguments -Arguments $arguments) `
            -WorkingDirectory $State.Paths.NginxRoot `
            -WindowStyle Hidden `
            -PassThru
        [void]$stopProcess.WaitForExit(3000)
    }
    catch {
        Write-Warning "nginx 优雅退出命令失败：$($_.Exception.Message)"
    }

    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while ([DateTime]::UtcNow -lt $deadline) {
        $remainingCount = 0
        foreach ($record in @($State.NginxProcesses)) {
            if ($null -ne (Get-OwnedProcess -Record $record -ExpectedProcessName "nginx")) {
                ++$remainingCount
            }
        }
        if ($remainingCount -eq 0) {
            break
        }
        Start-Sleep -Milliseconds 200
    }

    foreach ($record in @($State.NginxProcesses)) {
        Stop-OwnedProcess -Record $record -ExpectedProcessName "nginx"
    }
    Write-Host "[通过] 本脚本启动的 nginx 进程已停止"
}

function Invoke-Stop {
    if (-not (Test-Path -LiteralPath $StatePath -PathType Leaf)) {
        Write-Host "没有 Week 4 测试状态；无需清理。"
        return
    }

    $state = Get-State
    Write-Step "清理 Week 4 四路人工验收环境"

    Stop-OwnedProcess `
        -Record $state.App `
        -ExpectedProcessName "rtmp_monitor" `
        -TryCloseMainWindow `
        -GracefulTimeoutMs 5000
    foreach ($stream in @($state.Streams)) {
        Stop-StreamRecord -Stream $stream
    }
    Stop-RtmpServerForState -State $state

    $state.StoppedAt = [DateTimeOffset]::Now.ToString("O")
    Save-State -State $state

    if (Test-StateHasActiveProcesses -State $state) {
        throw "清理后仍检测到状态文件所属进程。"
    }

    Write-Host ""
    Write-Host "[通过] 本脚本记录的 Qt、FFmpeg 和 nginx 测试进程均已清理。" -ForegroundColor Green
    Write-Host "日志和状态保留在：$OutputRoot"
}

Write-Host "Week 4 四路人工验收脚本：$ScriptVersion" -ForegroundColor DarkGray

try {
    switch ($Action) {
        "Check" { Invoke-Check }
        "Start" { Invoke-Start }
        "Status" { Invoke-Status }
        "StopCamera03" { Invoke-StopCamera03 }
        "StartCamera03" { Invoke-StartCamera03 }
        "StopStreams" { Invoke-StopStreams }
        "StartStreams" { Invoke-StartStreams }
        "StartApp" { Invoke-StartApp }
        "Stop" { Invoke-Stop }
    }
}
catch {
    Write-Host ""
    Write-Host "[失败] $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "状态和日志目录：$OutputRoot"
    Write-Host "清理命令："
    Write-Host "  powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Action Stop"
    exit 1
}
