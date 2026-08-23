[CmdletBinding()]
param(
    [ValidateSet('Check', 'Run', 'Status', 'Stop', 'SelfTest')]
    [string]$Action = 'Check',
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$QtRoot = $env:QTDIR,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$VsDevCmd,
    [string]$BuildRoot,
    [int]$TimeoutSeconds = 75
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'QualificationCommon.psm1') -Force

$script:SourceRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..')
)
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $script:SourceRoot `
        'out\build-windows-x64\week5'
}
$script:BuildRoot = [System.IO.Path]::GetFullPath($BuildRoot)
$script:RuntimeRoot = Join-Path $script:SourceRoot 'out\webrtc-week5'
$script:StatePath = Join-Path $script:RuntimeRoot 'qualification-state.json'
$script:LogRoot = Join-Path $script:RuntimeRoot 'logs'
$script:AssetPath = Join-Path $script:RuntimeRoot `
    'webrtc-assets\sample.mp4'
$script:AudioOnlyPath = Join-Path $script:RuntimeRoot `
    'fixtures\audio-only.mp4'
$script:NonH264Path = Join-Path $script:RuntimeRoot `
    'fixtures\non-h264.mp4'
$script:BFramesPath = Join-Path $script:RuntimeRoot `
    'fixtures\h264-bframes.mp4'
$script:ExchangeRoot = Join-Path $script:SourceRoot `
    'out\webrtc-p2p\session-exchange'

function Assert-Prerequisites {
    [void](Assert-QualificationConcretePath -Value $QtRoot -Name 'QtRoot')
    [void](Assert-QualificationConcretePath -Value $VcpkgRoot -Name 'VcpkgRoot')
    if (-not [string]::IsNullOrWhiteSpace($VsDevCmd)) {
        [void](Assert-QualificationConcretePath -Value $VsDevCmd `
            -Name 'VsDevCmd')
    }
    if (
        -not (Test-Path -LiteralPath `
            (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake') `
            -PathType Leaf)) {
        throw 'Pass -QtRoot or set QTDIR to the Qt 6.6.1 MSVC kit.'
    }
    if ([string]::IsNullOrWhiteSpace($VcpkgRoot) -or
        -not (Test-Path -LiteralPath `
            (Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake') `
            -PathType Leaf)) {
        throw 'Pass -VcpkgRoot or set VCPKG_ROOT.'
    }
    if (Test-Path -LiteralPath $script:StatePath -PathType Leaf) {
        throw 'A Week 5 qualification state exists. Run -Action Status or Stop.'
    }
    $tools = Resolve-QualificationTools -VsDevCmd $VsDevCmd
    Write-Host 'Week 5 prerequisites passed.'
    return $tools
}

function Start-OwnedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)]$Records
    )
    return Start-QualificationOwnedProcess -Name $Name `
        -FilePath $FilePath -Arguments $Arguments `
        -WorkingDirectory $script:SourceRoot -LogRoot $script:LogRoot `
        -RuntimeRoot $script:RuntimeRoot -StatePath $script:StatePath `
        -Records $Records
}

