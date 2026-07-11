<#
.SYNOPSIS
    验证 Windows 上的 FFmpeg + nginx-rtmp 推流链路。

.DESCRIPTION
    该脚本用于完成以下操作：
    1. 检查 ffmpeg、ffplay、ffprobe 是否可用；
    2. 检查 FFmpeg 是否支持 libx264、RTMP 和 FLV；
    3. 检查测试视频的编码、分辨率、帧率和码率；
    4. 检查 nginx-rtmp 模块和 nginx.conf；
    5. 启动或停止 nginx-rtmp；
    6. 使用 FFmpeg 循环推流；
    7. 使用 ffplay 低延迟拉流播放；
    8. 一键打开“推流窗口”和“播放窗口”。

.NOTES
    推荐保存位置：
        E:\rtmpProject\scripts\verify_rtmp_chain.ps1

    默认环境：
        nginx-rtmp: E:\DevTools\nginx-rtmp
        测试视频:   项目根目录\testdata\test.mp4
        RTMP 地址:  rtmp://127.0.0.1:1935/live/camera001

.EXAMPLE
    .\scripts\verify_rtmp_chain.ps1 -Action Check

.EXAMPLE
    .\scripts\verify_rtmp_chain.ps1 -Action StartServer

.EXAMPLE
    .\scripts\verify_rtmp_chain.ps1 -Action Push

.EXAMPLE
    .\scripts\verify_rtmp_chain.ps1 -Action Play

.EXAMPLE
    .\scripts\verify_rtmp_chain.ps1 -Action All

.EXAMPLE
    .\scripts\verify_rtmp_chain.ps1 -Action StopServer

.EXAMPLE
    .\scripts\verify_rtmp_chain.ps1 -Action StopServer -ForceKill
#>

[CmdletBinding()]
param(
    [ValidateSet("Check", "StartServer", "Push", "Play", "All", "StopServer")]
    [string]$Action = "Check",

    # nginx-rtmp 安装根目录。
    [string]$NginxRoot = "E:\DevTools\nginx-rtmp",

    # 测试视频路径。留空时使用项目根目录下的 testdata\test.mp4。
    [string]$InputFile = "",

    # RTMP 推流和拉流地址。
    [string]$StreamUrl = "rtmp://127.0.0.1:1935/live/camera001",

    # StopServer 时，优雅退出失败后是否强制终止 nginx 进程。
    [switch]$ForceKill
)

Set-StrictMode -Version Latest

$ScriptVersion = "2026-07-11-v5"
Write-Host "RTMP 验证脚本版本：$ScriptVersion" -ForegroundColor DarkGray

# 脚本应位于“项目根目录\scripts”中，因此其父目录就是项目根目录。
$ProjectRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($InputFile)) {
    $InputFile = Join-Path $ProjectRoot "testdata\test.mp4"
}

$NginxExe = Join-Path $NginxRoot "sbin\nginx.exe"
$NginxConfigRelativePath = "conf\nginx.conf"
$NginxConfigFullPath = Join-Path $NginxRoot $NginxConfigRelativePath
$HlsDirectory = Join-Path $NginxRoot "temp\hls\live"

function Write-Step {
    param([Parameter(Mandatory)][string]$Message)

    Write-Host ""
    Write-Host "============================================================" -ForegroundColor DarkGray
    Write-Host $Message -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor DarkGray
}

function Assert-CommandExists {
    param([Parameter(Mandatory)][string]$CommandName)

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "找不到命令：$CommandName。请确认它已经安装并加入 PATH。"
    }

    return $command
}

function Assert-FileExists {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description 不存在：$Path"
    }
}

function Invoke-NginxCapture {
    param([Parameter(Mandatory)][string[]]$Arguments)

    # nginx 经常把正常的版本和检查信息写入 stderr。
    # 这里将 stdout 和 stderr 合并，并单独读取退出码，避免把正常输出误判成失败。
    #
    # 重要：
    # 当前 nginx-rtmp 配置中的 hls_path 使用相对路径 temp/hls/live。
    # 该 Windows 构建会按“当前工作目录”解析这个路径，而不一定按 -p 指定的前缀解析。
    # 因此调用 nginx 前必须先切换到 nginx 安装根目录。
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"

    Push-Location $script:NginxRoot

    try {
        $output = & $script:NginxExe @Arguments 2>&1 |
            ForEach-Object { $_.ToString() }

        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
        $ErrorActionPreference = $oldPreference
    }

    return [PSCustomObject]@{
        Output   = $output
        ExitCode = $exitCode
    }
}

