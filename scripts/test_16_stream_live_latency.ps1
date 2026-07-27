<#
.SYNOPSIS
    Windows 双屏 16 路实况源到显示延迟验收。

.DESCRIPTION
    SourceClock 在副屏绘制 UTC 毫秒低 32 位和 CRC-8 标记；一个 FFmpeg
    gdigrab 编码器通过 tee 发布到 camera001～camera016。应用只在
    --latency-marker 测试模式下解析标记。所有进程信息和结果写入 out，
    Stop 会在核对 PID、路径和启动时间后清理。
#>
[CmdletBinding()]
param(
    [ValidateSet("Check", "Start", "Status", "Capture", "RunAutomated", "Stop", "Clock")]
    [string]$Action = "Check",
    [ValidateSet("Source", "Reference")]
    [string]$ClockRole = "Source",
    [ValidateRange(30, 7200)]
    [int]$DurationSeconds = 600,
    [ValidateRange(1, 120)]
    [int]$WarmupSeconds = 20,
    [ValidateRange(10, 300)]
    [int]$CaptureIntervalSeconds = 60,
    [string]$FfmpegPath = "E:\DevTools\ffmpeg-8.1.2-full_build\bin\ffmpeg.exe",
    [string]$NginxRoot = "E:\DevTools\nginx-rtmp",
    [string]$AppPath = "",
    [string]$OutputRoot = "",
    [string[]]$StreamUrls = @(),
    [switch]$InternalLauncher,
    [string]$InternalStreamUrlsBase64 = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# 只清理本脚本继承到的 Path/PATH 重复项；不修改用户或系统环境。
$InheritedExecutablePath = cmd.exe /d /c echo %PATH%
[Environment]::SetEnvironmentVariable("PATH", $null, "Process")
[Environment]::SetEnvironmentVariable("Path", $null, "Process")
[Environment]::SetEnvironmentVariable(
    "Path", [string]$InheritedExecutablePath, "Process"
)

# Windows PowerShell 5.1 的 Start-Process 重定向回调会占用 ThreadPool。
# 双屏场景会连续创建 nginx、两个时钟、FFmpeg 和播放器；提前扩容可避免
# 第 5 个长驻进程提交时发生线程饥饿。该设置只影响当前脚本宿主。
if (($Action -eq "Start" -and $InternalLauncher) -or
    $Action -eq "RunAutomated") {
    $minimumWorkerThreads = 0
    $minimumCompletionPortThreads = 0
    [void][Threading.ThreadPool]::GetMinThreads(
        [ref]$minimumWorkerThreads,
        [ref]$minimumCompletionPortThreads
    )
    [void][Threading.ThreadPool]::SetMinThreads(
        [Math]::Max(64, $minimumWorkerThreads),
        [Math]::Max(64, $minimumCompletionPortThreads)
    )
}

if ($Action -eq "Start" -and $InternalLauncher -and
    $null -eq ("Week4LiveDeadline" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Threading;
public sealed class Week4LiveDeadline : IDisposable
{
    private Timer timer;
    public Week4LiveDeadline(int milliseconds)
    {
        timer = new Timer(
            _ => Environment.Exit(124), null, milliseconds, Timeout.Infinite
        );
    }
    public void Dispose()
    {
        Timer current = Interlocked.Exchange(ref timer, null);
        if (current != null) current.Dispose();
    }
}
"@
}

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if ($InternalLauncher -and
    -not [string]::IsNullOrWhiteSpace($InternalStreamUrlsBase64)) {
    $streamUrlsJson = [Text.Encoding]::UTF8.GetString(
        [Convert]::FromBase64String($InternalStreamUrlsBase64)
    )
    $decodedStreamUrls = $streamUrlsJson | ConvertFrom-Json
    $StreamUrls = [string[]]$decodedStreamUrls
}
if ([string]::IsNullOrWhiteSpace($AppPath)) {
    $AppPath = Join-Path $ProjectRoot "out\build-windows-x64\debug\rtmp_monitor.exe"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $ProjectRoot "out\16-stream-live-latency"
}
if ($StreamUrls.Count -eq 0) {
    $StreamUrls = @(1..16 | ForEach-Object {
        "rtmp://127.0.0.1:1935/live/camera{0:D3}" -f $_
    })
}

$LogsRoot = Join-Path $ProjectRoot "out\logs\16-stream-live-latency"
$RuntimeRoot = Join-Path $ProjectRoot "out\runtime\16-stream-live-latency"
$NginxRuntimeRoot = Join-Path $RuntimeRoot "nginx"
$NginxSourceExe = Join-Path $NginxRoot "sbin\nginx.exe"
$NginxSourceConfig = Join-Path $NginxRoot "conf\nginx.conf"
$NginxExe = Join-Path $NginxRuntimeRoot "sbin\nginx.exe"
$NginxConfig = Join-Path $NginxRuntimeRoot "conf\nginx.conf"
$StatePath = Join-Path $RuntimeRoot "pids.json"
$LauncherPath = Join-Path $RuntimeRoot "launcher.cmd"
$LauncherPidPath = Join-Path $RuntimeRoot "launcher-pid.json"
$LauncherResultPath = Join-Path $RuntimeRoot "launcher-result.json"
$MetricsPath = Join-Path $OutputRoot "metrics.json"
$ReportJsonPath = Join-Path $OutputRoot "latency-report.json"
$ReportMarkdownPath = Join-Path $OutputRoot "latency-report.md"

function Write-Stage {
    param([string]$Text)
    Write-Host ""
    Write-Host ("=" * 68) -ForegroundColor DarkGray
    Write-Host $Text -ForegroundColor Cyan
    Write-Host ("=" * 68) -ForegroundColor DarkGray
}

function Get-Displays {
    Add-Type -AssemblyName System.Windows.Forms
    return @( [Windows.Forms.Screen]::AllScreens )
}

function Get-SourceDisplay {
    $screens = Get-Displays
    $secondary = @($screens | Where-Object { -not $_.Primary } | Select-Object -First 1)
    if ($secondary.Count -eq 0) {
        throw "未检测到副屏。请连接并启用扩展桌面后重试。"
    }
    return $secondary[0]
}

function Get-MarkerCrc {
    param([uint32]$Value)
    [int]$crc = 0
    for ($byteIndex = 3; $byteIndex -ge 0; --$byteIndex) {
        $crc = $crc -bxor (($Value -shr ($byteIndex * 8)) -band 0xff)
        for ($bit = 0; $bit -lt 8; ++$bit) {
            if (($crc -band 0x80) -ne 0) {
                $crc = (($crc -shl 1) -bxor 0x07) -band 0xff
            } else {
                $crc = ($crc -shl 1) -band 0xff
            }
        }
    }
    return [byte]$crc
}

function Show-TestClock {
    param([string]$Role)
    Add-Type -AssemblyName PresentationFramework, PresentationCore, WindowsBase
    if ($null -eq ("Week4ClockDpi" -as [type])) {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Week4ClockDpi
{
    [DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr windowHandle);
}
"@
    }
    $screens = Get-Displays
    if ($Role -eq "Source") {
        $screen = Get-SourceDisplay
    } else {
        $screen = @($screens | Where-Object Primary | Select-Object -First 1)[0]
    }

    $window = New-Object Windows.Window
    $window.WindowStyle = [Windows.WindowStyle]::None
    $window.ResizeMode = [Windows.ResizeMode]::NoResize
    $window.Topmost = $true
    $window.ShowInTaskbar = $false
    $window.Background = [Windows.Media.Brushes]::Black
    $window.Left = $screen.Bounds.X
    $window.Top = $screen.Bounds.Y

    if ($Role -eq "Source") {
        $window.Width = $screen.Bounds.Width
        $window.Height = $screen.Bounds.Height
    } else {
        $window.Width = 800
        $window.Height = 130
        $window.Left = $screen.WorkingArea.X +
            [Math]::Floor(($screen.WorkingArea.Width - $window.Width) / 2)
        $window.Top = $screen.WorkingArea.Y
    }

    $coordinateScale = 1.0
    if ($Role -eq "Source") {
        $interopHelper = New-Object Windows.Interop.WindowInteropHelper($window)
        $windowHandle = $interopHelper.EnsureHandle()
        $windowDpi = [Week4ClockDpi]::GetDpiForWindow($windowHandle)
        if ($windowDpi -gt 0) {
            $coordinateScale = $windowDpi / 96.0
        }
        # WPF uses device-independent pixels. Keep the captured marker geometry
        # fixed in physical pixels so FFmpegPlayer can sample the same positions
        # on 100%, 125%, 150%, or mixed-DPI displays.
        $window.Width = $screen.Bounds.Width / $coordinateScale
        $window.Height = $screen.Bounds.Height / $coordinateScale
    }

    $canvas = New-Object Windows.Controls.Canvas
    $window.Content = $canvas
    $caption = New-Object Windows.Controls.TextBlock
    $caption.Foreground = [Windows.Media.Brushes]::Gold
    $caption.FontFamily = "Consolas"
    $caption.FontSize = 18 / $coordinateScale
    $caption.Text = if ($Role -eq "Source") {
        "MACHINE LATENCY MARKER (40 BITS - NOT TEXT)"
    } else {
        "MAIN-SCREEN REFERENCE CLOCK"
    }
    [Windows.Controls.Canvas]::SetLeft($caption, (20 / $coordinateScale))
    [Windows.Controls.Canvas]::SetTop($caption, $(if ($Role -eq "Source") {
        54 / $coordinateScale
    } else {
        12
    }))
    [void]$canvas.Children.Add($caption)

    $label = New-Object Windows.Controls.TextBlock
    $label.Foreground = [Windows.Media.Brushes]::Lime
    $label.FontFamily = "Consolas"
    $label.FontSize = 34 / $coordinateScale
    [Windows.Controls.Canvas]::SetLeft($label, (20 / $coordinateScale))
    [Windows.Controls.Canvas]::SetTop($label, $(if ($Role -eq "Source") {
        82 / $coordinateScale
    } else {
        48
    }))
    [void]$canvas.Children.Add($label)

    $cells = @()
    if ($Role -eq "Source") {
        for ($index = 0; $index -lt 40; ++$index) {
            $cell = New-Object Windows.Shapes.Rectangle
            $cell.Width = 26 / $coordinateScale
            $cell.Height = 40 / $coordinateScale
            $cell.Stroke = [Windows.Media.Brushes]::Gray
            $cell.StrokeThickness = 1 / $coordinateScale
            [Windows.Controls.Canvas]::SetLeft(
                $cell,
                ((20 + 28 * $index) / $coordinateScale)
            )
            [Windows.Controls.Canvas]::SetTop($cell, (10 / $coordinateScale))
            [void]$canvas.Children.Add($cell)
            $cells += $cell
        }
    }

    $timer = New-Object Windows.Threading.DispatcherTimer
    # Keep one complete marker value visible for several 30 fps capture
    # frames. A 20 ms update could let gdigrab observe mixed old/new bit cells,
    # causing the CRC check to reject every frame.
    $timer.Interval = [TimeSpan]::FromMilliseconds(100)
    $timer.Add_Tick({
        $now = [DateTimeOffset]::UtcNow
        $label.Text = "{0} LIVE UTC  {1:HH:mm:ss.fff}" -f $Role.ToUpperInvariant(), $now
        if ($Role -eq "Source") {
            [uint32]$stamp = [uint32]($now.ToUnixTimeMilliseconds() -band 0xffffffffL)
            [uint64]$bits = (([uint64]$stamp) -shl 8) -bor (Get-MarkerCrc $stamp)
            for ($index = 0; $index -lt 40; ++$index) {
                $isSet = (($bits -shr (39 - $index)) -band 1) -ne 0
                $cells[$index].Fill = if ($isSet) {
                    [Windows.Media.Brushes]::White
                } else {
                    [Windows.Media.Brushes]::Black
                }
            }
        }
    })
    $window.Add_Closed({ $timer.Stop() })
    $timer.Start()
    [void]$window.ShowDialog()
}

if ($Action -eq "Clock") {
    Show-TestClock -Role $ClockRole
    exit 0
}

function Assert-File {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description 不存在：$Path"
    }
}

function Invoke-BoundedProbe {
    param(
        [string]$Name,
        [string[]]$Arguments
    )
    New-Item -ItemType Directory -Path $LogsRoot -Force | Out-Null
    $stdoutPath = Join-Path $LogsRoot ("check-{0}.stdout.log" -f $Name)
    $stderrPath = Join-Path $LogsRoot ("check-{0}.stderr.log" -f $Name)
    $process = Start-Process -FilePath $FfmpegPath `
        -ArgumentList $Arguments -WindowStyle Hidden `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath -PassThru
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $nextProgress = 0
    while (-not $process.HasExited -and $watch.Elapsed.TotalSeconds -lt 10) {
        if ($watch.Elapsed.TotalSeconds -ge $nextProgress) {
            Write-Host "[Check $Name] $([int]$watch.Elapsed.TotalSeconds)/10 秒"
            $nextProgress += 5
        }
        Start-Sleep -Milliseconds 200
        $process.Refresh()
    }
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "FFmpeg $Name 检查超过 10 秒。"
    }
    [void]$process.WaitForExit(1000)
    $process.Refresh()
    if ($null -ne $process.ExitCode -and $process.ExitCode -ne 0) {
        Get-Content -LiteralPath $stderrPath -Tail 100 `
            -ErrorAction SilentlyContinue
        throw "FFmpeg $Name 检查失败，退出码 $($process.ExitCode)。"
    }
    return Get-Content -LiteralPath $stdoutPath -Raw
}

function Initialize-NginxRuntime {
    foreach ($directory in @("sbin", "conf", "logs", "temp")) {
        New-Item -ItemType Directory -Path (
            Join-Path $NginxRuntimeRoot $directory
        ) -Force | Out-Null
    }
    Copy-Item -LiteralPath $NginxSourceExe -Destination $NginxExe -Force
    @"
worker_processes 1;
error_log logs/error.log warn;
pid logs/nginx.pid;
events { worker_connections 1024; }
rtmp {
    server {
        listen 1935;
        chunk_size 4096;
        timeout 30s;
        ping 30s;
        ping_timeout 10s;
        application live { live on; record off; }
    }
}
"@ | Set-Content -LiteralPath $NginxConfig -Encoding Ascii
}

function Test-RtmpPort {
    $client = New-Object Net.Sockets.TcpClient
    try {
        $task = $client.ConnectAsync("127.0.0.1", 1935)
        return $task.Wait(500) -and $client.Connected
    } catch {
        return $false
    } finally {
        $client.Dispose()
    }
}

function Wait-RtmpPort {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.ElapsedMilliseconds -lt 2500) {
        if (Test-RtmpPort) { return $true }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

function New-ProcessRecord {
    param([Diagnostics.Process]$Process, [string]$Path, [string]$Role)
    return [ordered]@{
        Role = $Role
        Pid = $Process.Id
        ProcessName = $Process.ProcessName
        ExecutablePath = [IO.Path]::GetFullPath($Path)
        StartedAtUtc = $Process.StartTime.ToUniversalTime().ToString("O")
    }
}

function Get-OwnedProcess {
    param([object]$Record)
    if ($null -eq $Record -or $null -eq $Record.Pid) { return $null }
    $process = Get-Process -Id ([int]$Record.Pid) -ErrorAction SilentlyContinue
    if ($null -eq $process) { return $null }
    try {
        $actualPath = [IO.Path]::GetFullPath($process.Path)
        $expectedPath = [IO.Path]::GetFullPath([string]$Record.ExecutablePath)
        $actualStart = $process.StartTime.ToUniversalTime()
        $expectedStart = [DateTime]::Parse([string]$Record.StartedAtUtc).ToUniversalTime()
        if ($process.ProcessName -ine [string]$Record.ProcessName -or
            $actualPath -ine $expectedPath -or
            [Math]::Abs(($actualStart - $expectedStart).TotalSeconds) -gt 2) {
            return $null
        }
        return $process
    } catch {
        return $null
    }
}

function Stop-OwnedProcess {
    param([object]$Record, [int]$GraceSeconds = 2)
    $process = Get-OwnedProcess $Record
    if ($null -eq $process) { return }
    try {
        if ($Record.Role -eq "application") {
            [void]$process.CloseMainWindow()
            if ($process.WaitForExit($GraceSeconds * 1000)) { return }
        }
        Stop-Process -Id $process.Id -Force
        [void]$process.WaitForExit(3000)
    } catch {
        Write-Warning "停止 $($Record.Role) PID $($Record.Pid) 失败：$($_.Exception.Message)"
    }
}

function Save-State {
    param([object]$State)
    New-Item -ItemType Directory -Path $RuntimeRoot -Force | Out-Null
    $temporaryPath = "$StatePath.tmp"
    $State | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $temporaryPath -Encoding UTF8
    Move-Item -LiteralPath $temporaryPath -Destination $StatePath -Force
}

function Write-LauncherResult {
    param(
        [ValidateSet("starting", "succeeded", "failed")]
        [string]$Status,
        [string]$Message
    )
    New-Item -ItemType Directory -Path $RuntimeRoot -Force | Out-Null
    $result = [ordered]@{
        Status = $Status
        Message = $Message
        UpdatedAtUtc = [DateTime]::UtcNow.ToString("O")
    }
    $temporaryPath = "$LauncherResultPath.tmp"
    $result | ConvertTo-Json |
        Set-Content -LiteralPath $temporaryPath -Encoding UTF8
    Move-Item -LiteralPath $temporaryPath -Destination $LauncherResultPath -Force
}

function Get-State {
    if (-not (Test-Path -LiteralPath $StatePath)) {
        throw "找不到状态文件：$StatePath。请先运行 Start。"
    }
    return Get-Content -LiteralPath $StatePath -Raw -Encoding UTF8 |
        ConvertFrom-Json
}

function Invoke-Check {
    Write-Stage "双屏实况延迟：环境检查"
    if ($StreamUrls.Count -ne 16) { throw "StreamUrls 必须恰好为 16 个。" }
    Assert-File $FfmpegPath "FFmpeg"
    Assert-File $NginxSourceExe "nginx"
    Assert-File $NginxSourceConfig "nginx 配置"
    Assert-File $AppPath "RtmpMonitor Debug 程序"
    $formats = Invoke-BoundedProbe "formats" @("-hide_banner", "-formats")
    $devices = Invoke-BoundedProbe "devices" @("-hide_banner", "-devices")
    $encoders = Invoke-BoundedProbe "encoders" @("-hide_banner", "-encoders")
    if ($devices -notmatch "gdigrab") { throw "FFmpeg 缺少 gdigrab。" }
    if ($formats -notmatch "\btee\b") { throw "FFmpeg 缺少 tee muxer。" }
    if ($encoders -notmatch "\blibx264\b") { throw "FFmpeg 缺少 libx264。" }
    $screens = Get-Displays
    if ($screens.Count -lt 2) { throw "需要 Windows 扩展桌面的两块显示器。" }
    $source = Get-SourceDisplay
    Write-Host "[通过] 检测到 $($screens.Count) 块屏幕；副屏 $($source.Bounds.Width)x$($source.Bounds.Height) @ $($source.Bounds.X),$($source.Bounds.Y)"
    Write-Host "下一步：-Action Start，或直接 -Action RunAutomated"
}

function Start-NginxIfNeeded {
    param([object]$State)
    if (Test-RtmpPort) {
        $State.NginxReused = $true
        return
    }
    Initialize-NginxRuntime
    New-Item -ItemType Directory -Path $LogsRoot -Force | Out-Null
    $process = Start-Process -FilePath $NginxExe `
        -WorkingDirectory $NginxRuntimeRoot -ArgumentList @(
            "-p", $NginxRuntimeRoot, "-c", "conf/nginx.conf"
        ) -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $LogsRoot "nginx.stdout.log") `
        -RedirectStandardError (Join-Path $LogsRoot "nginx.stderr.log")
    $State.Nginx = New-ProcessRecord $process $NginxExe "nginx"
    Save-State $State
    if (-not (Wait-RtmpPort)) { throw "nginx 未在 2.5 秒内监听 1935。" }
}

function Start-ClockProcess {
    param([string]$Role)
    $powerShellPath = Join-Path $PSHOME "powershell.exe"
    $arguments = @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $PSCommandPath,
        "-Action", "Clock", "-ClockRole", $Role
    )
    $process = Start-Process -FilePath $powerShellPath -ArgumentList $arguments `
        -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $LogsRoot (
            "clock-{0}.stdout.log" -f $Role.ToLowerInvariant()
        )) -RedirectStandardError (Join-Path $LogsRoot (
            "clock-{0}.stderr.log" -f $Role.ToLowerInvariant()
        ))
    return New-ProcessRecord $process $powerShellPath ("clock-" + $Role.ToLowerInvariant())
}