function Stop-OwnedProcesses {
    Stop-QualificationOwnedProcesses -StatePath $script:StatePath `
        -RuntimeRoot $script:RuntimeRoot
}

function New-QualificationAssets {
    param([Parameter(Mandatory = $true)]$Tools)
    New-QualificationH264Fixtures -Tools $Tools `
        -AssetPath $script:AssetPath -AudioOnlyPath $script:AudioOnlyPath `
        -NonH264Path $script:NonH264Path -BFramesPath $script:BFramesPath
}

function Invoke-BuildMatrix {
    param([Parameter(Mandatory = $true)]$Tools)
    $toolchain = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
    foreach ($mode in @('off', 'on')) {
        $directory = Join-Path $script:BuildRoot $mode
        $enabled = if ($mode -eq 'on') { 'ON' } else { 'OFF' }
        Invoke-QualificationNative -FilePath $Tools.CMake -Arguments @(
            '-S', $script:SourceRoot, '-B', $directory, '--fresh',
            '-G', 'Visual Studio 18 2026', '-A', 'x64',
            "-DCMAKE_PREFIX_PATH=$QtRoot",
            "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
            '-DVCPKG_TARGET_TRIPLET=x64-windows',
            '-DBUILD_TESTING=ON',
            "-DRTMP_MONITOR_ENABLE_WEBRTC=$enabled"
        )
        Invoke-QualificationNative -FilePath $Tools.CMake -Arguments @(
            '--build', $directory, '--config', $Configuration,
            '--parallel', '4'
        )

        $previousSample = $env:RTMP_MONITOR_WEEK4_SAMPLE
        $previousAudioOnly = $env:RTMP_MONITOR_WEEK4_AUDIO_ONLY
        $previousNonH264 = $env:RTMP_MONITOR_WEEK4_NON_H264
        $previousBFrames = $env:RTMP_MONITOR_WEEK4_B_FRAMES
        try {
            if ($mode -eq 'on') {
                $env:RTMP_MONITOR_WEEK4_SAMPLE = $script:AssetPath
                $env:RTMP_MONITOR_WEEK4_AUDIO_ONLY = $script:AudioOnlyPath
                $env:RTMP_MONITOR_WEEK4_NON_H264 = $script:NonH264Path
                $env:RTMP_MONITOR_WEEK4_B_FRAMES = $script:BFramesPath
            } else {
                Remove-Item Env:RTMP_MONITOR_WEEK4_SAMPLE `
                    -ErrorAction SilentlyContinue
                Remove-Item Env:RTMP_MONITOR_WEEK4_AUDIO_ONLY `
                    -ErrorAction SilentlyContinue
                Remove-Item Env:RTMP_MONITOR_WEEK4_NON_H264 `
                    -ErrorAction SilentlyContinue
                Remove-Item Env:RTMP_MONITOR_WEEK4_B_FRAMES `
                    -ErrorAction SilentlyContinue
            }
            Invoke-QualificationNative -FilePath $Tools.CTest -Arguments @(
                '--test-dir', $directory, '-C', $Configuration,
                '--output-on-failure'
            )
            $listing = & $Tools.CTest --test-dir $directory -C $Configuration `
                -N 2>&1 | Out-String
            if ($LASTEXITCODE -ne 0) {
                throw "CTest listing failed for WebRTC $mode."
            }
            foreach ($required in @(
                    'rtmp_monitor_h264_contract_test',
                    'rtmp_monitor_encoded_video_decode_test')) {
                if ($listing -notmatch [regex]::Escape($required)) {
                    throw "Required CTest is missing for WebRTC ${mode}: $required"
                }
            }
            if ($mode -eq 'on') {
                foreach ($required in @(
                        'rtmp_monitor_webrtc_endpoint_test',
                        'rtmp_monitor_webrtc_viewer_pipeline_test')) {
                    if ($listing -notmatch [regex]::Escape($required)) {
                        throw "Required WebRTC CTest is missing: $required"
                    }
                }
            } elseif ($listing -notmatch 'rtmp_monitor_webrtc_disabled_test') {
                throw 'WebRTC OFF dependency test is missing.'
            }
        } finally {
            $restore = @{
                RTMP_MONITOR_WEEK4_SAMPLE = $previousSample
                RTMP_MONITOR_WEEK4_AUDIO_ONLY = $previousAudioOnly
                RTMP_MONITOR_WEEK4_NON_H264 = $previousNonH264
                RTMP_MONITOR_WEEK4_B_FRAMES = $previousBFrames
            }
            foreach ($name in $restore.Keys) {
                if ($null -eq $restore[$name]) {
                    Remove-Item "Env:$name" -ErrorAction SilentlyContinue
                } else {
                    Set-Item "Env:$name" $restore[$name]
                }
            }
        }
    }
}

