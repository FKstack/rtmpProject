#requires -Version 5.1
<#
.SYNOPSIS
    Windows camera-to-SRS qualification controller for RtmpMonitor.

.DESCRIPTION
    Actions: Check | Run | RunMatrix | Status | Stop | Analyze | SelfTest
    Formal camera gates are 1280x720 at 30 FPS with 20 seconds warm-up and
    600 seconds sampling. RunMatrix executes 1/4/8 required cases, a 16-stream
    capability case, and optional synthetic 60 FPS probes.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Check', 'Run', 'RunMatrix', 'Status', 'Stop', 'Analyze', 'SelfTest')]
    [string]$Action,

    [ValidateSet(1, 4, 8, 16)][int]$StreamCount = 1,
    [ValidateRange(1, 86400)][int]$DurationSeconds = 600,
    [ValidateRange(0, 300)][int]$WarmupSeconds = 20,
    [ValidateSet(15, 30, 60)][int]$DisplayFps = 30,
    [ValidateSet(30, 60)][int]$SourceFps = 30,
    [ValidateSet('Camera', 'Synthetic')][string]$SourceKind = 'Camera',
    [string]$Device = 'USB2.0 HD UVC WebCam',
    [string]$Distro = $env:RTMP_MONITOR_WSL_DISTRO,
    [string]$Ffmpeg,
    [string]$BuildDir,
    [string]$RunId,
    [switch]$RecordVisualEvidence,
    [switch]$IncludeSynthetic60,
    [string]$InputVideo,
    [string]$LeftRoi,
    [string]$RightRoi,
    [string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$DefaultBuildDir = Join-Path $ProjectRoot 'out\build-windows-x64\release'
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $debugBuild = Join-Path $ProjectRoot 'out\build-windows-x64\debug'
    $BuildDir = if (Test-Path (Join-Path $DefaultBuildDir 'rtmp_monitor.exe')) { $DefaultBuildDir } else { $debugBuild }
}
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$ValidationRoot = Join-Path $ProjectRoot 'out\camera-validation'
$SrsScript = Join-Path $ProjectRoot 'scripts\srs\srs_dev_wsl.ps1'
$GlobalStateFile = Join-Path $ValidationRoot 'active-run.json'

function Write-Step([string]$Message) { Write-Host "[camera-validation] $Message" }

function Resolve-Ffmpeg {
    if (-not [string]::IsNullOrWhiteSpace($Ffmpeg)) {
        $resolved = [IO.Path]::GetFullPath($Ffmpeg)
        if (-not (Test-Path $resolved)) { throw "FFmpeg not found: $resolved" }
        return $resolved
    }
    $command = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) { return $command.Source }
    $known = 'E:\DevTools\ffmpeg-8.1.2-full_build\bin\ffmpeg.exe'
    if (Test-Path $known) { return $known }
    throw 'FFmpeg was not found. Pass -Ffmpeg <path>.'
}

function Test-TcpPort([int]$Port) {
    $client = New-Object Net.Sockets.TcpClient
    try {
        $task = $client.ConnectAsync('127.0.0.1', $Port)
        return $task.Wait(1500) -and $client.Connected
    } catch { return $false } finally { $client.Dispose() }
}

function Get-ToolPaths {
    return [ordered]@{
        Monitor = Join-Path $BuildDir 'rtmp_monitor.exe'
        Source = Join-Path $BuildDir 'rtmp_monitor_camera_source.exe'
        Analyzer = Join-Path $BuildDir 'rtmp_monitor_video_analyzer.exe'
        CoreTest = Join-Path $BuildDir 'rtmp_monitor_camera_validation_core_test.exe'
    }
}

