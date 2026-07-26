<#
.SYNOPSIS
    用桌面实况测量 RTMP Monitor 的端到端显示延迟。

.DESCRIPTION
    Start 会在副屏显示源时钟，在主屏顶部显示同步参考时钟，然后使用
    FFmpeg gdigrab 捕获副屏并通过 nginx-rtmp 推送给 Qt 程序。

    Capture 会截取主屏。截图中“参考时钟”减去 Camera 01 内的“源时钟”
    即为采集、编码、RTMP、解码、颜色转换和 UI 绘制的总延迟。

    Stop 只停止本脚本启动的临时进程；若 nginx 在测试前已经运行，则不会停止它。

.EXAMPLE
    .\scripts\test_desktop_latency.ps1 -Action Check
    .\scripts\test_desktop_latency.ps1 -Action Start
    .\scripts\test_desktop_latency.ps1 -Action Capture -SampleCount 10
    .\scripts\test_desktop_latency.ps1 -Action Stop
#>

[CmdletBinding()]
param(
    [ValidateSet("Check", "Start", "Capture", "Stop", "Clock")]
    [string]$Action = "Check",

    [string]$FfmpegPath = "E:\DevTools\ffmpeg-8.1.2-full_build\bin\ffmpeg.exe",
    [string]$NginxRoot = "E:\DevTools\nginx-rtmp",
    [string]$AppPath = "",
    [string]$QtBin = "E:\QT6\6.6.1\msvc2019_64\bin",
    [string]$StreamUrl = "rtmp://127.0.0.1:1935/live/camera001",

    [ValidateRange(1, 100)]
    [int]$SampleCount = 10,

    [ValidateRange(100, 10000)]
    [int]$SampleIntervalMs = 1000,

    # 以下参数仅供脚本递归启动测试时钟使用。
    [ValidateSet("Source", "Reference")]
    [string]$ClockRole = "Source",
    [int]$ClockLeft = 0,
    [int]$ClockTop = 0,
    [int]$ClockWidth = 800,
    [int]$ClockHeight = 450
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$OutputRoot = Join-Path $ProjectRoot "out\desktop-latency"
$StatePath = Join-Path $OutputRoot "state.json"
$NginxExe = Join-Path $NginxRoot "sbin\nginx.exe"

if ([string]::IsNullOrWhiteSpace($AppPath)) {
    $AppPath = Join-Path $ProjectRoot "out\build-windows-x64\debug\rtmp_monitor.exe"
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

function Get-TestDisplays {
    Add-Type -AssemblyName System.Windows.Forms

    $primary = [System.Windows.Forms.Screen]::AllScreens |
        Where-Object Primary |
        Select-Object -First 1
    $source = [System.Windows.Forms.Screen]::AllScreens |
        Where-Object { -not $_.Primary } |
        Select-Object -First 1

    if ($null -eq $primary) {
        throw "没有检测到 Windows 主显示器。"
    }
    if ($null -eq $source) {
        throw "桌面实况测试需要第二块显示器，以避免把播放器递归采集进去。"
    }

    return [PSCustomObject]@{
        Primary = $primary
        Source = $source
    }
}

function Test-RtmpPort {
    $connection = Get-NetTCPConnection `
        -LocalPort 1935 `
        -State Listen `
        -ErrorAction SilentlyContinue |
        Select-Object -First 1

    return $null -ne $connection
}

function Wait-RtmpPort {
    param([int]$TimeoutMs = 5000)

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-RtmpPort) {
            return
        }
        Start-Sleep -Milliseconds 100
    }

    throw "nginx-rtmp 未在 ${TimeoutMs}ms 内监听 1935 端口。"
}

function Start-TestClock {
    param(
        [Parameter(Mandatory)][ValidateSet("Source", "Reference")]
        [string]$Role,
        [Parameter(Mandatory)][System.Drawing.Rectangle]$Bounds
    )

    $arguments = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-STA",
        "-File", $PSCommandPath,
        "-Action", "Clock",
        "-ClockRole", $Role,
        "-ClockLeft", $Bounds.X,
        "-ClockTop", $Bounds.Y,
        "-ClockWidth", $Bounds.Width,
        "-ClockHeight", $Bounds.Height
    )

    return Start-Process `
        -FilePath "powershell.exe" `
        -ArgumentList $arguments `
        -WindowStyle Hidden `
        -PassThru
}

function Show-TestClock {
    Add-Type -AssemblyName PresentationCore
    Add-Type -AssemblyName PresentationFramework
    Add-Type -AssemblyName WindowsBase

    $window = [System.Windows.Window]::new()
    $window.Title = "RTMP_LATENCY_$($ClockRole.ToUpperInvariant())"
    $window.WindowStartupLocation = [System.Windows.WindowStartupLocation]::Manual
    $window.WindowStyle = [System.Windows.WindowStyle]::None
    $window.ResizeMode = [System.Windows.ResizeMode]::NoResize
    $window.Left = $ClockLeft
    $window.Top = $ClockTop
    $window.Width = $ClockWidth
    $window.Height = $ClockHeight
    $window.Topmost = $true
    $window.Background = [System.Windows.Media.Brushes]::Black

    $grid = [System.Windows.Controls.Grid]::new()
    $grid.Margin = [System.Windows.Thickness]::new(20)

    $label = [System.Windows.Controls.TextBlock]::new()
    $label.Text = if ($ClockRole -eq "Source") {
        "SOURCE / 被采集的桌面时钟"
    } else {
        "REFERENCE / 当前时钟"
    }
    $label.Foreground = if ($ClockRole -eq "Source") {
        [System.Windows.Media.Brushes]::DeepSkyBlue
    } else {
        [System.Windows.Media.Brushes]::Lime
    }
    $label.FontFamily = [System.Windows.Media.FontFamily]::new("Consolas")
    $label.FontSize = [Math]::Max(22, [Math]::Min(48, $ClockHeight * 0.08))
    $label.FontWeight = [System.Windows.FontWeights]::Bold
    $label.HorizontalAlignment = [System.Windows.HorizontalAlignment]::Center
    $label.VerticalAlignment = [System.Windows.VerticalAlignment]::Top

    $clock = [System.Windows.Controls.TextBlock]::new()
    $clock.Foreground = [System.Windows.Media.Brushes]::White
    $clock.FontFamily = [System.Windows.Media.FontFamily]::new("Consolas")
    # Keep the complete HH:mm:ss.fff string inside the gdigrab rectangle even
    # when the source display uses 125% Windows scaling.
    $clock.FontSize = [Math]::Max(48, [Math]::Min(130, $ClockHeight * 0.22))
    $clock.FontWeight = [System.Windows.FontWeights]::Bold
    $clock.HorizontalAlignment = [System.Windows.HorizontalAlignment]::Center
    $clock.VerticalAlignment = [System.Windows.VerticalAlignment]::Center

    $epoch = [System.Windows.Controls.TextBlock]::new()
    $epoch.Foreground = [System.Windows.Media.Brushes]::Silver
    $epoch.FontFamily = [System.Windows.Media.FontFamily]::new("Consolas")
    $epoch.FontSize = [Math]::Max(18, [Math]::Min(42, $ClockHeight * 0.07))
    $epoch.HorizontalAlignment = [System.Windows.HorizontalAlignment]::Center
    $epoch.VerticalAlignment = [System.Windows.VerticalAlignment]::Bottom
    $epoch.Margin = [System.Windows.Thickness]::new(0, 0, 0, 35)

    $progress = [System.Windows.Controls.ProgressBar]::new()
    $progress.Minimum = 0
    $progress.Maximum = 999
    $progress.Height = [Math]::Max(12, $ClockHeight * 0.025)
    $progress.VerticalAlignment = [System.Windows.VerticalAlignment]::Bottom

    [void]$grid.Children.Add($label)
    [void]$grid.Children.Add($clock)
    [void]$grid.Children.Add($epoch)
    [void]$grid.Children.Add($progress)
    $window.Content = $grid

    $timer = [System.Windows.Threading.DispatcherTimer]::new(
        [System.Windows.Threading.DispatcherPriority]::Render
    )
    $timer.Interval = [TimeSpan]::FromMilliseconds(10)
    $timer.Add_Tick({
        $now = [DateTimeOffset]::Now
        $clock.Text = $now.ToString("HH:mm:ss.fff")
        $epoch.Text = "Unix ms: $($now.ToUnixTimeMilliseconds())"
        $progress.Value = $now.Millisecond
    })

    $window.Add_Closed({ $timer.Stop() })
    $timer.Start()
    [void]$window.ShowDialog()
}

function Invoke-Check {
    Assert-File -Path $FfmpegPath -Description "FFmpeg"
    Assert-File -Path $NginxExe -Description "nginx-rtmp"
    Assert-File -Path $AppPath -Description "Qt Debug 程序"
    Assert-File -Path (Join-Path $QtBin "Qt6Core.dll") -Description "Qt6 运行库"

    $devices = & $FfmpegPath -hide_banner -devices 2>&1 | Out-String
    $encoders = & $FfmpegPath -hide_banner -encoders 2>&1 | Out-String
    if ($devices -notmatch "(?m)^\s*D\s+gdigrab\s") {
        throw "当前 FFmpeg 不支持 gdigrab 桌面采集。"
    }
    if ($encoders -notmatch "(?m)^\s*V\S*\s+libx264\s") {
        throw "当前 FFmpeg 不支持 libx264 H.264 编码。"
    }

    $displays = Get-TestDisplays
    Write-Host "[通过] FFmpeg gdigrab + libx264"
    Write-Host "[通过] Qt 程序：$AppPath"
    Write-Host "[通过] 主屏：$($displays.Primary.Bounds)"
    Write-Host "[通过] 采集副屏：$($displays.Source.Bounds)"
    Write-Host "[信息] RTMP 1935 当前监听：$(Test-RtmpPort)"
}

function Invoke-Start {
    Invoke-Check
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

    if (Test-Path -LiteralPath $StatePath) {
        $oldState = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json
        $oldProcesses = @(
            @(
                $oldState.FfmpegPid,
                $oldState.AppPid,
                $oldState.SourceClockPid,
                $oldState.ReferenceClockPid
            ) | Where-Object {
                $_ -and (Get-Process -Id $_ -ErrorAction SilentlyContinue)
            }
        )
        if ($oldProcesses.Count -gt 0) {
            throw "已有桌面延迟测试正在运行。请先执行 -Action Stop。"
        }
    }

    $displays = Get-TestDisplays
    $primary = $displays.Primary.Bounds
    $source = $displays.Source.Bounds
    $referenceBounds = [System.Drawing.Rectangle]::new(
        $primary.X,
        $primary.Y,
        $primary.Width,
        [Math]::Min(180, [Math]::Floor($primary.Height * 0.18))
    )

    $serverWasRunning = Test-RtmpPort
    if (-not $serverWasRunning) {
        $nginxArguments = @(
            "-p", "$NginxRoot\",
            "-c", "conf\nginx.conf"
        )
        [void](Start-Process `
            -FilePath $NginxExe `
            -ArgumentList $nginxArguments `
            -WorkingDirectory $NginxRoot `
            -WindowStyle Hidden `
            -PassThru)
        Wait-RtmpPort
    }

    $sourceClock = Start-TestClock -Role Source -Bounds $source
    $referenceClock = Start-TestClock -Role Reference -Bounds $referenceBounds
    Start-Sleep -Milliseconds 800

    $ffmpegLog = Join-Path $OutputRoot "ffmpeg.log"
    $ffmpegStdout = Join-Path $OutputRoot "ffmpeg.stdout.log"
    $ffmpegArguments = @(
        "-hide_banner",
        "-loglevel", "info",
        "-f", "gdigrab",
        "-framerate", "60",
        "-draw_mouse", "1",
        "-offset_x", $source.X,
        "-offset_y", $source.Y,
        "-video_size", "$($source.Width)x$($source.Height)",
        "-i", "desktop",
        "-an",
        "-c:v", "libx264",
        "-preset", "ultrafast",
        "-tune", "zerolatency",
        "-profile:v", "baseline",
        "-pix_fmt", "yuv420p",
        "-g", "30",
        "-keyint_min", "30",
        "-sc_threshold", "0",
        "-bf", "0",
        "-refs", "1",
        "-threads", "4",
        "-b:v", "4M",
        "-maxrate", "4M",
        "-bufsize", "500k",
        "-flush_packets", "1",
        "-f", "flv",
        $StreamUrl
    )
    $ffmpeg = Start-Process `
        -FilePath $FfmpegPath `
        -ArgumentList $ffmpegArguments `
        -RedirectStandardOutput $ffmpegStdout `
        -RedirectStandardError $ffmpegLog `
        -WindowStyle Hidden `
        -PassThru

    Start-Sleep -Milliseconds 1000
    if ($ffmpeg.HasExited) {
        throw "FFmpeg 推流进程提前退出。请检查：$ffmpegLog"
    }

    $oldPath = $env:Path
    try {
        $env:Path = "$QtBin;$(Split-Path -Parent $FfmpegPath);$oldPath"
        $app = Start-Process `
            -FilePath $AppPath `
            -ArgumentList @("--url", $StreamUrl) `
            -WorkingDirectory (Split-Path -Parent $AppPath) `
            -WindowStyle Maximized `
            -PassThru
    }
    finally {
        $env:Path = $oldPath
    }

    $appDeadline = [DateTime]::UtcNow.AddSeconds(8)
    while ($app.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $appDeadline) {
        Start-Sleep -Milliseconds 100
        $app.Refresh()
    }

    if ($app.MainWindowHandle -ne 0) {
        Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class DesktopLatencyNativeMethods {
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool MoveWindow(
        IntPtr hWnd, int x, int y, int width, int height, bool repaint);
}
"@
        $appTop = $referenceBounds.Bottom
        [void][DesktopLatencyNativeMethods]::MoveWindow(
            $app.MainWindowHandle,
            $primary.X,
            $appTop,
            $primary.Width,
            $primary.Bottom - $appTop,
            $true
        )
    }

    # Start-Process may not expose a Qt HWND immediately, but AppActivate can
    # still raise the process window by PID. The reference clock remains above
    # it because that test window is intentionally Topmost.
    Add-Type -AssemblyName Microsoft.VisualBasic
    [void][Microsoft.VisualBasic.Interaction]::AppActivate($app.Id)

    $state = [PSCustomObject]@{
        StartedAt = [DateTimeOffset]::Now.ToString("O")
        StreamUrl = $StreamUrl
        ServerWasRunning = $serverWasRunning
        FfmpegPid = $ffmpeg.Id
        AppPid = $app.Id
        SourceClockPid = $sourceClock.Id
        ReferenceClockPid = $referenceClock.Id
        PrimaryBounds = [PSCustomObject]@{
            X = $primary.X
            Y = $primary.Y
            Width = $primary.Width
            Height = $primary.Height
        }
        SourceBounds = [PSCustomObject]@{
            X = $source.X
            Y = $source.Y
            Width = $source.Width
            Height = $source.Height
        }
        Encoder = "libx264 ultrafast zerolatency, 60 fps, GOP 30, B-frames 0"
    }
    $state | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $StatePath -Encoding UTF8

    Write-Host ""
    Write-Host "桌面延迟测试已启动。" -ForegroundColor Green
    Write-Host "副屏：SOURCE 时钟（被 FFmpeg 采集）"
    Write-Host "主屏顶部：REFERENCE 当前时钟"
    Write-Host "主屏下部：Qt Camera 01（应显示延迟后的 SOURCE 时钟）"
    Write-Host ""
    Write-Host "等待画面稳定 5 秒后执行："
    Write-Host "  .\scripts\test_desktop_latency.ps1 -Action Capture -SampleCount 10"
}

function Invoke-Capture {
    if (-not (Test-Path -LiteralPath $StatePath -PathType Leaf)) {
        throw "找不到测试状态。请先执行 -Action Start。"
    }

    $state = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json
    if (-not (Get-Process -Id $state.AppPid -ErrorAction SilentlyContinue)) {
        throw "Qt 测试程序没有运行。"
    }
    if (-not (Get-Process -Id $state.FfmpegPid -ErrorAction SilentlyContinue)) {
        throw "FFmpeg 桌面推流进程没有运行。"
    }

    Add-Type -AssemblyName System.Drawing
    Add-Type -AssemblyName Microsoft.VisualBasic
    [void][Microsoft.VisualBasic.Interaction]::AppActivate([int]$state.AppPid)
    Start-Sleep -Milliseconds 500
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

    $bounds = [System.Drawing.Rectangle]::new(
        [int]$state.PrimaryBounds.X,
        [int]$state.PrimaryBounds.Y,
        [int]$state.PrimaryBounds.Width,
        [int]$state.PrimaryBounds.Height
    )

    $captured = @()
    for ($index = 1; $index -le $SampleCount; ++$index) {
        $bitmap = [System.Drawing.Bitmap]::new(
            $bounds.Width,
            $bounds.Height,
            [System.Drawing.Imaging.PixelFormat]::Format24bppRgb
        )
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CopyFromScreen(
                $bounds.X,
                $bounds.Y,
                0,
                0,
                $bounds.Size,
                [System.Drawing.CopyPixelOperation]::SourceCopy
            )
            $capturedAt = [DateTimeOffset]::Now
            $fileName = "sample-{0:D2}-{1}.png" -f `
                $index, $capturedAt.ToString("HHmmss-fff")
            $path = Join-Path $OutputRoot $fileName
            $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
            $captured += [PSCustomObject]@{
                Index = $index
                CapturedAt = $capturedAt.ToString("O")
                Path = $path
            }
            Write-Host "[$index/$SampleCount] $path"
        }
        finally {
            $graphics.Dispose()
            $bitmap.Dispose()
        }

        if ($index -lt $SampleCount) {
            Start-Sleep -Milliseconds $SampleIntervalMs
        }
    }

    $manifestPath = Join-Path $OutputRoot "samples.json"
    $captured | ConvertTo-Json -Depth 3 |
        Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Write-Host "采样完成：$manifestPath" -ForegroundColor Green
}