function Assert-ClientArguments {
    param([Parameter(Mandatory = $true)][string]$ClientPath)
    $help = & $ClientPath --help 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $help -notmatch '--media-role' -or
        $help -notmatch 'publisher or viewer') {
        throw 'Week 5 client help contract failed.'
    }
    $invalidViewer = & $ClientPath --media-role viewer `
        --signaling-role offer --source sample --timeout-ms 1000 `
        2>&1 | Out-String
    if ($LASTEXITCODE -ne 2 -or
        $invalidViewer -notmatch 'invalid_arguments') {
        throw 'Viewer accepted the publisher-only source option.'
    }
    $invalidPublisher = & $ClientPath --media-role publisher `
        --signaling-role offer --timeout-ms 1000 2>&1 | Out-String
    if ($LASTEXITCODE -ne 2 -or
        $invalidPublisher -notmatch 'invalid_arguments') {
        throw 'Publisher accepted a missing source option.'
    }
    $platformDirectory = Join-Path (Split-Path -Parent $ClientPath) 'platforms'
    $platformName = if ($Configuration -eq 'Debug') {
        'qwindowsd.dll'
    } else { 'qwindows.dll' }
    $offscreenName = if ($Configuration -eq 'Debug') {
        'qoffscreend.dll'
    } else { 'qoffscreen.dll' }
    foreach ($name in @($platformName, $offscreenName)) {
        if (-not (Test-Path -LiteralPath `
                (Join-Path $platformDirectory $name) -PathType Leaf)) {
            throw "Qt platform plugin was not deployed: $name"
        }
    }
}

function Assert-ExchangeEmpty {
    if (-not (Test-Path -LiteralPath $script:ExchangeRoot `
            -PathType Container)) {
        return
    }
    $files = @(Get-ChildItem -LiteralPath $script:ExchangeRoot -File `
        -ErrorAction SilentlyContinue)
    if ($files.Count -ne 0) {
        throw 'Managed signaling exchange files remain after the client pair.'
    }
}

function Invoke-ClientTopology {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('publisher-offer', 'viewer-offer')]
        [string]$Topology
    )
    $runtime = Join-Path $script:BuildRoot "on\webrtc\$Configuration"
    $client = Join-Path $runtime 'rtmp_monitor_webrtc_client.exe'
    $peer = Join-Path $runtime 'rtmp_monitor_webrtc_publisher_peer.exe'
    $assetDirectory = Join-Path $runtime 'webrtc-assets'
    New-Item -ItemType Directory -Force -Path $assetDirectory | Out-Null
    Copy-Item -LiteralPath $script:AssetPath `
        -Destination (Join-Path $assetDirectory 'sample.mp4') -Force
    & $peer --cleanup | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Pre-topology cleanup failed.' }

    New-Item -ItemType Directory -Force -Path $script:LogRoot | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $script:LogRoot -File) {
        [void](Assert-QualificationPathUnderRoot -Path $file.FullName `
            -Root $script:RuntimeRoot -Label 'Week 5 runtime')
        Remove-Item -LiteralPath $file.FullName -Force
    }
    $records = [System.Collections.Generic.List[object]]::new()
    $previousPlatform = $env:QT_QPA_PLATFORM
    $env:QT_QPA_PLATFORM = 'offscreen'
    try {
        if ($Topology -eq 'publisher-offer') {
            $first = Start-OwnedProcess -Name 'publisher-offer' `
                -FilePath $client -Records $records -Arguments @(
                    '--media-role','publisher','--signaling-role','offer',
                    '--source','sample','--timeout-ms','30000')
            [void](Wait-QualificationJsonEvent `
                -Path $records[0].stdout -Event 'description_exported')
            $second = Start-OwnedProcess -Name 'viewer-answer' `
                -FilePath $client -Records $records -Arguments @(
                    '--media-role','viewer','--signaling-role','answer',
                    '--timeout-ms','30000')
        } else {
            $first = Start-OwnedProcess -Name 'viewer-offer' `
                -FilePath $client -Records $records -Arguments @(
                    '--media-role','viewer','--signaling-role','offer',
                    '--timeout-ms','30000')
            [void](Wait-QualificationJsonEvent `
                -Path $records[0].stdout -Event 'description_exported')
            $second = Start-OwnedProcess -Name 'publisher-answer' `
                -FilePath $client -Records $records -Arguments @(
                    '--media-role','publisher','--signaling-role','answer',
                    '--source','sample','--timeout-ms','30000')
        }
        foreach ($process in @($first, $second)) {
            if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
                throw "Week 5 process timeout: $($process.Id)"
            }
            if ($process.ExitCode -ne 0) {
                throw "Week 5 process failed with exit code $($process.ExitCode)."
            }
        }

        $viewerRecord = $records | Where-Object { $_.name -like 'viewer-*' } |
            Select-Object -First 1
        $publisherRecord = $records |
            Where-Object { $_.name -like 'publisher-*' } |
            Select-Object -First 1
        foreach ($event in @(
                'connected','media_received','frame_decoded',
                'frame_presented','completed')) {
            [void](Wait-QualificationJsonEvent -Path $viewerRecord.stdout `
                -Event $event -Seconds 1)
        }
        foreach ($event in @('connected','completed')) {
            [void](Wait-QualificationJsonEvent -Path $publisherRecord.stdout `
                -Event $event -Seconds 1)
        }
        $viewerCompleted = Wait-QualificationJsonEvent `
            -Path $viewerRecord.stdout -Event 'completed' -Seconds 1
        if ([int64]$viewerCompleted.receivedRtpPackets -le 0 -or
            [int64]$viewerCompleted.receivedAccessUnits -le 0 -or
            [int64]$viewerCompleted.submittedAccessUnits -le 0 -or
            -not [bool]$viewerCompleted.decoded -or
            -not [bool]$viewerCompleted.presented) {
            throw 'Viewer completion evidence is incomplete.'
        }
        $publisherCompleted = Wait-QualificationJsonEvent `
            -Path $publisherRecord.stdout -Event 'completed' -Seconds 1
        if ([int64]$publisherCompleted.sentAccessUnits -le 0 -or
            [int64]$publisherCompleted.sendFailures -ne 0) {
            throw 'Publisher completion evidence is incomplete.'
        }
        Assert-ExchangeEmpty
        Assert-QualificationSafeLogs -Paths @(
            $viewerRecord.stdout, $viewerRecord.stderr,
            $publisherRecord.stdout, $publisherRecord.stderr)
    } finally {
        Stop-OwnedProcesses
        & $peer --cleanup | Out-Null
        if ($null -eq $previousPlatform) {
            Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
        } else {
            $env:QT_QPA_PLATFORM = $previousPlatform
        }
    }
}