function Invoke-Check([switch]$ThrowOnFailure, [switch]$IgnoreCamera) {
    $ffmpegPath = Resolve-Ffmpeg
    $tools = Get-ToolPaths
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $deviceText = ((& $ffmpegPath -hide_banner -list_devices true -f dshow -i dummy 2>&1) | ForEach-Object { $_.ToString() } | Out-String)
        $modeText = ((& $ffmpegPath -hide_banner -f dshow -list_options true -i "video=$Device" 2>&1) | ForEach-Object { $_.ToString() } | Out-String)
        $encoderText = ((& $ffmpegPath -hide_banner -encoders 2>&1) | ForEach-Object { $_.ToString() } | Out-String)
        $formatText = ((& $ffmpegPath -hide_banner -formats 2>&1) | ForEach-Object { $_.ToString() } | Out-String)
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($ValidationRoot))
    $result = [ordered]@{
        checkedAtUtc = [DateTime]::UtcNow.ToString('o')
        passed = $true
        ffmpegFound = Test-Path $ffmpegPath
        directShowAvailable = $deviceText -match '\(video\)'
        cameraFound = $deviceText -match [regex]::Escape($Device)
        cameraMode720p30 = $modeText -match '1280x720' -and $modeText -match 'fps=30(?:\.0+)?'
        libx264Available = $encoderText -match '\blibx264\b'
        nvencAvailable = $encoderText -match '\bh264_nvenc\b'
        srsRtmpHealthy = Test-TcpPort 1935
        srsApiHealthy = Test-TcpPort 1985
        monitorBuilt = Test-Path $tools.Monitor
        sourceBuilt = Test-Path $tools.Source
        analyzerBuilt = Test-Path $tools.Analyzer
        freeDiskGiB = [math]::Round($drive.AvailableFreeSpace / 1GB, 2)
        desktopCaptureAvailable = $formatText -match '\bgdigrab\b'
    }
    $required = @('ffmpegFound','directShowAvailable','libx264Available','monitorBuilt','sourceBuilt','analyzerBuilt','desktopCaptureAvailable')
    if (-not $IgnoreCamera) { $required += @('cameraFound','cameraMode720p30') }
    foreach ($name in $required) {
        if (-not $result[$name]) { $result.passed = $false }
    }
    $json = [pscustomobject]$result
    if ($ThrowOnFailure -and -not $result.passed) { throw 'Prerequisite check failed.' }
    return $json
}