function Invoke-Stop {
    if (-not (Test-Path -LiteralPath $StatePath -PathType Leaf)) {
        Write-Host "没有找到运行中的桌面延迟测试状态。"
        return
    }

    $state = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json
    foreach ($property in @(
        "AppPid",
        "FfmpegPid",
        "SourceClockPid",
        "ReferenceClockPid"
    )) {
        $processId = $state.$property
        if ($processId) {
            $process = Get-Process -Id $processId -ErrorAction SilentlyContinue
            if ($null -ne $process) {
                Stop-Process -Id $processId -Force
                Write-Host "已停止 $property：$processId"
            }
        }
    }

    if (-not [bool]$state.ServerWasRunning -and (Test-RtmpPort)) {
        Push-Location $NginxRoot
        try {
            & $NginxExe `
                -p "$NginxRoot\" `
                -c "conf\nginx.conf" `
                -s quit
        }
        finally {
            Pop-Location
        }
        Write-Host "已请求 nginx-rtmp 优雅退出。"
    }

    $state | Add-Member `
        -NotePropertyName StoppedAt `
        -NotePropertyValue ([DateTimeOffset]::Now.ToString("O")) `
        -Force
    $state | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath $StatePath -Encoding UTF8
    Write-Host "桌面延迟测试已停止。" -ForegroundColor Green
}

switch ($Action) {
    "Check" { Invoke-Check }
    "Start" { Invoke-Start }
    "Capture" { Invoke-Capture }
    "Stop" { Invoke-Stop }
    "Clock" { Show-TestClock }
}
