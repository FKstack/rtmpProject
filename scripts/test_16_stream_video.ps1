<#
.SYNOPSIS
    16 路带标签预录视频的分阶段功能与性能验收。

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File .\scripts\test_16_stream_video.ps1 -Action Check
.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File .\scripts\test_16_stream_video.ps1 -Action RunAutomated
#>
[CmdletBinding()]
param(
    [ValidateSet(
        "Check", "Prepare", "Start", "Status", "Test", "RunAutomated",
        "StopStream", "StartStream", "StopStreams", "StartStreams",
        "StartApp", "Stop"
    )]
    [string]$Action = "Check",
    [ValidateRange(1, 16)]
    [int]$StreamNumber = 3,
    [ValidateRange(30, 7200)]
    [int]$DurationSeconds = 600,
    [ValidateRange(1, 120)]
    [int]$WarmupSeconds = 20,
    [ValidateRange(1, 60)]
    [int]$HealthTimeoutSeconds = 60,
    [string]$FfmpegPath = "E:\DevTools\ffmpeg-8.1.2-full_build\bin\ffmpeg.exe",
    [string]$FfprobePath = "E:\DevTools\ffmpeg-8.1.2-full_build\bin\ffprobe.exe",
    [string]$NginxRoot = "E:\DevTools\nginx-rtmp",
    [string]$InputFile = "",
    [string]$AppPath = "",
    [string]$OutputRoot = "",
    [string[]]$StreamUrls = @(),
    [switch]$InternalLauncher,
    [string]$InternalStreamUrlsBase64 = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Codex Desktop 当前进程可能同时继承 Path/PATH；Windows PowerShell 5.1 的
# Start-Process 会因此抛出重复键。仅规范化本脚本进程的继承块，不改用户环境。
$InheritedExecutablePath = cmd.exe /d /c echo %PATH%
[Environment]::SetEnvironmentVariable("PATH", $null, "Process")
[Environment]::SetEnvironmentVariable("Path", $null, "Process")
[Environment]::SetEnvironmentVariable(
    "Path", [string]$InheritedExecutablePath, "Process"
)

# Windows PowerShell 5.1 为 Start-Process 的 stdout/stderr 重定向使用
# ThreadPool 回调。默认最小线程数过低时，多个长驻进程会占满工作线程，
# 使后续 Start-Process 在第 5 路附近长时间无返回。提前扩容只影响当前
# 脚本宿主；进程仍由 Start-Process 后台启动，并继续写入各自日志文件。
$LongLivedStartActions = @("Start", "StartStream", "StartStreams", "StartApp")

if (($Action -in $LongLivedStartActions -and $InternalLauncher) -or
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

if ((($Action -in $LongLivedStartActions -and $InternalLauncher) -or
     $Action -eq "Test") -and
    $null -eq ("Week4ProcessDeadline" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Threading;

public sealed class Week4ProcessDeadline : IDisposable
{
    private Timer timer;

    public Week4ProcessDeadline(int milliseconds, int exitCode)
    {
        timer = new Timer(
            _ => Environment.Exit(exitCode),
            null,
            milliseconds,
            Timeout.Infinite
        );
    }

    public void Dispose()
    {
        Timer current = Interlocked.Exchange(ref timer, null);
        if (current != null) {
            current.Dispose();
        }
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
if ([string]::IsNullOrWhiteSpace($InputFile)) {
    $InputFile = Join-Path $ProjectRoot "testdata\test.mp4"
}
if ([string]::IsNullOrWhiteSpace($AppPath)) {
    $AppPath = Join-Path $ProjectRoot "out\build-windows-x64\debug\rtmp_monitor.exe"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $ProjectRoot "out\16-stream-video"
}
if ($StreamUrls.Count -eq 0) {
    $StreamUrls = @(
        1..16 | ForEach-Object {
            "rtmp://127.0.0.1:1935/live/camera{0:D3}" -f $_
        }
    )
}

$AssetRoot = Join-Path $OutputRoot "assets"
$LogsRoot = Join-Path $ProjectRoot "out\logs\16-stream-video"
$RuntimeRoot = Join-Path $ProjectRoot "out\runtime\16-stream-video"
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
$ReportJsonPath = Join-Path $OutputRoot "automated-report.json"
$ReportMarkdownPath = Join-Path $OutputRoot "automated-report.md"
$FontPath = "C:\Windows\Fonts\arial.ttf"
$Colors = @(
    "0xD32F2F", "0x1976D2", "0x388E3C", "0xF57C00",
    "0x7B1FA2", "0x00796B", "0xC2185B", "0x455A64",
    "0x5D4037", "0x303F9F", "0x689F38", "0xAFB42B",
    "0xE64A19", "0x512DA8", "0x0097A7", "0x616161"
)

function Write-Stage {
    param([Parameter(Mandatory)][string]$Text)
    Write-Host ""
    Write-Host ("=" * 68) -ForegroundColor DarkGray
    Write-Host $Text -ForegroundColor Cyan
    Write-Host ("=" * 68) -ForegroundColor DarkGray
}

function Assert-File {
    param([string]$Path, [string]$Description)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description 不存在：$Path"
    }
}

function Assert-Urls {
    if ($StreamUrls.Count -ne 16) {
        throw "StreamUrls 必须恰好包含 16 个地址，当前为 $($StreamUrls.Count)。"
    }
    $seen = @{}
    foreach ($urlText in $StreamUrls) {
        $uri = $null
        if (-not [Uri]::TryCreate($urlText, [UriKind]::Absolute, [ref]$uri) -or
            $uri.Scheme -ne "rtmp" -or [string]::IsNullOrWhiteSpace($uri.Host) -or
            [string]::IsNullOrWhiteSpace($uri.AbsolutePath)) {
            throw "无效 RTMP URL：$urlText"
        }
        if ($seen.ContainsKey($urlText)) {
            throw "RTMP URL 不能重复：$urlText"
        }
        $seen[$urlText] = $true
    }
}

function Initialize-NginxRuntime {
    foreach ($directory in @("sbin", "conf", "logs", "temp")) {
        New-Item -ItemType Directory -Path (
            Join-Path $NginxRuntimeRoot $directory
        ) -Force | Out-Null
    }
    Copy-Item -LiteralPath $NginxSourceExe -Destination $NginxExe -Force
    @"
daemon off;
master_process off;
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
        application live {
            live on;
            record off;
        }
    }
}
"@ | Set-Content -LiteralPath $NginxConfig -Encoding Ascii
}

function Show-LogTails {
    $logFiles = @()
    if (Test-Path -LiteralPath $LogsRoot) {
        $logFiles += @(Get-ChildItem -LiteralPath $LogsRoot -Filter "*.log" `
            -File -ErrorAction SilentlyContinue)
    }
    $nginxInternalLog = Join-Path $NginxRuntimeRoot "logs\error.log"
    if (Test-Path -LiteralPath $nginxInternalLog -PathType Leaf) {
        $logFiles += Get-Item -LiteralPath $nginxInternalLog
    }
    foreach ($logFile in @($logFiles | Sort-Object LastWriteTime)) {
        Write-Output ""
        Write-Output ("--- {0}（最后 100 行）---" -f $logFile.Name)
        Get-Content -LiteralPath $logFile.FullName -Tail 100 `
            -ErrorAction SilentlyContinue
    }
}

function Wait-WithProgress {
    param(
        [ValidateRange(0, 120)][int]$Seconds,
        [string]$Stage
    )
    for ($elapsed = 0; $elapsed -lt $Seconds; ++$elapsed) {
        if (($elapsed % 5) -eq 0) {
            Write-Output ("[{0}] {1}/{2} 秒" -f $Stage, $elapsed, $Seconds)
        }
        Start-Sleep -Seconds 1
    }
}

function Invoke-BoundedProcess {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$StdoutPath,
        [string]$StderrPath,
        [string]$Stage,
        [ValidateRange(1, 60)][int]$TimeoutSeconds = 60
    )
    $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments `
        -WindowStyle Hidden -RedirectStandardOutput $StdoutPath `
        -RedirectStandardError $StderrPath -PassThru
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $nextProgress = 0
    while (-not $process.HasExited -and
           $watch.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        if ($watch.Elapsed.TotalSeconds -ge $nextProgress) {
            Write-Output ("[{0}] 已运行 {1:N0} 秒" -f
                $Stage, $watch.Elapsed.TotalSeconds)
            $nextProgress += 5
        }
        Start-Sleep -Milliseconds 200
        $process.Refresh()
    }
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "$Stage 超过 $TimeoutSeconds 秒截止时间。"
    }
    [void]$process.WaitForExit(1000)
    $process.Refresh()
    if ($null -ne $process.ExitCode -and $process.ExitCode -ne 0) {
        throw "$Stage 失败，退出码 $($process.ExitCode)。"
    }
}

function Test-RtmpPort {
    $client = New-Object Net.Sockets.TcpClient
    try {
        $task = $client.ConnectAsync("127.0.0.1", 1935)
        return $task.Wait(250) -and $client.Connected
    } catch {
        return $false
    } finally {
        $client.Dispose()
    }
}

function Wait-RtmpPort {
    param([int]$TimeoutMs = 2500)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.ElapsedMilliseconds -lt $TimeoutMs) {
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
    $process = Get-OwnedProcess -Record $Record
    if ($null -eq $process) { return }
    try {
        if ($Record.Role -eq "application") {
            [void]$process.CloseMainWindow()
            if ($process.WaitForExit($GraceSeconds * 1000)) { return }
        }
        Stop-Process -Id $process.Id -Force -ErrorAction Stop
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
        Action = $Action
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
    Write-Stage "检查 16 路视频验收环境"
    New-Item -ItemType Directory -Path $LogsRoot -Force | Out-Null
    Assert-Urls
    Assert-File $FfmpegPath "FFmpeg"
    Assert-File $FfprobePath "ffprobe"
    Assert-File $NginxSourceExe "nginx"
    Assert-File $NginxSourceConfig "nginx 配置"
    Assert-File $InputFile "测试视频"
    Assert-File $AppPath "RtmpMonitor Debug 程序"
    Assert-File $FontPath "Arial 字体"

    $queries = @(
        @{ Name = "encoders"; Arguments = @("-hide_banner", "-encoders") },
        @{ Name = "filters"; Arguments = @("-hide_banner", "-filters") },
        @{ Name = "formats"; Arguments = @("-hide_banner", "-formats") },
        @{ Name = "protocols"; Arguments = @("-hide_banner", "-protocols") }
    )
    foreach ($query in $queries) {
        Invoke-BoundedProcess -FilePath $FfmpegPath `
            -Arguments $query.Arguments `
            -StdoutPath (Join-Path $LogsRoot ("check-{0}.stdout.log" -f $query.Name)) `
            -StderrPath (Join-Path $LogsRoot ("check-{0}.stderr.log" -f $query.Name)) `
            -Stage ("FFmpeg " + $query.Name) -TimeoutSeconds 10
    }
    $encoders = Get-Content (Join-Path $LogsRoot "check-encoders.stdout.log") -Raw
    $filters = Get-Content (Join-Path $LogsRoot "check-filters.stdout.log") -Raw
    $formats = Get-Content (Join-Path $LogsRoot "check-formats.stdout.log") -Raw
    $protocols = Get-Content (Join-Path $LogsRoot "check-protocols.stdout.log") -Raw
    if ($encoders -notmatch "\blibx264\b") { throw "FFmpeg 缺少 libx264。" }
    if ($filters -notmatch "\bdrawtext\b") { throw "FFmpeg 缺少 drawtext。" }
    if ($formats -notmatch "\bflv\b") { throw "FFmpeg 缺少 FLV。" }
    if ($protocols -notmatch "(?m)^\s*rtmp\s*$") { throw "FFmpeg 缺少 RTMP。" }

    $probeStdout = Join-Path $LogsRoot "check-probe.stdout.log"
    Invoke-BoundedProcess -FilePath $FfprobePath -Arguments @(
        "-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=codec_name,width,height,r_frame_rate",
        "-of", "default=noprint_wrappers=1", $InputFile
    ) -StdoutPath $probeStdout `
        -StderrPath (Join-Path $LogsRoot "check-probe.stderr.log") `
        -Stage "ffprobe 输入视频" -TimeoutSeconds 10
    $probe = Get-Content -LiteralPath $probeStdout -Raw
    if ($probe -notmatch "codec_name=h264") {
        throw "测试视频第一路视频必须为 H.264。"
    }
    Write-Host "[通过] 16 个 URL、FFmpeg、nginx、测试视频、字体和应用可用"
    Write-Host "RTMP 1935 当前状态：$(if (Test-RtmpPort) {'监听中'} else {'空闲'})"
    Write-Host "下一步：-Action Prepare"
}

function Invoke-Prepare {
    Invoke-Check
    Write-Stage "顺序生成 16 个 720p30 带标签测试素材"
    New-Item -ItemType Directory -Path $AssetRoot -Force | Out-Null
    $font = $FontPath.Replace("\", "/").Replace(":", "\:")

    for ($camera = 1; $camera -le 16; $camera++) {
        $asset = Join-Path $AssetRoot ("camera{0:D3}.flv" -f $camera)
        if (Test-Path -LiteralPath $asset) {
            $assetProbeStdout = Join-Path $LogsRoot (
                "probe-camera{0:D3}.stdout.log" -f $camera
            )
            try {
                Invoke-BoundedProcess -FilePath $FfprobePath -Arguments @(
                    "-v", "error", "-select_streams", "v:0",
                    "-show_entries", "stream=codec_name,width,height,r_frame_rate",
                    "-of", "csv=p=0", $asset
                ) -StdoutPath $assetProbeStdout `
                    -StderrPath (Join-Path $LogsRoot (
                        "probe-camera{0:D3}.stderr.log" -f $camera
                    )) -Stage ("探测 CAMERA {0:D3}" -f $camera) `
                    -TimeoutSeconds 10
                $probe = Get-Content -LiteralPath $assetProbeStdout -Raw
            } catch {
                $probe = ""
                Write-Output ("[{0:D2}/16] 缓存无效，将重新生成。" -f $camera)
            }
            if ($probe -match "h264,1280,720,30/1") {
                Write-Host ("[{0:D2}/16] 复用 {1}" -f $camera, $asset)
                continue
            }
        }

        Write-Host ("[{0:D2}/16] 生成 CAMERA {0:D3}" -f $camera)
        $label = "CAMERA {0:D3}" -f $camera
        $filter = "scale=1280:720:force_original_aspect_ratio=decrease," +
            "pad=1280:720:(ow-iw)/2:(oh-ih)/2:color=$($Colors[$camera-1])," +
            "drawbox=x=28:y=28:w=420:h=92:color=black@0.65:t=fill," +
            "drawtext=fontfile='$font':text='$label':x=48:y=50:" +
            "fontsize=44:fontcolor=white"
        $stdoutLog = Join-Path $LogsRoot (
            "prepare-camera{0:D3}.stdout.log" -f $camera
        )
        $stderrLog = Join-Path $LogsRoot (
            "prepare-camera{0:D3}.stderr.log" -f $camera
        )
        Invoke-BoundedProcess -FilePath $FfmpegPath -Arguments @(
            "-hide_banner", "-y", "-stream_loop", "-1", "-i", $InputFile,
            "-t", "8", "-an", "-vf", $filter, "-r", "30",
            "-c:v", "libx264", "-preset", "ultrafast",
            "-tune", "zerolatency", "-pix_fmt", "yuv420p",
            "-g", "30", "-keyint_min", "30", "-sc_threshold", "0",
            "-bf", "0", "-f", "flv", $asset
        ) -StdoutPath $stdoutLog -StderrPath $stderrLog `
            -Stage ("准备 CAMERA {0:D3}" -f $camera) -TimeoutSeconds 60
    }

    [ordered]@{
        SchemaVersion = 1
        GeneratedAtUtc = [DateTime]::UtcNow.ToString("O")
        InputFile = [IO.Path]::GetFullPath($InputFile)
        Resolution = "1280x720"
        FramesPerSecond = 30
        Gop = 30
        Assets = @(1..16 | ForEach-Object {
            [IO.Path]::GetFullPath(
                (Join-Path $AssetRoot ("camera{0:D3}.flv" -f $_))
            )
        })
    } | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $AssetRoot "manifest.json") -Encoding UTF8
    Write-Host "[通过] 16 个预编码素材已准备，下一步：-Action Start"
}

function Start-Nginx {
    param(
        [object]$State,
        [Diagnostics.Stopwatch]$StartWatch
    )
    if (Test-RtmpPort) {
        $State.NginxReused = $true
        return
    }
    Initialize-NginxRuntime
    $process = Start-Process -FilePath $NginxExe -ArgumentList @(
        "-p", ($NginxRuntimeRoot.TrimEnd("\") + "\"),
        "-c", "conf/nginx.conf"
    ) -WorkingDirectory $NginxRuntimeRoot -WindowStyle Hidden `
        -RedirectStandardOutput (Join-Path $LogsRoot "nginx.stdout.log") `
        -RedirectStandardError (Join-Path $LogsRoot "nginx.stderr.log") `
        -PassThru
    $State.Nginx = New-ProcessRecord $process $NginxExe "nginx"
    Save-State $State
    $remainingMs = [Math]::Max(
        1, [Math]::Min(2500, 9000 - [int]$StartWatch.ElapsedMilliseconds)
    )
    if (-not (Wait-RtmpPort -TimeoutMs $remainingMs)) {
        throw "nginx 启动后 1935 端口未监听。"
    }
}

function Start-Publisher {
    param([int]$Number, [object]$State)
    $asset = Join-Path $AssetRoot ("camera{0:D3}.flv" -f $Number)
    Assert-File $asset "Camera $Number 预编码素材"
    $stdout = Join-Path $LogsRoot ("camera{0:D3}.stdout.log" -f $Number)
    $stderr = Join-Path $LogsRoot ("camera{0:D3}.stderr.log" -f $Number)
    $process = Start-Process -FilePath $FfmpegPath -ArgumentList @(
        "-hide_banner", "-nostdin", "-loglevel", "warning",
        "-re", "-stream_loop", "-1", "-i", $asset,
        "-map", "0:v:0", "-an", "-c:v", "copy",
        "-flvflags", "no_duration_filesize", "-f", "flv",
        $StreamUrls[$Number - 1]
    ) -WindowStyle Hidden -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr -PassThru
    return New-ProcessRecord $process $FfmpegPath ("publisher-{0:D3}" -f $Number)
}

function Start-Application {
    param([object]$State)
    $arguments = @("--decode-threads", "8", "--metrics-file", $MetricsPath)
    foreach ($url in $StreamUrls) { $arguments += @("--url", $url) }
    $process = Start-Process -FilePath $AppPath -ArgumentList $arguments `
        -WorkingDirectory (Split-Path -Parent $AppPath) `
        -RedirectStandardOutput (Join-Path $LogsRoot "application.stdout.log") `
        -RedirectStandardError (Join-Path $LogsRoot "application.stderr.log") `
        -PassThru
    $State.App = New-ProcessRecord $process $AppPath "application"
    Save-State $State
    $process.Refresh()
    if ($process.HasExited) { throw "RtmpMonitor 启动失败。" }
}

function New-TestState {
    return [ordered]@{
        SchemaVersion = 1
        StartedAtUtc = [DateTime]::UtcNow.ToString("O")
        NginxReused = $false
        Nginx = $null
        App = $null
        Publishers = @()
    }
}

function Invoke-Start {
    $startWatch = [Diagnostics.Stopwatch]::StartNew()
    Write-Output "[Start 1/5] 快速检查路径和参数"
    Assert-Urls
    foreach ($required in @(
        @($FfmpegPath, "FFmpeg"),
        @($NginxSourceExe, "nginx"),
        @($AppPath, "RtmpMonitor Debug 程序"),
        @((Join-Path $AssetRoot "manifest.json"), "素材清单（先运行 Prepare）")
    )) {
        Assert-File $required[0] $required[1]
    }
    Assert-File (Join-Path $AssetRoot "manifest.json") "素材清单（先运行 Prepare）"
    if (Test-Path -LiteralPath $StatePath) {
        try {
            $old = Get-State
            $ownedProcesses = @()
            if ($null -ne $old.App) { $ownedProcesses += $old.App }
            if ($null -ne $old.Nginx -and -not $old.NginxReused) {
                $ownedProcesses += $old.Nginx
            }
            $ownedProcesses += @($old.Publishers)
            if (@($ownedProcesses | Where-Object {
                    $null -ne (Get-OwnedProcess $_)
                }).Count -gt 0) {
                throw "pids.json 中仍有本次测试进程在运行，请先 Stop。"
            }
        } catch {
            if ($_.Exception.Message -match "仍有") { throw }
        }
    }

    Write-Stage "启动 nginx、16 个 copy 推流和 RtmpMonitor"
    New-Item -ItemType Directory -Path $OutputRoot,$LogsRoot -Force | Out-Null
    $state = New-TestState
    try {
        Write-Output "[Start 2/5] 后台启动 nginx"
        if ($startWatch.Elapsed.TotalSeconds -ge 8) {
            throw "Start 在 nginx 前已用尽 8 秒内部预算。"
        }
        Start-Nginx $state $startWatch
        Save-State $state
        Write-Output "[Start 3/5] 后台启动 16 个 FFmpeg copy 推流"
        for ($camera = 1; $camera -le 16; $camera++) {
            if ($startWatch.Elapsed.TotalSeconds -ge 8) {
                throw "Start 在 Camera $camera 前已用尽 8 秒内部预算。"
            }
            $state.Publishers += Start-Publisher $camera $state
            Save-State $state
            if (($camera % 4) -eq 0) {
                Write-Output ("  已提交 {0}/16 路；耗时 {1:N1} 秒" -f
                    $camera, $startWatch.Elapsed.TotalSeconds)
            }
        }
        Write-Output "[Start 4/5] 后台启动 RtmpMonitor"
        if ($startWatch.Elapsed.TotalSeconds -ge 8) {
            throw "Start 在应用启动前已用尽 8 秒内部预算。"
        }
        Start-Application $state
        Save-State $state
    } catch {
        Save-State $state
        throw
    }
    if ($startWatch.Elapsed.TotalSeconds -gt 10) {
        throw ("Start 超过 10 秒：{0:N1} 秒。" -f $startWatch.Elapsed.TotalSeconds)
    }
    Write-Output ("[Start 5/5] 已后台提交全部进程，{0:N1} 秒内返回。" -f
        $startWatch.Elapsed.TotalSeconds)
    Write-Output "下一步：-Action Status；完整健康等待请使用 -Action Test。"
}

function Invoke-ActionBroker {
    New-Item -ItemType Directory -Path $OutputRoot,$LogsRoot,$RuntimeRoot `
        -Force | Out-Null
    $launcherSuffix = [Guid]::NewGuid().ToString("N")
    $launcherScriptPath = Join-Path $RuntimeRoot (
        "launcher-{0}-{1}.cmd" -f $Action.ToLowerInvariant(), $launcherSuffix
    )
    $launcherStdout = Join-Path $LogsRoot (
        "launcher-{0}-{1}.stdout.log" -f
        $Action.ToLowerInvariant(), $launcherSuffix
    )
    $launcherStderr = Join-Path $LogsRoot (
        "launcher-{0}-{1}.stderr.log" -f
        $Action.ToLowerInvariant(), $launcherSuffix
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
        "-Action", $Action,
        "-InternalLauncher",
        "-StreamNumber", [string]$StreamNumber,
        "-DurationSeconds", [string]$DurationSeconds,
        "-WarmupSeconds", [string]$WarmupSeconds,
        "-HealthTimeoutSeconds", [string]$HealthTimeoutSeconds,
        "-FfmpegPath", $FfmpegPath,
        "-FfprobePath", $FfprobePath,
        "-NginxRoot", $NginxRoot,
        "-InputFile", $InputFile,
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
    Write-Output "[$Action] 后台提交隐藏启动器；本操作不等待长驻进程。"
    $launcher = Start-Process -FilePath $env:ComSpec `
        -ArgumentList @("/d", "/c", $launcherScriptPath) `
        -WorkingDirectory $ProjectRoot -PassThru -WindowStyle Hidden
    New-ProcessRecord $launcher $env:ComSpec "launcher" |
        ConvertTo-Json |
        Set-Content -LiteralPath $LauncherPidPath -Encoding UTF8
    Write-Output "[$Action] 已返回：launcher PID=$($launcher.Id)"
    Write-Output "下一步：-Action Status；完整健康检查使用 -Action Test。"
}

function Invoke-Status {
    if (Test-Path -LiteralPath $LauncherResultPath) {
        $launcherResult = Get-Content -LiteralPath $LauncherResultPath -Raw |
            ConvertFrom-Json
        Write-Host (
            "启动器：action={0} status={1}，{2}" -f
            $launcherResult.Action,
            $launcherResult.Status,
            $launcherResult.Message
        )
    }
    if (-not (Test-Path -LiteralPath $StatePath)) {
        Write-Host "PID 状态文件尚未生成。"
        return
    }
    $state = Get-State
    Write-Stage "16 路视频验收状态"
    $app = Get-OwnedProcess $state.App
    Write-Host ("应用：{0}" -f $(if ($null -ne $app) {
        "PID=$($app.Id) CPU=$([Math]::Round($app.CPU,1))s WS=$([Math]::Round($app.WorkingSet64/1MB,1))MiB"
    } else { "缺失" }))
    $alive = 0
    foreach ($record in @($state.Publishers)) {
        if ($null -ne (Get-OwnedProcess $record)) { $alive++ }
    }
    Write-Host "推流进程：$alive/16；RTMP 端口：$(if(Test-RtmpPort){'监听'}else{'未监听'})"
    if (Test-Path -LiteralPath $MetricsPath) {
        $metrics = Get-Content $MetricsPath -Raw -Encoding UTF8 | ConvertFrom-Json
        Write-Host "应用指标：流=$($metrics.streamCount)，解码 worker=$($metrics.decodeWorkerCount)，UI 最大间隔=$($metrics.maximumUiTimerGapMs)ms"
        foreach ($stream in $metrics.streams) {
            Write-Host ("  {0,-10} {1,-12} decode={2,5:N1} display={3,5:N1} queue={4,2} age={5,4}ms" -f
                $stream.displayName, $stream.state, $stream.decodeFps,
                $stream.displayFps, $stream.queuePackets, $stream.lastFrameAgeMs)
        }
    } else {
        Write-Warning "指标文件尚未生成：$MetricsPath"
    }
}

function Invoke-Test {
    $state = Get-State
    $effectiveTimeoutSeconds = [Math]::Min($HealthTimeoutSeconds, 55)
    Write-Stage "最多 $effectiveTimeoutSeconds 秒的 16 路健康检查"
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $nextProgress = 0
    while ($watch.Elapsed.TotalSeconds -lt $effectiveTimeoutSeconds) {
        $appAlive = $null -ne (Get-OwnedProcess $state.App)
        $publisherCount = @($state.Publishers | Where-Object {
            $null -ne (Get-OwnedProcess $_)
        }).Count
        $streamCount = 0
        $playingCount = 0
        if (Test-Path -LiteralPath $MetricsPath) {
            try {
                $metrics = Get-Content $MetricsPath -Raw -Encoding UTF8 |
                    ConvertFrom-Json
                $streamCount = [int]$metrics.streamCount
                $playingCount = @($metrics.streams |
                    Where-Object state -eq "playing").Count
            } catch {}
        }
        if ($appAlive -and $publisherCount -eq 16 -and
            (Test-RtmpPort) -and $streamCount -eq 16 -and
            $playingCount -eq 16) {
            Write-Output ("[通过] 16/16 推流和播放健康，耗时 {0:N1} 秒。" -f
                $watch.Elapsed.TotalSeconds)
            return
        }
        if ($watch.Elapsed.TotalSeconds -ge $nextProgress) {
            Write-Output ("[Test] t={0:N0}s app={1} publishers={2}/16 streams={3}/16 playing={4}/16" -f
                $watch.Elapsed.TotalSeconds, $appAlive, $publisherCount,
                $streamCount, $playingCount)
            $nextProgress += 5
        }
        Start-Sleep -Milliseconds 500
    }
    Show-LogTails
    throw "健康检查在 $effectiveTimeoutSeconds 秒内未达到 16/16 playing。"
}

function Invoke-StopStream {
    $state = Get-State
    $record = @($state.Publishers)[$StreamNumber - 1]
    Stop-OwnedProcess $record
    Write-Host "已停止 Camera $("{0:D2}" -f $StreamNumber)。"
}

function Invoke-StartStream {
    $state = Get-State
    $records = @($state.Publishers)
    if ($null -ne (Get-OwnedProcess $records[$StreamNumber - 1])) {
        Write-Host "Camera $StreamNumber 已在推流。"
        return
    }
    $records[$StreamNumber - 1] = Start-Publisher $StreamNumber $state
    $state.Publishers = $records
    Save-State $state
    Write-Host "已恢复 Camera $("{0:D2}" -f $StreamNumber)。"
}

function Invoke-StopStreams {
    $state = Get-State
    foreach ($record in @($state.Publishers)) { Stop-OwnedProcess $record }
    Write-Host "已停止本次测试的全部推流。"
}

function Invoke-StartStreams {
    $state = Get-State
    $records = @($state.Publishers)
    for ($camera = 1; $camera -le 16; $camera++) {
        if ($null -eq (Get-OwnedProcess $records[$camera - 1])) {
            $records[$camera - 1] = Start-Publisher $camera $state
        }
        Write-Output "[StartStreams] 已处理 $camera/16 路"
    }
    $state.Publishers = $records
    Save-State $state
    Write-Host "16 路推流已恢复。"
}

function Invoke-StartApp {
    $state = Get-State
    if ($null -ne (Get-OwnedProcess $state.App)) {
        Write-Host "应用已经运行。"
        return
    }
    Start-Application $state
    Save-State $state
}

function Invoke-RunAutomated {
    $samples = @()
    $passed = $false
    try {
        Invoke-Start
        Write-Stage "预热 $WarmupSeconds 秒"
        Wait-WithProgress -Seconds $WarmupSeconds -Stage "预热"
        $state = Get-State
        $app = Get-OwnedProcess $state.App
        if ($null -eq $app) { throw "预热后应用不存在。" }
        $previousCpu = $app.TotalProcessorTime.TotalSeconds
        $previousAt = Get-Date

        Write-Stage "采样 $DurationSeconds 秒；中途注入 Camera 03 故障"
        for ($second = 1; $second -le $DurationSeconds; $second++) {
            if ($second -eq [Math]::Max(5, [int]($DurationSeconds / 2))) {
                $script:StreamNumber = 3
                Invoke-StopStream
                Wait-WithProgress -Seconds 10 -Stage "Camera 03 故障注入"
                Invoke-StartStream
            }
            Start-Sleep -Seconds 1
            $app.Refresh()
            if ($app.HasExited) { throw "应用在自动测试期间退出。" }
            if (-not (Test-Path -LiteralPath $MetricsPath)) { continue }
            $snapshot = Get-Content $MetricsPath -Raw -Encoding UTF8 | ConvertFrom-Json
            $now = Get-Date
            $cpuNow = $app.TotalProcessorTime.TotalSeconds
            $elapsed = ($now - $previousAt).TotalSeconds
            $cpuPercent = 100 * ($cpuNow - $previousCpu) /
                ([Environment]::ProcessorCount * [Math]::Max($elapsed, 0.001))
            $previousCpu = $cpuNow
            $previousAt = $now
            $samples += [pscustomobject]@{
                AtUtc = [DateTime]::UtcNow.ToString("O")
                Playing = @($snapshot.streams | Where-Object state -eq "playing").Count
                MinimumDecodeFps = [double](($snapshot.streams | Measure-Object decodeFps -Minimum).Minimum)
                MinimumDisplayFps = [double](($snapshot.streams | Measure-Object displayFps -Minimum).Minimum)
                MaximumQueue = [int](($snapshot.streams | Measure-Object queuePackets -Maximum).Maximum)
                CpuPercent = $cpuPercent
                WorkingSetMiB = $app.WorkingSet64 / 1MB
                UiGapMs = [int]$snapshot.maximumUiTimerGapMs
            }
            if (($second % 5) -eq 0) {
                Write-Host ("{0,4}/{1}s playing={2}/16 minDecode={3:N1} CPU={4:N1}% WS={5:N0}MiB" -f
                    $second, $DurationSeconds, $samples[-1].Playing,
                    $samples[-1].MinimumDecodeFps, $cpuPercent,
                    $samples[-1].WorkingSetMiB)
            }
        }

        $steady = @($samples | Select-Object -Skip ([Math]::Min(5, $samples.Count)))
        $decodePassRatio = @($steady | Where-Object MinimumDecodeFps -ge 27).Count /
            [Math]::Max(1, $steady.Count)
        $averageCpu = ($steady | Measure-Object CpuPercent -Average).Average
        $peakMemory = ($steady | Measure-Object WorkingSetMiB -Maximum).Maximum
        $maxUiGap = ($steady | Measure-Object UiGapMs -Maximum).Maximum
        $maxQueue = ($steady | Measure-Object MaximumQueue -Maximum).Maximum
        $passed = $decodePassRatio -ge 0.95 -and $averageCpu -le 85 -and
            $peakMemory -le 2048 -and $maxUiGap -lt 500 -and $maxQueue -le 45

        $report = [ordered]@{
            SchemaVersion = 1
            Scenario = "sixteen-stream-video"
            Passed = $passed
            DurationSeconds = $DurationSeconds
            DecodePassRatio = $decodePassRatio
            AverageApplicationCpuPercent = $averageCpu
            PeakWorkingSetMiB = $peakMemory
            MaximumUiTimerGapMs = $maxUiGap
            MaximumQueuePackets = $maxQueue
            Samples = $samples
        }
        $report | ConvertTo-Json -Depth 6 |
            Set-Content -LiteralPath $ReportJsonPath -Encoding UTF8
        @"
# 16 路预录视频自动验收

- 结果：$(if($passed){"通过"}else{"失败"})
- 时长：$DurationSeconds 秒
- 解码 FPS 达标采样比例：$([Math]::Round($decodePassRatio * 100, 1))%
- 应用平均 CPU：$([Math]::Round($averageCpu, 1))%
- 峰值工作集：$([Math]::Round($peakMemory, 1)) MiB
- UI 最大定时器间隔：$maxUiGap ms
- 最大压缩包队列：$maxQueue/45
"@ | Set-Content -LiteralPath $ReportMarkdownPath -Encoding UTF8
        if (-not $passed) { throw "自动性能门槛未全部通过：$ReportMarkdownPath" }
    } finally {
        try { Invoke-Stop } catch { Write-Warning $_.Exception.Message }
    }
}

function Invoke-Stop {
    if (-not (Test-Path -LiteralPath $StatePath)) {
        Write-Host "没有状态文件；Stop 为幂等操作。"
        return
    }
    $state = Get-State
    Write-Stage "安全清理 16 路视频测试进程"
    if ($null -ne $state.App) {
        Write-Output "[Stop] 正在关闭应用（最长 5 秒）"
        Stop-OwnedProcess $state.App 5
    }
    $publisherRecords = @($state.Publishers)
    for ($index = 0; $index -lt $publisherRecords.Count; ++$index) {
        Stop-OwnedProcess $publisherRecords[$index]
        Write-Output "[Stop] 已处理 $($index + 1)/$($publisherRecords.Count) 路推流"
    }
    if (-not $state.NginxReused -and $null -ne $state.Nginx) {
        Write-Output "[Stop] 正在关闭 nginx"
        Stop-OwnedProcess $state.Nginx
    }
    Write-Output "清理完成；只处理了 pids.json 中通过路径和启动时间核验的进程。"
    Write-Output "日志：$LogsRoot"
    Write-Output "PID：$StatePath"
    Write-Output "报告：$OutputRoot"
}

try {
    $actionDeadline = $null
    if ($Action -in $LongLivedStartActions -and $InternalLauncher) {
        $actionDeadline = [Week4ProcessDeadline]::new(9000, 124)
    } elseif ($Action -eq "Test") {
        $actionDeadline = [Week4ProcessDeadline]::new(59000, 124)
    }
    if ($Action -in $LongLivedStartActions -and -not $InternalLauncher) {
        Invoke-ActionBroker
    } else {
        switch ($Action) {
            "Check" { Invoke-Check }
            "Prepare" { Invoke-Prepare }
            "Start" { Invoke-Start }
            "Status" { Invoke-Status }
            "Test" { Invoke-Test }
            "RunAutomated" { Invoke-RunAutomated }
            "StopStream" { Invoke-StopStream }
            "StartStream" { Invoke-StartStream }
            "StopStreams" { Invoke-StopStreams }
            "StartStreams" { Invoke-StartStreams }
            "StartApp" { Invoke-StartApp }
            "Stop" { Invoke-Stop }
        }
    }
    if ($Action -in $LongLivedStartActions -and $InternalLauncher) {
        Write-LauncherResult "succeeded" "$Action 已完成后台进程提交。"
        $actionDeadline.Dispose()
        [Environment]::Exit(0)
    }
    if ($null -ne $actionDeadline) {
        $actionDeadline.Dispose()
    }
} catch {
    if ($null -ne $actionDeadline) {
        $actionDeadline.Dispose()
    }
    [Console]::Error.WriteLine("失败：{0}", $_.Exception.Message)
    Show-LogTails
    Write-Output "PID 文件：$StatePath"
    Write-Output "日志目录：$LogsRoot"
    Write-Output "清理命令：powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Action Stop"
    if ($InternalLauncher) {
        Write-LauncherResult "failed" $_.Exception.Message
        [Environment]::Exit(1)
    }
    exit 1
}