function Write-JsonAtomic([string]$Path, $Value) {
    $parent = Split-Path -Parent $Path
    if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    $temporary = "$Path.tmp"
    $Value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Read-Json([string]$Path) {
    if (-not (Test-Path $Path)) { return $null }
    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

function Get-OwnedProcess($Record) {
    if ($null -eq $Record -or $Record.pid -le 0) { return $null }
    $process = Get-Process -Id ([int]$Record.pid) -ErrorAction SilentlyContinue
    if ($null -eq $process) { return $null }
    try {
        $actualPath = [IO.Path]::GetFullPath($process.Path)
        $expectedPath = [IO.Path]::GetFullPath([string]$Record.path)
        $actualStart = $process.StartTime.ToUniversalTime()
        $expectedStart = [DateTime]::Parse([string]$Record.startedAtUtc).ToUniversalTime()
        if (-not $actualPath.Equals($expectedPath, [StringComparison]::OrdinalIgnoreCase)) { return $null }
        if ([math]::Abs(($actualStart - $expectedStart).TotalSeconds) -gt 2) { return $null }
        return $process
    } catch { return $null }
}

function Stop-OwnedProcesses($State) {
    if ($null -eq $State) { return }
    foreach ($name in @('recorder','monitor','source')) {
        $property = $State.PSObject.Properties[$name]
        if ($null -eq $property) { continue }
        $process = Get-OwnedProcess $property.Value
        if ($null -ne $process) {
            Write-Step "Stopping owned $name process $($process.Id)."
            $process.CloseMainWindow() | Out-Null
            if (-not $process.WaitForExit(4000)) {
                Stop-Process -Id $process.Id -Force
            }
        }
    }
    if ($State.srsStartedByRun -eq $true) {
        $stopOutput = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $SrsScript -Action Stop -Distro $Distro)
        foreach ($line in $stopOutput) { Write-Host $line }
    }
}

function Convert-ProcessRecord($Process, [string]$Path) {
    $Process.Refresh()
    return [ordered]@{ pid = $Process.Id; path = [IO.Path]::GetFullPath($Path); startedAtUtc = $Process.StartTime.ToUniversalTime().ToString('o') }
}

function Get-Percentile([double[]]$Values, [double]$Fraction) {
    if ($null -eq $Values -or $Values.Count -eq 0) { return -1 }
    $sorted = @($Values | Sort-Object)
    $index = [math]::Max(0, [math]::Min($sorted.Count - 1, [math]::Ceiling($Fraction * $sorted.Count) - 1))
    return [double]$sorted[$index]
}

function New-Report([string]$Directory, [int]$Count, [int]$Duration, [int]$Warmup, [string]$Kind, [int]$Fps) {
    $sourceSamples = @()
    $sourceFile = Join-Path $Directory 'source-metrics.jsonl'
    if (Test-Path $sourceFile) {
        $sourceSamples = @(Get-Content $sourceFile | ForEach-Object { if (-not [string]::IsNullOrWhiteSpace($_)) { $_ | ConvertFrom-Json } })
        if ($sourceSamples.Count -gt $Warmup) { $sourceSamples = @($sourceSamples | Select-Object -Skip $Warmup -First $Duration) }
    }
    $clientSamples = @()
    $clientFile = Join-Path $Directory 'client-metrics.jsonl'
    if (Test-Path $clientFile) {
        $clientSamples = @(Get-Content $clientFile | ForEach-Object { if (-not [string]::IsNullOrWhiteSpace($_)) { $_ | ConvertFrom-Json } })
    }
    $sourceCaptureFps = @($sourceSamples | ForEach-Object { [double]$_.captureFps })
    $sourcePublishFps = @($sourceSamples | ForEach-Object { [double]$_.publishFps })
    $streamResults = @()
    for ($streamIndex = 0; $streamIndex -lt $Count; $streamIndex++) {
        $perSecond = @($clientSamples | Where-Object { $_.streams.Count -gt $streamIndex } | ForEach-Object { $_.streams[$streamIndex] })
        $decode = @($perSecond | ForEach-Object { [double]$_.decodeFps })
        $display = @($perSecond | ForEach-Object { [double]$_.displayFps })
        $stableDisplay = @($display | Where-Object { $_ -ge 27 }).Count
        $displayAverage = if ($display.Count) { ($display | Measure-Object -Average).Average } else { 0 }
        $decodeAverage = if ($decode.Count) { ($decode | Measure-Object -Average).Average } else { 0 }
        $latest = if ($perSecond.Count) { $perSecond[-1] } else { $null }
        $queueGrowth = if ($perSecond.Count -ge 2) { [int]$perSecond[-1].queuePackets - [int]$perSecond[0].queuePackets } else { 0 }
        $markerRecognition = if ($null -ne $latest -and ($latest.markerDecodedFrames + $latest.markerDecodeFailures) -gt 0) { [double]$latest.markerDecodedFrames / ($latest.markerDecodedFrames + $latest.markerDecodeFailures) } else { 0 }
        $sequenceGapGrowth = if ($perSecond.Count -ge 2) { [int64]$perSecond[-1].sourceSequenceGaps - [int64]$perSecond[0].sourceSequenceGaps } else { 0 }
        $streamPassed = $null -ne $latest -and $decodeAverage -ge 29 -and $displayAverage -ge 28 -and
            ($display.Count -gt 0 -and $stableDisplay / $display.Count -ge 0.95) -and
            ($decodeAverage -le 0 -or $displayAverage / $decodeAverage -ge 0.93) -and
            $latest.presentationIntervalP95Ms -le 50 -and $latest.presentationIntervalMaxMs -le 200 -and
            $latest.sourceLatencyP50Ms -ge 0 -and $latest.sourceLatencyP50Ms -le 180 -and
            $latest.sourceLatencyP95Ms -le 250 -and $latest.sourceLatencyMaxMs -le 500 -and
            $latest.internalLatencyP95Ms -le 60 -and $latest.reconnectCount -eq 0 -and $latest.unsupportedFrames -eq 0 -and
            $markerRecognition -ge 0.90 -and $sequenceGapGrowth -eq 0 -and $queueGrowth -le 5
        $streamResults += [ordered]@{
            stream = $streamIndex + 1; passed = [bool]$streamPassed
            decodeAverageFps = [math]::Round($decodeAverage, 3)
            displayAverageFps = [math]::Round($displayAverage, 3)
            steadySecondsAtLeast27Ratio = if ($display.Count) { [math]::Round($stableDisplay / $display.Count, 4) } else { 0 }
            displayDecodeRetention = if ($decodeAverage -gt 0) { [math]::Round($displayAverage / $decodeAverage, 4) } else { 0 }
            markerRecognitionRate = [math]::Round($markerRecognition, 4)
            sourceSequenceGapsDuringSample = $sequenceGapGrowth
            packetQueueGrowth = $queueGrowth
            latest = $latest
        }
    }
    $sourceCaptureAverage = if ($sourceCaptureFps.Count) { ($sourceCaptureFps | Measure-Object -Average).Average } else { 0 }
    $sourcePublishAverage = if ($sourcePublishFps.Count) { ($sourcePublishFps | Measure-Object -Average).Average } else { 0 }
    $sourceBackpressureDrops = if ($sourceSamples.Count -ge 2) { [int64]$sourceSamples[-1].backpressureDrops - [int64]$sourceSamples[0].backpressureDrops } else { 0 }
    $sourceCaptureDrops = if ($sourceSamples.Count -ge 2) { [int64]$sourceSamples[-1].captureDroppedFrames - [int64]$sourceSamples[0].captureDroppedFrames } else { 0 }
    $latestClient = if ($clientSamples.Count) { $clientSamples[-1] } else { $null }
    $sourcePassed = $sourceCaptureAverage -ge ($Fps - 1) -and $sourcePublishAverage -ge ($Fps - 1)
    $rendererPassed = $null -ne $latestClient -and $latestClient.renderer.activeBackend -eq 'opengl' -and -not $latestClient.renderer.fallbackOccurred -and
        $latestClient.renderStatistics.paintCpuP95Us -le 33333 -and $latestClient.renderStatistics.uploadCpuP95Us -le 33333 -and
        ($latestClient.renderStatistics.gpuTimeP95Us -lt 0 -or $latestClient.renderStatistics.gpuTimeP95Us -le 33333)
    $formal = $Kind -eq 'Camera' -and $Fps -eq 30 -and $Duration -ge 600 -and $Warmup -ge 20 -and $Count -in @(1,4,8)
    $passed = $sourcePassed -and $rendererPassed -and @($streamResults | Where-Object { -not $_.passed }).Count -eq 0
    $diagnosis = if (-not $sourcePassed -or $sourceBackpressureDrops -gt 0 -or $sourceCaptureDrops -gt 0) { 'camera-directshow-encoder-or-publish-backpressure' }
        elseif (@($streamResults | Where-Object { $_.decodeAverageFps -lt 29 }).Count) { 'rtmp-demux-decode-or-worker-pool' }
        elseif (@($streamResults | Where-Object { $_.displayDecodeRetention -lt 0.93 }).Count) { 'display-scheduler-or-mailbox-overwrite' }
        elseif (-not $rendererPassed) { 'renderer-fallback-or-backend' }
        else { 'none-detected' }
    $report = [ordered]@{
        schemaVersion = 1; generatedAtUtc = [DateTime]::UtcNow.ToString('o')
        runId = Split-Path -Leaf $Directory; testKind = $Kind; streamCount = $Count
        sourceFps = $Fps; warmupSeconds = $Warmup; sampledSeconds = $clientSamples.Count
        formalGateEligible = $formal; passed = [bool]$passed
        source = [ordered]@{ captureAverageFps = [math]::Round($sourceCaptureAverage,3); publishAverageFps = [math]::Round($sourcePublishAverage,3); captureDropsDuringSample = $sourceCaptureDrops; backpressureDropsDuringSample = $sourceBackpressureDrops; passed = $sourcePassed }
        renderer = if ($null -ne $latestClient) { $latestClient.renderer } else { $null }
        streams = $streamResults; bottleneck = $diagnosis
        note = if ($formal) { 'Formal Windows camera qualification result.' } else { 'Diagnostic/capability result; not a formal 1/4/8 600-second camera gate.' }
    }
    Write-JsonAtomic (Join-Path $Directory 'report.json') $report
    $lines = @("# Camera validation report", "", "- Run: $($report.runId)", "- Streams: $Count", "- Source: ${Kind} ${Fps} FPS", "- Formal gate eligible: $formal", "- Passed: $passed", "- Bottleneck: $diagnosis", "", "| Stream | Decode FPS | Display FPS | >=27 FPS ratio | Retention | Passed |", "|---:|---:|---:|---:|---:|:---:|")
    foreach ($stream in $streamResults) { $lines += "| $($stream.stream) | $($stream.decodeAverageFps) | $($stream.displayAverageFps) | $($stream.steadySecondsAtLeast27Ratio) | $($stream.displayDecodeRetention) | $($stream.passed) |" }
    $lines | Set-Content -LiteralPath (Join-Path $Directory 'report.md') -Encoding UTF8
    return [pscustomobject]$report
}

function Start-SrsIfNeeded {
    if ((Test-TcpPort 1935) -and (Test-TcpPort 1985)) { return $false }
    if ([string]::IsNullOrWhiteSpace($Distro)) { throw 'SRS is not running and -Distro was not provided.' }
    $startOutput = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $SrsScript -Action Start -Distro $Distro)
    $startExitCode = $LASTEXITCODE
    foreach ($line in $startOutput) { Write-Host $line }
    if ($startExitCode -ne 0 -or -not (Test-TcpPort 1935)) { throw 'SRS failed to start.' }
    return $true
}

