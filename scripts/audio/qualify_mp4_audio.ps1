#requires -Version 5.1
<#[
.SYNOPSIS
    Qualify the MP4 -> FFmpeg -> WSL2 SRS -> RtmpMonitor audio path.

.DESCRIPTION
    Downloads the benign official Aliyun Player sample into ignored output storage,
    validates its tracks, safely starts SRS when needed, and runs the real
    FFmpegPlayer/AudioPlaybackEngine/QAudioSink qualification executable.
    Reports measure publisher progress to QAudioSink write; acoustic output is
    deliberately outside this qualification.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Check', 'Download', 'Run', 'RunThree', 'Soak')]
    [string]$Action,

    [string]$Distro = $(if ($env:RTMP_MONITOR_WSL_DISTRO) {
        $env:RTMP_MONITOR_WSL_DISTRO
    } else { 'Ubuntu-22.04-New' }),
    [string]$BuildDir,
    [string]$Ffmpeg,
    [string]$MediaFile,
    [ValidateRange(20, 3600)][int]$DurationSeconds = 320,
    [ValidateRange(0, 300)][int]$WarmupSeconds = 10,
    [ValidateRange(1, 5000)][int]$MinimumSamples = 300
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$QualificationRoot = Join-Path $ProjectRoot 'out\qualification\audio'
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $release = Join-Path $ProjectRoot 'out\build-windows-x64\release'
    $debug = Join-Path $ProjectRoot 'out\build-windows-x64\debug'
    $BuildDir = if (Test-Path (Join-Path $release 'rtmp_monitor_audio_qualification.exe')) {
        $release
    } else { $debug }
}
if ([string]::IsNullOrWhiteSpace($MediaFile)) {
    $MediaFile = Join-Path $QualificationRoot 'media\AliyunMediaSample.mp4'
}
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$MediaFile = [IO.Path]::GetFullPath($MediaFile)
$SrsScript = Join-Path $ProjectRoot 'scripts\srs\srs_dev_wsl.ps1'
$Runner = Join-Path $BuildDir 'rtmp_monitor_audio_qualification.exe'
$StreamUrl = 'rtmp://127.0.0.1:1935/live/audio-qualification'

function Write-Step([string]$Message) {
    Write-Host "[audio-qualification] $Message"
}

function Resolve-Ffmpeg {
    if (-not [string]::IsNullOrWhiteSpace($Ffmpeg)) {
        $resolved = [IO.Path]::GetFullPath($Ffmpeg)
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "FFmpeg not found: $resolved"
        }
        return $resolved
    }
    $command = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) { return $command.Source }
    $known = 'E:\DevTools\ffmpeg-8.1.2-full_build\bin\ffmpeg.exe'
    if (Test-Path -LiteralPath $known) { return $known }
    throw 'FFmpeg not found. Pass -Ffmpeg <path>.'
}

function Resolve-Ffprobe([string]$FfmpegPath) {
    $probe = Join-Path (Split-Path -Parent $FfmpegPath) 'ffprobe.exe'
    if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) {
        throw "ffprobe not found beside FFmpeg: $probe"
    }
    return $probe
}

function Test-TcpPort([int]$Port) {
    $client = New-Object Net.Sockets.TcpClient
    try {
        $task = $client.ConnectAsync('127.0.0.1', $Port)
        return $task.Wait(1500) -and $client.Connected
    } catch { return $false } finally { $client.Dispose() }
}