function Start-CapturePublisher {
    $source = Get-SourceDisplay
    $tee = (($StreamUrls | ForEach-Object {
        "[f=flv:onfail=ignore:use_fifo=1]$_"
    }) -join "|")
    $stdoutPath = Join-Path $LogsRoot "ffmpeg-live.stdout.log"
    $stderrPath = Join-Path $LogsRoot "ffmpeg-live.stderr.log"
    $arguments = @(
        "-hide_banner", "-loglevel", "warning",
        "-f", "gdigrab", "-framerate", "30",
        "-offset_x", [string]$source.Bounds.X,
        "-offset_y", [string]$source.Bounds.Y,
        "-video_size", ("{0}x{1}" -f $source.Bounds.Width, $source.Bounds.Height),
        "-i", "desktop",
        "-map", "0:v:0",
        "-vf", "scale=1280:720:flags=fast_bilinear,format=yuv420p",
        "-c:v", "libx264", "-preset", "ultrafast", "-tune", "zerolatency",
        "-threads", "4", "-g", "30", "-keyint_min", "30",
        "-sc_threshold", "0", "-bf", "0", "-an",
        "-f", "tee", $tee
    )
    $process = Start-Process -FilePath $FfmpegPath -ArgumentList $arguments `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath -PassThru -WindowStyle Hidden
    return New-ProcessRecord $process $FfmpegPath "ffmpeg-live"
}

function Start-Application {
    $arguments = @("--decode-threads", "8", "--metrics-file", $MetricsPath, "--latency-marker")
    foreach ($url in $StreamUrls) { $arguments += @("--url", $url) }
    $process = Start-Process -FilePath $AppPath -ArgumentList $arguments `
        -RedirectStandardOutput (Join-Path $LogsRoot "application.stdout.log") `
        -RedirectStandardError (Join-Path $LogsRoot "application.stderr.log") `
        -PassThru -WindowStyle Maximized
    return New-ProcessRecord $process $AppPath "application"
}