function Arrange-ValidationWindows($State, [string]$Directory) {
    Add-Type -AssemblyName System.Windows.Forms
    if ($null -eq ('CameraValidationWindows' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class CameraValidationWindows {
  [DllImport("user32.dll", SetLastError=true)] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hgt, bool repaint);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  public struct RECT { public int Left, Top, Right, Bottom; }
  public struct POINT { public int X, Y; }
}
'@
    }
    Start-Sleep -Seconds 2
    $monitor = Get-OwnedProcess $State.monitor
    $source = Get-OwnedProcess $State.source
    if ($null -eq $monitor -or $null -eq $source -or $monitor.MainWindowHandle -eq 0 -or $source.MainWindowHandle -eq 0) { throw 'Validation windows are not ready.' }
    $screen = [Windows.Forms.Screen]::PrimaryScreen.Bounds
    $half = [int]($screen.Width / 2)
    [CameraValidationWindows]::MoveWindow($monitor.MainWindowHandle, 0, 0, $half, [int]($half * 9 / 16), $true) | Out-Null
    [CameraValidationWindows]::MoveWindow($source.MainWindowHandle, $half, 0, $half, [int]($half * 9 / 16), $true) | Out-Null
    Start-Sleep -Seconds 1
    function Get-ClientRoi($Process) {
        $rect = New-Object CameraValidationWindows+RECT
        $point = New-Object CameraValidationWindows+POINT
        [CameraValidationWindows]::GetClientRect($Process.MainWindowHandle, [ref]$rect) | Out-Null
        [CameraValidationWindows]::ClientToScreen($Process.MainWindowHandle, [ref]$point) | Out-Null
        return [ordered]@{ x=$point.X; y=$point.Y; width=$rect.Right; height=$rect.Bottom }
    }
    $rois = [ordered]@{ left = Get-ClientRoi $monitor; right = Get-ClientRoi $source; coordinateSpace = 'desktop'; accurateForValidationLayout = $true }
    Write-JsonAtomic (Join-Path $Directory 'rois.json') $rois
    Write-Step 'Windows arranged: RtmpMonitor on the left, marked camera reference on the right.'
    return $rois
}