function Test-FFmpegEnvironment {
    Write-Step "1. 检查 FFmpeg 命令和 PATH"

    $ffmpegCommand = Assert-CommandExists "ffmpeg"
    $ffplayCommand = Assert-CommandExists "ffplay"
    $ffprobeCommand = Assert-CommandExists "ffprobe"

    Write-Host "[通过] ffmpeg : $($ffmpegCommand.Source)"
    Write-Host "[通过] ffplay : $($ffplayCommand.Source)"
    Write-Host "[通过] ffprobe: $($ffprobeCommand.Source)"

    Write-Step "2. 检查 FFmpeg 推流能力"

    $encoders = (& ffmpeg -hide_banner -encoders 2>&1 | Out-String)
    if ($encoders -notmatch "libx264") {
        throw "当前 FFmpeg 没有检测到 libx264 编码器。"
    }

    $protocols = (& ffmpeg -hide_banner -protocols 2>&1 | Out-String)
    if ($protocols -notmatch "(?m)^\s*rtmp\s*$") {
        throw "当前 FFmpeg 没有检测到 RTMP 协议。"
    }

    $muxers = (& ffmpeg -hide_banner -muxers 2>&1 | Out-String)
    if ($muxers -notmatch "(?m)^\s*E\s+flv\s+") {
        throw "当前 FFmpeg 没有检测到 FLV 封装器。"
    }

    Write-Host "[通过] libx264：可以编码 H.264"
    Write-Host "[通过] RTMP：可以推流和拉流"
    Write-Host "[通过] FLV：可以封装 RTMP 数据"
}

function Show-TestVideoInformation {
    Write-Step "3. 检查测试视频"

    Assert-FileExists -Path $InputFile -Description "测试视频"

    Write-Host "文件：$InputFile"
    Write-Host ""

    & ffprobe `
        -v error `
        -select_streams v:0 `
        -show_entries "stream=codec_name,profile,width,height,pix_fmt,r_frame_rate,avg_frame_rate,bit_rate" `
        -of "default=noprint_wrappers=1" `
        $InputFile

    if ($LASTEXITCODE -ne 0) {
        throw "ffprobe 检查测试视频失败。"
    }
}

function Test-NginxEnvironment {
    Write-Step "4. 检查 nginx-rtmp"

    Assert-FileExists -Path $NginxExe -Description "nginx.exe"
    Assert-FileExists -Path $NginxConfigFullPath -Description "nginx.conf"

    # 当前 nginx.conf 启用了 HLS。目录不存在时，nginx -t 会失败。
    New-Item -ItemType Directory -Path $HlsDirectory -Force | Out-Null

    $versionResult = Invoke-NginxCapture -Arguments @("-V")
    $versionResult.Output | ForEach-Object { Write-Host $_ }

    if ($versionResult.ExitCode -ne 0) {
        throw "nginx -V 执行失败，退出码：$($versionResult.ExitCode)"
    }

    if (($versionResult.Output -join "`n") -notmatch "nginx-rtmp-module") {
        throw "没有在 nginx 编译信息中检测到 nginx-rtmp-module。"
    }

    Write-Host "[通过] 已检测到 nginx-rtmp-module"

    Write-Step "5. 检查 nginx.conf 语法"

    $testResult = Invoke-NginxCapture -Arguments @(
        "-t",
        "-p", "$NginxRoot\",
        "-c", $NginxConfigRelativePath
    )

    $testResult.Output | ForEach-Object { Write-Host $_ }

    if ($testResult.ExitCode -ne 0) {
        throw "nginx 配置检查失败。"
    }

    if (($testResult.Output -join "`n") -notmatch "test is successful") {
        throw "nginx 没有返回配置检查成功信息。"
    }

    Write-Host "[通过] nginx.conf 配置有效"
}