function Invoke-Start {
    Write-Output "[Start 1/5] 快速检查双屏、路径和 16 个 URL"
    if ($StreamUrls.Count -ne 16) { throw "StreamUrls 必须恰好为 16 个。" }
    Assert-File $FfmpegPath "FFmpeg"
    Assert-File $NginxSourceExe "nginx"
    Assert-File $AppPath "RtmpMonitor Debug 程序"
    [void](Get-SourceDisplay)
    New-Item -ItemType Directory -Path $OutputRoot,$LogsRoot,$RuntimeRoot `
        -Force | Out-Null
    $state = [ordered]@{
        SchemaVersion = 1
        StartedAtUtc = [DateTime]::UtcNow.ToString("O")
        NginxReused = $false
        Nginx = $null
        SourceClock = $null
        ReferenceClock = $null
        Publisher = $null
        Application = $null
    }
    try {
        Save-State $state
        Write-Output "[Start 2/5] 后台启动 nginx"
        Start-NginxIfNeeded $state
        Write-Output "[Start 3/5] 后台启动源/参考时钟"
        $state.SourceClock = Start-ClockProcess "Source"
        Save-State $state
        $state.ReferenceClock = Start-ClockProcess "Reference"
        Save-State $state
        Start-Sleep -Milliseconds 300
        Write-Output "[Start 4/5] 后台启动单编码器 tee 和应用"
        $state.Publisher = Start-CapturePublisher
        Save-State $state
        Start-Sleep -Milliseconds 500
        if ($null -eq (Get-OwnedProcess $state.Publisher)) {
            Write-Output "[FFmpeg 最后 100 行]"
            Get-Content -LiteralPath (
                Join-Path $LogsRoot "ffmpeg-live.stderr.log"
            ) -Tail 100 -ErrorAction SilentlyContinue
            throw "FFmpeg 实况发布器启动后立即退出。"
        }
        $state.Application = Start-Application
        Save-State $state
        Write-Output "[Start 5/5] 已后台提交；请把应用最大化到主屏"
        Write-Output "下一步：-Action Status / Capture"
    } catch {
        Save-State $state
        foreach ($record in @(
            $state.Application,
            $state.Publisher,
            $state.ReferenceClock,
            $state.SourceClock
        )) {
            $ownedProcess = Get-OwnedProcess $record
            if ($null -ne $ownedProcess) {
                Stop-Process -Id $ownedProcess.Id -Force -ErrorAction SilentlyContinue
            }
        }
        if (-not $state.NginxReused -and $null -ne $state.Nginx) {
            $ownedNginx = Get-OwnedProcess $state.Nginx
            if ($null -ne $ownedNginx) {
                Stop-Process -Id $ownedNginx.Id -Force -ErrorAction SilentlyContinue
            }
        }
        throw
    }
}

function Invoke-StartBroker {
    New-Item -ItemType Directory -Path $OutputRoot,$LogsRoot,$RuntimeRoot `
        -Force | Out-Null
    $launcherSuffix = [Guid]::NewGuid().ToString("N")
    $launcherScriptPath = Join-Path $RuntimeRoot (
        "launcher-start-{0}.cmd" -f $launcherSuffix
    )
    $launcherStdout = Join-Path $LogsRoot (
        "launcher-start-{0}.stdout.log" -f $launcherSuffix
    )
    $launcherStderr = Join-Path $LogsRoot (
        "launcher-start-{0}.stderr.log" -f $launcherSuffix
    )
    $powerShellPath = Join-Path $PSHOME "powershell.exe"
    $streamUrlsJson = ConvertTo-Json -InputObject @($StreamUrls) -Compress
    $streamUrlsBase64 = [Convert]::ToBase64String(
        [Text.Encoding]::UTF8.GetBytes($streamUrlsJson)
    )
    $arguments = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $PSCommandPath,
        "-Action", "Start",
        "-InternalLauncher",
        "-DurationSeconds", [string]$DurationSeconds,
        "-WarmupSeconds", [string]$WarmupSeconds,
        "-CaptureIntervalSeconds", [string]$CaptureIntervalSeconds,
        "-FfmpegPath", $FfmpegPath,
        "-NginxRoot", $NginxRoot,
        "-AppPath", $AppPath,
        "-OutputRoot", $OutputRoot,
        "-InternalStreamUrlsBase64", $streamUrlsBase64
    )

    $quotedArguments = @($arguments | ForEach-Object {
        '"{0}"' -f ([string]$_).Replace('"', '""')
    })
    $commandLine = '"{0}" {1} 1>"{2}" 2>"{3}"' -f `
        $powerShellPath,
        ($quotedArguments -join " "),
        $launcherStdout,
        $launcherStderr
    @(
        "@echo off",
        $commandLine,
        "exit /b %errorlevel%"
    ) | Set-Content -LiteralPath $launcherScriptPath -Encoding Ascii

    Write-LauncherResult "starting" "隐藏启动器已提交。"
    Write-Output "[Start] 后台提交隐藏启动器；本操作不等待长驻进程。"
    $launcher = Start-Process -FilePath $env:ComSpec `
        -ArgumentList @("/d", "/c", $launcherScriptPath) `
        -WorkingDirectory $ProjectRoot -PassThru -WindowStyle Hidden
    New-ProcessRecord $launcher $env:ComSpec "launcher" |
        ConvertTo-Json |
        Set-Content -LiteralPath $LauncherPidPath -Encoding UTF8
    Write-Output "[Start] 已返回：launcher PID=$($launcher.Id)"
    Write-Output "请执行 -Action Status 查看 succeeded/failed 和 16 路状态。"
}

function Save-Screenshot {
    param([string]$Path)
    Add-Type -AssemblyName System.Drawing
    Add-Type -AssemblyName System.Windows.Forms
    $screen = @([Windows.Forms.Screen]::AllScreens | Where-Object Primary)[0]
    $bitmap = New-Object Drawing.Bitmap($screen.Bounds.Width, $screen.Bounds.Height)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen(
            $screen.Bounds.X, $screen.Bounds.Y, 0, 0, $screen.Bounds.Size
        )
        $bitmap.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Invoke-Capture {
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
    $path = Join-Path $OutputRoot ("audit-{0:yyyyMMdd-HHmmss}.png" -f (Get-Date))
    Save-Screenshot $path
    Write-Host "已保存主屏审计截图：$path"
}

function Invoke-Status {
    Write-Stage "双屏实况延迟：状态"
    if (Test-Path -LiteralPath $LauncherResultPath) {
        $launcherResult = Get-Content -LiteralPath $LauncherResultPath -Raw |
            ConvertFrom-Json
        Write-Host (
            "启动器：{0}，{1}" -f
            $launcherResult.Status,
            $launcherResult.Message
        )
    }
    $state = Get-State
    foreach ($property in @("SourceClock", "ReferenceClock", "Publisher", "Application")) {
        $record = $state.$property
        $process = Get-OwnedProcess $record
        if ($null -eq $process) {
            Write-Host ("{0,-16} 缺失" -f $property) -ForegroundColor Red
        } else {
            Write-Host ("{0,-16} PID={1,-7} CPU={2,8:N1}s WS={3,7:N1} MiB" -f `
                $property, $process.Id, $process.CPU, ($process.WorkingSet64 / 1MB))
        }
    }
    if (Test-Path -LiteralPath $MetricsPath) {
        $metrics = Get-Content $MetricsPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $sampled = @($metrics.streams | Where-Object { $_.sourceLatencySamples -gt 0 }).Count
        $playing = @($metrics.streams | Where-Object state -eq "playing").Count
        Write-Host "应用指标：playing=$playing/16，已有延迟样本=$sampled/16，UI 最大间隔=$($metrics.maximumUiTimerGapMs) ms"
    } else {
        Write-Host "指标文件尚未生成。"
    }
}