function Invoke-SelfTest {
    $valid = [pscustomobject]@{
        codec_name = 'h264'; profile = 'Constrained Baseline'; level = 31
        width = 1280; height = 720; has_b_frames = 0; r_frame_rate = '30/1'
    }
    Assert-QualificationSample -Stream $valid
    $rejected = $false
    try {
        $invalid = $valid.PSObject.Copy()
        $invalid.has_b_frames = 2
        Assert-QualificationSample -Stream $invalid
    } catch { $rejected = $true }
    if (-not $rejected) { throw 'Sample B-frame rejection self-test failed.' }

    New-Item -ItemType Directory -Force -Path $script:LogRoot | Out-Null
    $eventPath = Join-Path $script:LogRoot 'selftest.stdout.jsonl'
    '{"event":"frame_presented","renderedFrames":1}' |
        Set-Content -LiteralPath $eventPath -Encoding UTF8
    $event = Wait-QualificationJsonEvent -Path $eventPath `
        -Event 'frame_presented' -Seconds 1
    if ([int]$event.renderedFrames -ne 1) {
        throw 'JSONL event self-test failed.'
    }
    Remove-Item -LiteralPath $eventPath -Force
    Write-Host 'Week 5 qualification self-test passed.'
}

switch ($Action) {
    'SelfTest' { Invoke-SelfTest; break }
    'Status' {
        $state = Read-QualificationState -StatePath $script:StatePath
        if (-not $state) { Write-Host 'Week 5 qualification is idle.'; break }
        foreach ($record in @($state.processes)) {
            $running = $null -ne (Get-Process -Id ([int]$record.pid) `
                -ErrorAction SilentlyContinue)
            Write-Host "$($record.name): pid=$($record.pid) running=$running"
        }
        break
    }
    'Stop' {
        Stop-OwnedProcesses
        Write-Host 'Week 5 owned processes stopped.'
        break
    }
    'Check' { [void](Assert-Prerequisites); break }
    'Run' {
        $tools = Assert-Prerequisites
        New-QualificationAssets -Tools $tools
        Invoke-BuildMatrix -Tools $tools
        $client = Join-Path $script:BuildRoot `
            "on\webrtc\$Configuration\rtmp_monitor_webrtc_client.exe"
        Assert-ClientArguments -ClientPath $client
        foreach ($round in 1..2) {
            Invoke-ClientTopology -Topology publisher-offer
            Invoke-ClientTopology -Topology viewer-offer
        }
        Write-Host 'Week 5 automated qualification passed.'
        break
    }
}