function Start-Recording($State, [string]$Directory, [string]$FfmpegPath) {
    $null = Arrange-ValidationWindows $State $Directory
    $video = Join-Path $Directory 'comparison.mp4'
    $args = "-hide_banner -nostdin -y -f gdigrab -framerate 60 -draw_mouse 0 -i desktop -an -c:v h264_nvenc -preset p1 -tune ull -bf 0 -g 60 `"$video`""
    $process = Start-Process -FilePath $FfmpegPath -ArgumentList $args -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $Directory 'logs\recorder.stdout.log') -RedirectStandardError (Join-Path $Directory 'logs\recorder.stderr.log')
    return Convert-ProcessRecord $process $FfmpegPath
}

function Invoke-Run {
    $check = Invoke-Check -ThrowOnFailure -IgnoreCamera:($SourceKind -eq 'Synthetic')
    $ffmpegPath = Resolve-Ffmpeg
    $tools = Get-ToolPaths
    if ([string]::IsNullOrWhiteSpace($RunId)) { $script:RunId = [DateTime]::Now.ToString('yyyyMMdd-HHmmss') + "-${SourceKind.ToLower()}-${StreamCount}x${SourceFps}" }
    $directory = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { Join-Path $ValidationRoot $RunId } else { [IO.Path]::GetFullPath($OutputDirectory) }
    if (Test-Path $directory) { throw "Output directory already exists: $directory" }
    foreach ($child in @('', 'logs', 'screenshots')) { New-Item -ItemType Directory -Path (Join-Path $directory $child) -Force | Out-Null }
    $urls = @(1..$StreamCount | ForEach-Object { "rtmp://127.0.0.1/live/camera-validation-$RunId-$_" })
    $redacted = [ordered]@{ schemaVersion=1; runId=$RunId; streamCount=$StreamCount; sourceKind=$SourceKind; width=1280; height=720; sourceFps=$SourceFps; displayFps=$DisplayFps; warmupSeconds=$WarmupSeconds; durationSeconds=$DurationSeconds; deviceAlias=if($SourceKind -eq 'Camera'){'configured-directshow-camera'}else{'synthetic-testsrc2'}; streamKeys=@(1..$StreamCount | ForEach-Object { "camera-validation-<run-id>-$_" }) }
    Write-JsonAtomic (Join-Path $directory 'resolved-config.redacted.json') $redacted
    $state = [ordered]@{ schemaVersion=1; runId=$RunId; directory=$directory; status='starting'; srsStartedByRun=$false; createdAtUtc=[DateTime]::UtcNow.ToString('o') }
    $sourceProcess = $null
    $monitorProcess = $null
    try {
        $state.srsStartedByRun = Start-SrsIfNeeded
        $sourceArguments = @('--ffmpeg', "`"$ffmpegPath`"", '--device', "`"$Device`"", '--width','1280','--height','720','--fps',"$SourceFps",'--metrics-file',"`"$(Join-Path $directory 'source-metrics.jsonl')`"")
        if ($SourceKind -eq 'Synthetic') { $sourceArguments += '--synthetic' }
        foreach ($url in $urls) { $sourceArguments += @('--url', $url) }
        $sourceProcess = Start-Process -FilePath $tools.Source -ArgumentList ($sourceArguments -join ' ') -PassThru -RedirectStandardOutput (Join-Path $directory 'logs\source.stdout.log') -RedirectStandardError (Join-Path $directory 'logs\source.stderr.log')
        $state.source = Convert-ProcessRecord $sourceProcess $tools.Source
        $monitorArguments = @('--renderer=auto', "--display-fps=$DisplayFps", '--latency-marker', '--validation-layout', '--no-camera-autostart', "--metrics-file=`"$(Join-Path $directory 'client-current.json')`"", '--max-reconnect-failures=1')
        foreach ($url in $urls) { $monitorArguments += "--url=$url" }
        $monitorProcess = Start-Process -FilePath $tools.Monitor -ArgumentList ($monitorArguments -join ' ') -PassThru -RedirectStandardOutput (Join-Path $directory 'logs\client.stdout.log') -RedirectStandardError (Join-Path $directory 'logs\client.stderr.log')
        $state.monitor = Convert-ProcessRecord $monitorProcess $tools.Monitor
        $state.status = 'warming-up'
        Write-JsonAtomic (Join-Path $directory 'state.json') $state
        Write-JsonAtomic $GlobalStateFile $state
        $null = Arrange-ValidationWindows $state $directory
        if ($RecordVisualEvidence) { $state.recorder = Start-Recording $state $directory $ffmpegPath; Write-JsonAtomic $GlobalStateFile $state }
        Write-Step "Warm-up: $WarmupSeconds seconds."
        for ($second=0; $second -lt $WarmupSeconds; $second++) {
            Start-Sleep -Seconds 1
            if ($null -eq (Get-OwnedProcess $state.source) -or $null -eq (Get-OwnedProcess $state.monitor)) { throw 'Source or monitor exited during warm-up.' }
        }
        $state.status = 'sampling'; Write-JsonAtomic (Join-Path $directory 'state.json') $state; Write-JsonAtomic $GlobalStateFile $state
        $clientJsonl = Join-Path $directory 'client-metrics.jsonl'
        for ($second=0; $second -lt $DurationSeconds; $second++) {
            Start-Sleep -Seconds 1
            if ($null -eq (Get-OwnedProcess $state.source) -or $null -eq (Get-OwnedProcess $state.monitor)) { throw 'Source or monitor exited during sampling.' }
            $current = Join-Path $directory 'client-current.json'
            if (Test-Path $current) {
                try { (Get-Content $current -Raw | ConvertFrom-Json | ConvertTo-Json -Depth 12 -Compress) | Add-Content -LiteralPath $clientJsonl -Encoding UTF8 } catch { Write-Step "Metric sample $second was incomplete and skipped." }
            }
            if (($second + 1) % 30 -eq 0) { Write-Step "Sampled $($second + 1)/$DurationSeconds seconds." }
        }
        $report = New-Report $directory $StreamCount $DurationSeconds $WarmupSeconds $SourceKind $SourceFps
        $state.status = 'completed'; $state.completedAtUtc=[DateTime]::UtcNow.ToString('o'); $state.passed=$report.passed
        Write-JsonAtomic (Join-Path $directory 'state.json') $state
        return $report
    } catch {
        $state.status = 'failed'; $state.failure = $_.Exception.Message; $state.completedAtUtc=[DateTime]::UtcNow.ToString('o')
        if ($null -ne $sourceProcess) {
            $sourceProcess.Refresh()
            if ($sourceProcess.HasExited) { $state.sourceExitCode = $sourceProcess.ExitCode }
        }
        if ($null -ne $monitorProcess) {
            $monitorProcess.Refresh()
            if ($monitorProcess.HasExited) { $state.monitorExitCode = $monitorProcess.ExitCode }
        }
        Write-JsonAtomic (Join-Path $directory 'state.json') $state
        throw
    } finally {
        Stop-OwnedProcesses ([pscustomobject]$state)
        if (Test-Path $GlobalStateFile) { Remove-Item -LiteralPath $GlobalStateFile -Force }
    }
}