function Invoke-Srs([string]$SrsAction) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $SrsScript `
        -Action $SrsAction -Distro $Distro | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "SRS action failed: $SrsAction ($LASTEXITCODE)"
    }
}

function Get-MediaInformation([string]$Path, [string]$Probe) {
    $json = & $Probe -v error -show_entries `
        'format=duration:stream=codec_type,codec_name,sample_rate,channels' `
        -of json $Path 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0) { throw "ffprobe rejected media: $json" }
    $info = $json | ConvertFrom-Json
    $duration = [double]$info.format.duration
    $video = @($info.streams | Where-Object { $_.codec_type -eq 'video' })
    $audio = @($info.streams | Where-Object { $_.codec_type -eq 'audio' })
    if ($duration -lt 120 -or $video.Count -lt 1 -or $audio.Count -lt 1) {
        throw "Media must contain normal audio and video and last at least 120 seconds: $Path"
    }
    return [pscustomobject]@{
        durationSeconds = [math]::Round($duration, 3)
        videoCodec = [string]$video[0].codec_name
        audioCodec = [string]$audio[0].codec_name
        audioSampleRate = [int]$audio[0].sample_rate
        audioChannels = [int]$audio[0].channels
    }
}

function Save-TestMedia([string]$Probe) {
    $parent = Split-Path -Parent $MediaFile
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    if (Test-Path -LiteralPath $MediaFile -PathType Leaf) {
        $info = Get-MediaInformation -Path $MediaFile -Probe $Probe
        Write-Step "Using existing qualified media: $MediaFile"
        return $info
    }
    $curl = (Get-Command curl.exe -ErrorAction Stop).Source
    # User requirement: only use a mainland-China source. This is the official
    # sample referenced by Alibaba Cloud's Web Player documentation.
    $sources = @('https://player.alicdn.com/video/aliyunmedia.mp4')
    $partial = "$MediaFile.partial"
    foreach ($source in $sources) {
        Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue
        Write-Step "Downloading the approved Aliyun Player sample from $source"
        & $curl --noproxy '*' -L --fail --show-error --retry 2 `
            --connect-timeout 20 --output $partial $source
        if ($LASTEXITCODE -ne 0 -or
            -not (Test-Path -LiteralPath $partial -PathType Leaf)) {
            continue
        }
        try {
            $info = Get-MediaInformation -Path $partial -Probe $Probe
            Move-Item -LiteralPath $partial -Destination $MediaFile
            return $info
        } catch {
            Write-Step "Downloaded candidate failed media validation: $($_.Exception.Message)"
        }
    }
    Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue
    throw 'Unable to download and validate the approved Aliyun Player sample.'
}

function Use-ContinuousQualificationMedia(
    [string]$SourcePath,
    [string]$FfmpegPath,
    [string]$Probe
) {
    $sourceInfo = Get-MediaInformation -Path $SourcePath -Probe $Probe
    if ($sourceInfo.durationSeconds -ge 600) {
        return [pscustomobject]@{ path = $SourcePath; info = $sourceInfo }
    }

    $qualifiedPath = Join-Path (Split-Path -Parent $SourcePath) `
        'AliyunMediaQualification-720s.mp4'
    if (Test-Path -LiteralPath $qualifiedPath -PathType Leaf) {
        $qualifiedInfo = Get-MediaInformation -Path $qualifiedPath -Probe $Probe
        if ($qualifiedInfo.durationSeconds -ge 600) {
            return [pscustomobject]@{ path = $qualifiedPath; info = $qualifiedInfo }
        }
    }

    Write-Step 'Preparing a 720-second continuous qualification derivative.'
    & $FfmpegPath -hide_banner -loglevel warning -stream_loop 2 `
        -i $SourcePath -map '0:v:0' -map '0:a:0' -t 720 -c copy `
        -avoid_negative_ts make_zero -movflags +faststart -y $qualifiedPath
    if ($LASTEXITCODE -ne 0) {
        throw 'FFmpeg could not prepare the continuous qualification media.'
    }
    $qualifiedInfo = Get-MediaInformation -Path $qualifiedPath -Probe $Probe
    if ($qualifiedInfo.durationSeconds -lt 600) {
        throw 'Continuous qualification media is shorter than 600 seconds.'
    }
    return [pscustomobject]@{ path = $qualifiedPath; info = $qualifiedInfo }
}

function Assert-Environment([switch]$RequireMedia) {
    $ffmpegPath = Resolve-Ffmpeg
    $probe = Resolve-Ffprobe $ffmpegPath
    if (-not (Test-Path -LiteralPath $Runner -PathType Leaf)) {
        throw "Qualification executable not built: $Runner"
    }
    Invoke-Srs 'Check'
    $info = $null
    if ($RequireMedia) {
        $sourceInfo = Save-TestMedia $probe
        $continuous = Use-ContinuousQualificationMedia `
            -SourcePath $MediaFile -FfmpegPath $ffmpegPath -Probe $probe
        $script:MediaFile = $continuous.path
        $info = [pscustomobject]@{
            source = $sourceInfo
            qualification = $continuous.info
        }
    }
    return [pscustomobject]@{
        ffmpeg = $ffmpegPath
        ffprobe = $probe
        media = $info
    }
}