function Test-RtmpPort {
    param(
        [string]$HostName = "127.0.0.1",
        [int]$Port = 1935,
        [int]$TimeoutMilliseconds = 500
    )

    $client = [System.Net.Sockets.TcpClient]::new()

    try {
        $connectTask = $client.ConnectAsync($HostName, $Port)

        if (-not $connectTask.Wait($TimeoutMilliseconds)) {
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

function Start-RtmpServer {
    Test-NginxEnvironment

    Write-Step "6. 启动 nginx-rtmp"

    $existingProcesses = Get-Process nginx -ErrorAction SilentlyContinue

    if ($existingProcesses -and (Test-RtmpPort)) {
        Write-Host "检测到 nginx 已经运行，并且 1935 端口正在监听，本次不重复启动。"
    }
    else {
        if ($existingProcesses) {
            Write-Warning "检测到残留 nginx 进程，但 1935 端口没有监听，先强制清理。"
            $existingProcesses | Stop-Process -Force
            Start-Sleep -Milliseconds 500
        }

        Write-Host "正在后台启动 nginx……"
        Write-Host "启动参数：-p `"$NginxRoot`" -c `"$NginxConfigRelativePath`"" -ForegroundColor DarkGray

        # 使用 .NET ProcessStartInfo，不捕获 stdout/stderr，也不等待常驻进程退出。
        # 这样可以避免 Windows 版 nginx 持有 PowerShell 管道而导致脚本卡住。
        $processStartInfo = [System.Diagnostics.ProcessStartInfo]::new()
        $processStartInfo.FileName = $NginxExe
        $processStartInfo.WorkingDirectory = $NginxRoot
        $processStartInfo.UseShellExecute = $true
        $processStartInfo.CreateNoWindow = $true
        $processStartInfo.Arguments = "-p `"$NginxRoot`" -c `"$NginxConfigRelativePath`""

        [System.Diagnostics.Process]::Start($processStartInfo) | Out-Null
        Write-Host "启动命令已发出，正在等待 1935 端口……"

        $started = $false

        foreach ($attempt in 1..20) {
            if ((Get-Process nginx -ErrorAction SilentlyContinue) -and (Test-RtmpPort)) {
                $started = $true
                break
            }

            Start-Sleep -Milliseconds 500
        }

        if (-not $started) {
            $errorLog = Join-Path $NginxRoot "logs\error.log"
            $tail = if (Test-Path $errorLog) {
                (Get-Content $errorLog -Tail 20 -ErrorAction SilentlyContinue) -join "`n"
            }
            else {
                "错误日志不存在。"
            }

            throw "nginx 在 10 秒内没有监听 1935 端口。错误日志：`n$tail"
        }
    }

    Write-Host "[通过] nginx-rtmp 正在监听 127.0.0.1:1935"

    Get-Process nginx -ErrorAction SilentlyContinue |
        Select-Object Id, ProcessName, CPU, WorkingSet |
        Format-Table -AutoSize
}

function Stop-RtmpServer {
    Write-Step "停止 nginx-rtmp"

    Assert-FileExists -Path $NginxExe -Description "nginx.exe"

    $processes = Get-Process nginx -ErrorAction SilentlyContinue
    if (-not $processes) {
        Write-Host "nginx 当前没有运行。"
        return
    }

    Write-Host "先尝试使用 nginx -s quit 优雅退出……"

    $stopStartInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $stopStartInfo.FileName = $NginxExe
    $stopStartInfo.WorkingDirectory = $NginxRoot
    $stopStartInfo.UseShellExecute = $false
    $stopStartInfo.CreateNoWindow = $true
    $stopStartInfo.Arguments = "-p `"$NginxRoot`" -c `"$NginxConfigRelativePath`" -s quit"

    try {
        $stopProcess = [System.Diagnostics.Process]::Start($stopStartInfo)

        if (-not $stopProcess.WaitForExit(3000)) {
            try { $stopProcess.Kill() } catch {}
        }
    }
    catch {
        Write-Warning "优雅退出命令执行失败：$($_.Exception.Message)"
    }

    foreach ($attempt in 1..6) {
        if (-not (Get-Process nginx -ErrorAction SilentlyContinue)) {
            Write-Host "[通过] nginx 已正常停止"
            return
        }

        Start-Sleep -Milliseconds 500
    }

    $remainingProcesses = Get-Process nginx -ErrorAction SilentlyContinue

    if ($ForceKill) {
        Write-Warning "仍有 nginx 进程，正在强制终止。"
        $remainingProcesses | Stop-Process -Force
        Start-Sleep -Milliseconds 500

        if (Get-Process nginx -ErrorAction SilentlyContinue) {
            throw "强制终止后仍检测到 nginx 进程。"
        }

        Write-Host "[通过] nginx 已被强制终止"
    }
    else {
        Write-Warning "优雅退出失败。该 Windows nginx 构建可能留下孤立进程。"
        Write-Host "执行下面命令可强制停止："
        Write-Host ".\scripts\verify_rtmp_chain.ps1 -Action StopServer -ForceKill"
    }
}

function Start-TestPush {
    Test-FFmpegEnvironment
    Show-TestVideoInformation

    Write-Step "开始 RTMP 推流"

    Write-Host "输入文件：$InputFile"
    Write-Host "推流地址：$StreamUrl"
    Write-Host "按 q 或 Ctrl+C 停止推流。"
    Write-Host ""

    $arguments = @(
        "-re",
        "-stream_loop", "-1",
        "-i", $InputFile,
        "-c:v", "libx264",
        "-preset", "veryfast",
        "-tune", "zerolatency",
        "-vf", "scale=1280:-2",
        "-r", "30",
        "-b:v", "2500k",
        "-maxrate", "2500k",
        "-bufsize", "5000k",
        "-an",
        "-f", "flv",
        $StreamUrl
    )

    & ffmpeg @arguments
}

function Start-TestPlayback {
    Test-FFmpegEnvironment

    Write-Step "开始 RTMP 拉流播放"

    Write-Host "拉流地址：$StreamUrl"
    Write-Host "关闭播放窗口或按 q 停止播放。"
    Write-Host ""

    $arguments = @(
        "-fflags", "nobuffer",
        "-flags", "low_delay",
        "-framedrop",
        $StreamUrl
    )

    & ffplay @arguments
}

function Start-AllWindows {
    Start-RtmpServer

    Write-Step "打开推流窗口和播放窗口"

    $pushArguments = @(
        "-NoExit",
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", "`"$PSCommandPath`"",
        "-Action", "Push",
        "-NginxRoot", "`"$NginxRoot`"",
        "-InputFile", "`"$InputFile`"",
        "-StreamUrl", "`"$StreamUrl`""
    )

    Start-Process -FilePath "powershell.exe" -ArgumentList $pushArguments

    # 给推流端几秒钟时间连接 nginx，然后再启动播放器。
    Start-Sleep -Seconds 3

    $playArguments = @(
        "-NoExit",
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", "`"$PSCommandPath`"",
        "-Action", "Play",
        "-NginxRoot", "`"$NginxRoot`"",
        "-InputFile", "`"$InputFile`"",
        "-StreamUrl", "`"$StreamUrl`""
    )

    Start-Process -FilePath "powershell.exe" -ArgumentList $playArguments

    Write-Host "已打开两个新 PowerShell 窗口："
    Write-Host "1. FFmpeg 推流窗口"
    Write-Host "2. ffplay 播放窗口"
    Write-Host ""
    Write-Host "测试完成后可停止服务器："
    Write-Host ".\scripts\verify_rtmp_chain.ps1 -Action StopServer"
}

try {
    switch ($Action) {
        "Check" {
            Test-FFmpegEnvironment
            Show-TestVideoInformation
            Test-NginxEnvironment

            Write-Step "环境检查完成"
            Write-Host "可继续执行："
            Write-Host ".\scripts\verify_rtmp_chain.ps1 -Action StartServer"
            Write-Host ".\scripts\verify_rtmp_chain.ps1 -Action Push"
            Write-Host ".\scripts\verify_rtmp_chain.ps1 -Action Play"
            Write-Host ""
            Write-Host "一键启动整条测试链路："
            Write-Host ".\scripts\verify_rtmp_chain.ps1 -Action All"
        }

        "StartServer" {
            Start-RtmpServer
        }

        "Push" {
            Start-TestPush
        }

        "Play" {
            Start-TestPlayback
        }

        "All" {
            Start-AllWindows
        }

        "StopServer" {
            Stop-RtmpServer
        }
    }
}
catch {
    Write-Host ""
    Write-Host "[失败] $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