function Invoke-Matrix {
    $matrixId = if ([string]::IsNullOrWhiteSpace($RunId)) { [DateTime]::Now.ToString('yyyyMMdd-HHmmss') + '-matrix' } else { $RunId }
    $matrixDirectory = Join-Path $ValidationRoot $matrixId
    New-Item -ItemType Directory -Path $matrixDirectory -Force | Out-Null
    $results = @()
    foreach ($count in @(1,4,8)) {
        $child = Join-Path $matrixDirectory "camera-${count}x30-600s"
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Action Run -StreamCount $count -DurationSeconds 600 -WarmupSeconds 20 -DisplayFps 30 -SourceFps 30 -SourceKind Camera -Device $Device -Distro $Distro -Ffmpeg (Resolve-Ffmpeg) -BuildDir $BuildDir -RunId "$matrixId-camera-$count" -OutputDirectory $child
        if ($LASTEXITCODE -ne 0) { throw "Required ${count}-stream case failed to execute." }
        $results += Read-Json (Join-Path $child 'report.json')
    }
    $capability = Join-Path $matrixDirectory 'camera-16x30-120s'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Action Run -StreamCount 16 -DurationSeconds 120 -WarmupSeconds 20 -DisplayFps 30 -SourceFps 30 -SourceKind Camera -Device $Device -Distro $Distro -Ffmpeg (Resolve-Ffmpeg) -BuildDir $BuildDir -RunId "$matrixId-camera-16-short" -OutputDirectory $capability
    $short16 = Read-Json (Join-Path $capability 'report.json'); $results += $short16
    if ($null -ne $short16 -and $short16.passed) {
        $long16 = Join-Path $matrixDirectory 'camera-16x30-600s'
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Action Run -StreamCount 16 -DurationSeconds 600 -WarmupSeconds 20 -DisplayFps 30 -SourceFps 30 -SourceKind Camera -Device $Device -Distro $Distro -Ffmpeg (Resolve-Ffmpeg) -BuildDir $BuildDir -RunId "$matrixId-camera-16-long" -OutputDirectory $long16
        $results += Read-Json (Join-Path $long16 'report.json')
    }
    $recommended60 = 0
    if ($IncludeSynthetic60) {
        foreach ($count in @(1,4,8,16)) {
            $child = Join-Path $matrixDirectory "synthetic-${count}x60-120s"
            & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Action Run -StreamCount $count -DurationSeconds 120 -WarmupSeconds 20 -DisplayFps 60 -SourceFps 60 -SourceKind Synthetic -Distro $Distro -Ffmpeg (Resolve-Ffmpeg) -BuildDir $BuildDir -RunId "$matrixId-synthetic-$count" -OutputDirectory $child
            $probe = Read-Json (Join-Path $child 'report.json'); $results += $probe
            if ($null -ne $probe -and $probe.passed) { $recommended60 = $count }
        }
    }
    $summary = [ordered]@{ schemaVersion=1; matrixId=$matrixId; generatedAtUtc=[DateTime]::UtcNow.ToString('o'); requiredGatePassed=@($results | Where-Object { $_.testKind -eq 'Camera' -and $_.streamCount -in @(1,4,8) -and $_.sampledSeconds -ge 590 -and -not $_.passed }).Count -eq 0; recommendedMaxStreamsAt60=$recommended60; results=$results }
    Write-JsonAtomic (Join-Path $matrixDirectory 'report.json') $summary
    $summary | ConvertTo-Json -Depth 8
}