function Invoke-OneRun(
    [string]$FfmpegPath,
    [int]$Index,
    [int]$Duration,
    [int]$Minimum
) {
    $runId = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + "-r$Index"
    $directory = Join-Path $QualificationRoot $runId
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $report = Join-Path $directory 'report.json'
    Write-Step "Run ${Index}: duration=${Duration}s, report=$report"
    & $Runner --ffmpeg $FfmpegPath --input $MediaFile --url $StreamUrl `
        --output $report --duration $Duration --warmup $WarmupSeconds `
        --minimum-samples $Minimum
    $code = $LASTEXITCODE
    if (-not (Test-Path -LiteralPath $report -PathType Leaf)) {
        throw "Qualification runner produced no report (exit $code)."
    }
    $result = Get-Content -LiteralPath $report -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($code -ne 0 -or -not $result.passed) {
        throw "Audio qualification run $Index failed. Report: $report"
    }
    return [pscustomobject]@{
        report = $report
        sampleCount = $result.sampleCount
        p50Ms = $result.p50Ms
        p95Ms = $result.p95Ms
        maximumMs = $result.maximumMs
        passed = [bool]$result.passed
    }
}

function Invoke-Runs([int]$Count, [int]$Duration, [int]$Minimum) {
    $environment = Assert-Environment -RequireMedia
    $startedSrs = $false
    if (-not (Test-TcpPort 1935) -or -not (Test-TcpPort 1985)) {
        Invoke-Srs 'Start'
        $startedSrs = $true
    }
    try {
        $results = @()
        for ($index = 1; $index -le $Count; ++$index) {
            $results += Invoke-OneRun -FfmpegPath $environment.ffmpeg `
                -Index $index -Duration $Duration -Minimum $Minimum
        }
        $summaryPath = Join-Path $QualificationRoot (
            'summary-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '.json')
        [pscustomobject]@{
            schemaVersion = 1
            measurementScope = 'ffmpeg-publisher-progress-to-qaudiosink-write'
            acousticOutputIncluded = $false
            sourceTitle = 'Aliyun Player official sample'
            sourceRegion = 'mainland-China CDN'
            media = $environment.media
            runCount = $Count
            passed = (@($results | Where-Object { -not $_.passed }).Count -eq 0)
            runs = $results
        } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $summaryPath -Encoding UTF8
        Write-Step "QUALIFICATION PASS: $summaryPath"
    } finally {
        if ($startedSrs) { Invoke-Srs 'Stop' }
    }
}

$ffmpegPath = Resolve-Ffmpeg
$probePath = Resolve-Ffprobe $ffmpegPath
switch ($Action) {
    'Check' {
        $result = Assert-Environment -RequireMedia:$false
        Write-Step "FFmpeg: $($result.ffmpeg)"
        Write-Step "Runner: $Runner"
    }
    'Download' {
        $info = Save-TestMedia $probePath
        Write-Step ("MEDIA PASS: " + ($info | ConvertTo-Json -Compress))
    }
    'Run' { Invoke-Runs -Count 1 -Duration $DurationSeconds -Minimum $MinimumSamples }
    'RunThree' { Invoke-Runs -Count 3 -Duration $DurationSeconds -Minimum $MinimumSamples }
    'Soak' { Invoke-Runs -Count 1 -Duration 600 -Minimum 300 }
}