function Get-Percentile {
    param([double[]]$Values, [double]$Fraction)
    if ($Values.Count -eq 0) { return -1 }
    $sorted = @($Values | Sort-Object)
    $index = [Math]::Max(0, [Math]::Min(
        $sorted.Count - 1, [Math]::Ceiling($Fraction * $sorted.Count) - 1
    ))
    return [double]$sorted[$index]
}

function Invoke-RunAutomated {
    $samples = New-Object Collections.Generic.List[object]
    try {
        Invoke-Start
        Write-Stage "预热 $WarmupSeconds 秒"
        Start-Sleep -Seconds $WarmupSeconds
        $state = Get-State
        $application = Get-OwnedProcess $state.Application
        if ($null -eq $application) { throw "应用在采样前退出。" }
        $previousCpu = [double]$application.CPU
        $previousTime = Get-Date

        Write-Stage "连续采样 $DurationSeconds 秒"
        for ($second = 1; $second -le $DurationSeconds; ++$second) {
            Start-Sleep -Seconds 1
            if (-not (Test-Path -LiteralPath $MetricsPath)) { continue }
            $metrics = Get-Content $MetricsPath -Raw -Encoding UTF8 | ConvertFrom-Json
            $application = Get-OwnedProcess $state.Application
            if ($null -eq $application) { throw "应用在采样期间退出。" }
            $now = Get-Date
            $cpu = ([double]$application.CPU - $previousCpu) * 100.0 /
                [Math]::Max(0.1, ($now - $previousTime).TotalSeconds)
            $previousCpu = [double]$application.CPU
            $previousTime = $now
            $samples.Add([pscustomobject]@{
                AtUtc = [DateTime]::UtcNow.ToString("O")
                Playing = @($metrics.streams | Where-Object state -eq "playing").Count
                SampledStreams = @($metrics.streams | Where-Object { $_.sourceLatencySamples -gt 0 }).Count
                WorstP95Ms = [double](($metrics.streams | Measure-Object sourceLatencyP95Ms -Maximum).Maximum)
                WorstMaximumMs = [double](($metrics.streams | Measure-Object sourceLatencyMaxMs -Maximum).Maximum)
                CpuPercent = $cpu
                WorkingSetMiB = $application.WorkingSet64 / 1MB
                UiGapMs = [int]$metrics.maximumUiTimerGapMs
            })
            if (($second % $CaptureIntervalSeconds) -eq 0) { Invoke-Capture }
            if (($second % 10) -eq 0) {
                Write-Host ("t={0,4}s playing={1}/16 sampled={2}/16 worstP95={3}ms CPU={4:N1}%" -f `
                    $second, $samples[$samples.Count - 1].Playing,
                    $samples[$samples.Count - 1].SampledStreams,
                    $samples[$samples.Count - 1].WorstP95Ms,
                    $samples[$samples.Count - 1].CpuPercent)
            }
        }

        $last = Get-Content $MetricsPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $streamResults = @($last.streams | ForEach-Object {
            [pscustomobject]@{
                StreamId = $_.streamId
                Name = $_.displayName
                Samples = [long]$_.sourceLatencySamples
                P50Ms = [long]$_.sourceLatencyP50Ms
                P95Ms = [long]$_.sourceLatencyP95Ms
                MaximumMs = [long]$_.sourceLatencyMaxMs
                Passed = ($_.sourceLatencySamples -gt 0 -and
                    $_.sourceLatencyP95Ms -le 750 -and $_.sourceLatencyMaxMs -le 1500)
            }
        })
        $averageCpu = [double](($samples | Measure-Object CpuPercent -Average).Average)
        $peakMemory = [double](($samples | Measure-Object WorkingSetMiB -Maximum).Maximum)
        $maximumUiGap = [int](($samples | Measure-Object UiGapMs -Maximum).Maximum)
        $passed = (@($streamResults | Where-Object { -not $_.Passed }).Count -eq 0) -and
            $averageCpu -le 85 -and $peakMemory -le 2048 -and $maximumUiGap -lt 500
        $report = [ordered]@{
            SchemaVersion = 1
            CompletedAtUtc = [DateTime]::UtcNow.ToString("O")
            DurationSeconds = $DurationSeconds
            Passed = $passed
            AverageCpuPercent = $averageCpu
            PeakWorkingSetMiB = $peakMemory
            MaximumUiGapMs = $maximumUiGap
            Streams = $streamResults
            Samples = $samples
        }
        $report | ConvertTo-Json -Depth 8 |
            Set-Content $ReportJsonPath -Encoding UTF8
        $rows = $streamResults | ForEach-Object {
            "| $($_.Name) | $($_.Samples) | $($_.P50Ms) | $($_.P95Ms) | $($_.MaximumMs) | $($_.Passed) |"
        }
        @(
            "# 16 路双屏实况延迟自动报告", "",
            "- 结果：$(if ($passed) {'通过'} else {'未通过'})",
            "- 时长：$DurationSeconds 秒",
            "- 应用平均 CPU：$([Math]::Round($averageCpu, 1))%",
            "- 峰值工作集：$([Math]::Round($peakMemory, 1)) MiB",
            "- UI 最大调度间隔：$maximumUiGap ms", "",
            "| 流 | 样本 | P50 ms | P95 ms | 最大 ms | 通过 |",
            "|---|---:|---:|---:|---:|---|"
        ) + $rows | Set-Content $ReportMarkdownPath -Encoding UTF8
        Write-Host "报告：$ReportMarkdownPath"
        if (-not $passed) { throw "双屏实况延迟硬门槛未全部通过。" }
    } finally {
        try { Invoke-Stop } catch { Write-Warning $_.Exception.Message }
    }
}

function Invoke-Stop {
    if (-not (Test-Path -LiteralPath $StatePath)) {
        Write-Host "无状态文件；Stop 无需执行。"
        return
    }
    $state = Get-State
    Stop-OwnedProcess $state.Application 5
    Stop-OwnedProcess $state.Publisher
    Stop-OwnedProcess $state.ReferenceClock
    Stop-OwnedProcess $state.SourceClock
    if (-not $state.NginxReused -and $null -ne $state.Nginx) {
        Stop-OwnedProcess $state.Nginx
    }
    Write-Host "已清理脚本拥有的应用、FFmpeg、时钟和 nginx 进程。"
}

try {
switch ($Action) {
    "Check" { Invoke-Check }
    "Start" {
        if ($InternalLauncher) {
            $deadline = [Week4LiveDeadline]::new(5000)
            Invoke-Start
            $deadline.Dispose()
            Write-LauncherResult "succeeded" "时钟、FFmpeg 和客户端已提交。"
            [Environment]::Exit(0)
        } else {
            Invoke-StartBroker
        }
    }
    "Status" { Invoke-Status }
    "Capture" { Invoke-Capture }
    "RunAutomated" { Invoke-RunAutomated }
    "Stop" { Invoke-Stop }
}
} catch {
    [Console]::Error.WriteLine("失败：{0}", $_.Exception.Message)
    Write-Output "日志目录：$LogsRoot"
    Write-Output "PID 文件：$StatePath"
    if ($InternalLauncher) {
        Write-LauncherResult "failed" $_.Exception.Message
        [Environment]::Exit(1)
    }
    exit 1
}