function Invoke-Analyze {
    if ([string]::IsNullOrWhiteSpace($InputVideo)) { throw '-InputVideo is required for Analyze.' }
    $tools = Get-ToolPaths
    if (-not (Test-Path $tools.Analyzer)) { throw "Analyzer not built: $($tools.Analyzer)" }
    if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $script:OutputDirectory = Join-Path $ValidationRoot ([DateTime]::Now.ToString('yyyyMMdd-HHmmss') + '-analysis') }
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    if ([string]::IsNullOrWhiteSpace($LeftRoi) -or [string]::IsNullOrWhiteSpace($RightRoi)) {
        $previousPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            $probe = ((& (Resolve-Ffmpeg) -hide_banner -i $InputVideo 2>&1) | ForEach-Object { $_.ToString() } | Out-String)
        } finally { $ErrorActionPreference = $previousPreference }
        if ($probe -notmatch '(\d{3,5})x(\d{3,5})') { throw 'Could not infer video dimensions; provide both ROIs.' }
        $width=[int]$Matches[1]; $height=[int]$Matches[2]; $script:LeftRoi="0,0,$([int]($width/2)),$height"; $script:RightRoi="$([int]($width/2)),0,$([int]($width/2)),$height"
    }
    $output = Join-Path $OutputDirectory 'video-analysis.json'
    & $tools.Analyzer --input $InputVideo --output $output --left-roi $LeftRoi --right-roi $RightRoi
    if ($LASTEXITCODE -ne 0) { throw 'Video analyzer failed.' }
    Get-Content $output -Raw
}

switch ($Action) {
    'Check' { Invoke-Check | ConvertTo-Json -Depth 4; break }
    'Run' { Invoke-Run | ConvertTo-Json -Depth 8; break }
    'RunMatrix' { Invoke-Matrix; break }
    'Status' {
        $state = Read-Json $GlobalStateFile
        if ($null -eq $state) { [pscustomobject]@{ active=$false } | ConvertTo-Json; break }
        [pscustomobject]@{ active=$true; runId=$state.runId; status=$state.status; sourceRunning=$null -ne (Get-OwnedProcess $state.source); monitorRunning=$null -ne (Get-OwnedProcess $state.monitor) } | ConvertTo-Json
        break
    }
    'Stop' {
        $state = Read-Json $GlobalStateFile
        if ($null -eq $state) { Write-Step 'No active owned run.'; break }
        Stop-OwnedProcesses $state
        if (Test-Path $GlobalStateFile) { Remove-Item -LiteralPath $GlobalStateFile -Force }
        break
    }
    'Analyze' { Invoke-Analyze; break }
    'SelfTest' {
        $tools = Get-ToolPaths
        Invoke-Check -ThrowOnFailure | Out-Null
        if (-not (Test-Path $tools.CoreTest)) { throw "Core test not built: $($tools.CoreTest)" }
        $env:RTMP_MONITOR_TEST_FFMPEG = Resolve-Ffmpeg
        $cachePath = Join-Path $BuildDir 'CMakeCache.txt'
        if (Test-Path $cachePath) {
            $qtLine = Get-Content -LiteralPath $cachePath | Where-Object { $_ -match '^Qt6_DIR:PATH=(.+)$' } | Select-Object -First 1
            if ($qtLine -match '^Qt6_DIR:PATH=(.+)$') {
                $qtRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $Matches[1]))
                $qtBin = Join-Path $qtRoot 'bin'
                if (Test-Path $qtBin) { $env:PATH = "$BuildDir;$qtBin;$env:PATH" }
            }
        }
        & $tools.CoreTest -o '-',txt
        if ($LASTEXITCODE -ne 0) { throw 'Camera validation core tests failed.' }
        Write-Step 'Self-test passed.'
        break
    }
}
