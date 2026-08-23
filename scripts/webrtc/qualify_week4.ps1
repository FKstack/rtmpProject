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
    [int]$TimeoutSeconds = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'QualificationCommon.psm1') -Force

$script:SourceRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..\..')
)
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $script:SourceRoot 'out\build-windows-x64\week4'
}
$script:BuildRoot = [System.IO.Path]::GetFullPath($BuildRoot)
$script:RuntimeRoot = Join-Path $script:SourceRoot 'out\webrtc-week4'
$script:StatePath = Join-Path $script:RuntimeRoot 'qualification-state.json'
$script:LogRoot = Join-Path $script:RuntimeRoot 'logs'
$script:AssetPath = Join-Path $script:RuntimeRoot 'webrtc-assets\sample.mp4'
$script:AudioOnlyPath = Join-Path $script:RuntimeRoot 'fixtures\audio-only.mp4'
$script:NonH264Path = Join-Path $script:RuntimeRoot 'fixtures\non-h264.mp4'
$script:BFramesPath = Join-Path $script:RuntimeRoot 'fixtures\h264-bframes.mp4'

function Assert-UnderRuntimeRoot {
    param([Parameter(Mandatory = $true)][string]$Path)
    $resolved = [System.IO.Path]::GetFullPath($Path)
    $root = $script:RuntimeRoot.TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the managed Week 4 runtime root: $resolved"
    }
}

function Assert-UnderBuildRoot {
    param([Parameter(Mandatory = $true)][string]$Path)
    $resolved = [System.IO.Path]::GetFullPath($Path)
    $root = $script:BuildRoot.TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the managed Week 4 build root: $resolved"
    }
}

function Find-VsDevCmd {
    if (-not [string]::IsNullOrWhiteSpace($VsDevCmd)) {
        if (-not (Test-Path -LiteralPath $VsDevCmd -PathType Leaf)) {
            throw "VsDevCmd does not exist: $VsDevCmd"
        }
        return [System.IO.Path]::GetFullPath($VsDevCmd)
    }
    $vswhere = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if (-not $vswhere) {
        $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
        if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
            $candidate = Join-Path $programFilesX86 `
                'Microsoft Visual Studio\Installer\vswhere.exe'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $vswhere = Get-Item -LiteralPath $candidate
            }
        }
    }
    if (-not $vswhere) {
        throw 'vswhere.exe was not found.'
    }
    $vswherePath = if ($vswhere -is [System.Management.Automation.ApplicationInfo]) {
        $vswhere.Source
    } else {
        $vswhere.FullName
    }
    $installation = (& $vswherePath -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installation)) {
        throw 'Visual Studio C++ installation was not found.'
    }
    return Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
}

function Get-VsInstallationRoot {
    param([Parameter(Mandatory = $true)][string]$BatchPath)
    $tools = Split-Path -Parent ([System.IO.Path]::GetFullPath($BatchPath))
    $common7 = Split-Path -Parent $tools
    return Split-Path -Parent $common7
}

function Resolve-Tools {
    $batch = Find-VsDevCmd
    $root = Get-VsInstallationRoot -BatchPath $batch
    $cmake = Join-Path $root `
        'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    $ctest = Join-Path $root `
        'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
    foreach ($path in @($batch, $cmake, $ctest)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required Visual Studio tool is missing: $path"
        }
    }
    $ffmpeg = Get-Command ffmpeg.exe -ErrorAction Stop
    $ffprobe = Get-Command ffprobe.exe -ErrorAction Stop
    return [pscustomobject]@{
        VsDevCmd = $batch
        CMake = $cmake
        CTest = $ctest
        Ffmpeg = $ffmpeg.Source
        Ffprobe = $ffprobe.Source
    }
}

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    Invoke-QualificationNative -FilePath $FilePath -Arguments $Arguments
}

function Assert-Prerequisites {
    if ([string]::IsNullOrWhiteSpace($QtRoot) -or
        -not (Test-Path -LiteralPath (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
        throw 'Pass -QtRoot or set QTDIR to the Qt 6.6.1 MSVC kit.'
    }
    if ([string]::IsNullOrWhiteSpace($VcpkgRoot) -or
        -not (Test-Path -LiteralPath (Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'))) {
        throw 'Pass -VcpkgRoot or set VCPKG_ROOT.'
    }
    $tools = Resolve-Tools
    if (Test-Path -LiteralPath $script:StatePath) {
        throw 'A Week 4 qualification state exists. Run -Action Status or Stop.'
    }
    Write-Host 'Week 4 prerequisites passed.'
    return $tools
}

function Write-State {
    param([Parameter(Mandatory = $true)]$Processes)
    Write-QualificationState -StatePath $script:StatePath `
        -RuntimeRoot $script:RuntimeRoot -Processes $Processes
}

function Read-State {
    return Read-QualificationState -StatePath $script:StatePath
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

function Wait-SafeEvent {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Event,
        [int]$Seconds = 30
    )
    [void](Wait-QualificationJsonEvent -Path $Path `
        -Event $Event -Seconds $Seconds)
}

function Stop-OwnedProcesses {
    Stop-QualificationOwnedProcesses -StatePath $script:StatePath `
        -RuntimeRoot $script:RuntimeRoot
}

function Assert-Sample {
    param([Parameter(Mandatory = $true)]$Stream)
    Assert-QualificationSample -Stream $Stream
}

function New-QualificationSample {
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
        Invoke-Native -FilePath $Tools.CMake -Arguments @(
            '-S', $script:SourceRoot, '-B', $directory, '--fresh',
            '-G', 'Visual Studio 18 2026', '-A', 'x64',
            "-DCMAKE_PREFIX_PATH=$QtRoot",
            "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
            '-DVCPKG_TARGET_TRIPLET=x64-windows',
            '-DBUILD_TESTING=ON',
            "-DRTMP_MONITOR_ENABLE_WEBRTC=$enabled"
        )
        Invoke-Native -FilePath $Tools.CMake -Arguments @(
            '--build', $directory, '--config', $Configuration, '--parallel', '4'
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
                Remove-Item Env:RTMP_MONITOR_WEEK4_SAMPLE -ErrorAction SilentlyContinue
                Remove-Item Env:RTMP_MONITOR_WEEK4_AUDIO_ONLY -ErrorAction SilentlyContinue
                Remove-Item Env:RTMP_MONITOR_WEEK4_NON_H264 -ErrorAction SilentlyContinue
                Remove-Item Env:RTMP_MONITOR_WEEK4_B_FRAMES -ErrorAction SilentlyContinue
            }
            Invoke-Native -FilePath $Tools.CTest -Arguments @(
                '--test-dir', $directory, '-C', $Configuration,
                '--output-on-failure'
            )
        } finally {
            if ($null -eq $previousSample) {
                Remove-Item Env:RTMP_MONITOR_WEEK4_SAMPLE -ErrorAction SilentlyContinue
            } else {
                $env:RTMP_MONITOR_WEEK4_SAMPLE = $previousSample
            }
            if ($null -eq $previousAudioOnly) {
                Remove-Item Env:RTMP_MONITOR_WEEK4_AUDIO_ONLY -ErrorAction SilentlyContinue
            } else {
                $env:RTMP_MONITOR_WEEK4_AUDIO_ONLY = $previousAudioOnly
            }
            if ($null -eq $previousNonH264) {
                Remove-Item Env:RTMP_MONITOR_WEEK4_NON_H264 -ErrorAction SilentlyContinue
            } else {
                $env:RTMP_MONITOR_WEEK4_NON_H264 = $previousNonH264
            }
            if ($null -eq $previousBFrames) {
                Remove-Item Env:RTMP_MONITOR_WEEK4_B_FRAMES -ErrorAction SilentlyContinue
            } else {
                $env:RTMP_MONITOR_WEEK4_B_FRAMES = $previousBFrames
            }
        }
    }
}

function Assert-SafeLogs {
    $paths = @(Get-ChildItem -LiteralPath $script:LogRoot `
        -Filter '*.stdout.jsonl' -File | ForEach-Object { $_.FullName })
    Assert-QualificationSafeLogs -Paths $paths
}

function Assert-ClientArguments {
    param([Parameter(Mandatory = $true)][string]$ClientPath)
    $helpOutput = & $ClientPath --help 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $helpOutput -notmatch '--media-role') {
        throw 'Week 4 client help contract failed.'
    }
    $invalidOutput = & $ClientPath --media-role viewer 2>&1 | Out-String
    if ($LASTEXITCODE -ne 2 -or $invalidOutput -notmatch 'invalid_arguments') {
        throw 'Week 4 client invalid-argument contract failed.'
    }
    if (($helpOutput + $invalidOutput) -match
        '(?i)(candidate:|a=candidate|fingerprint|ice-ufrag|ice-pwd|stun:|turn:|token|rtmps?://|[A-Z]:\\)') {
        throw 'Sensitive output pattern found in Week 4 CLI contract output.'
    }
}

function Assert-ClientMissingSample {
    param([Parameter(Mandatory = $true)][string]$ClientPath)
    $output = & $ClientPath --media-role publisher --signaling-role offer `
        --source sample --timeout-ms 1000 2>&1 | Out-String
    if ($LASTEXITCODE -ne 4 -or $output -notmatch 'file_not_found') {
        throw 'Week 4 client missing-sample contract failed.'
    }
}

function Assert-ClientSignalingTimeout {
    param(
        [Parameter(Mandatory = $true)][string]$ClientPath,
        [Parameter(Mandatory = $true)][string]$PeerPath
    )
    & $PeerPath --cleanup | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Pre-timeout signaling cleanup failed.' }
    $output = & $ClientPath --media-role publisher --signaling-role offer `
        --source sample --timeout-ms 1000 2>&1 | Out-String
    if ($LASTEXITCODE -ne 3 -or $output -notmatch 'not_found') {
        throw 'Week 4 client signaling-timeout contract failed.'
    }
    & $PeerPath --cleanup | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Post-timeout signaling cleanup failed.' }
}

function Invoke-ClientPair {
    param(
        [Parameter(Mandatory = $true)][ValidateSet('offer', 'answer')]
        [string]$PublisherRole
    )
    $onRoot = Join-Path $script:BuildRoot 'on\webrtc'
    $runtime = Join-Path $onRoot $Configuration
    $client = Join-Path $runtime 'rtmp_monitor_webrtc_client.exe'
    $peer = Join-Path $runtime 'rtmp_monitor_webrtc_publisher_peer.exe'
    $assetDirectory = Join-Path $runtime 'webrtc-assets'
    New-Item -ItemType Directory -Force -Path $assetDirectory | Out-Null
    $runtimeSample = Join-Path $assetDirectory 'sample.mp4'
    Assert-UnderBuildRoot -Path $runtimeSample
    Remove-Item -LiteralPath $runtimeSample -Force -ErrorAction SilentlyContinue
    Assert-ClientArguments -ClientPath $client
    Assert-ClientMissingSample -ClientPath $client
    Copy-Item -LiteralPath $script:AssetPath -Destination $runtimeSample -Force
    Assert-ClientSignalingTimeout -ClientPath $client -PeerPath $peer
    & $peer --cleanup | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'Managed signaling cleanup failed.' }
    New-Item -ItemType Directory -Force -Path $script:LogRoot | Out-Null
    Assert-UnderRuntimeRoot -Path $script:LogRoot
    foreach ($logFile in Get-ChildItem -LiteralPath $script:LogRoot -File) {
        Assert-UnderRuntimeRoot -Path $logFile.FullName
        Remove-Item -LiteralPath $logFile.FullName -Force
    }
    $records = [System.Collections.Generic.List[object]]::new()
    try {
        if ($PublisherRole -eq 'offer') {
            $first = Start-OwnedProcess -Name 'publisher-offer' -FilePath $client `
                -Arguments @('--media-role','publisher','--signaling-role','offer','--source','sample','--timeout-ms','30000') `
                -Records $records
            Wait-SafeEvent -Path $records[0].stdout -Event 'description_exported'
            $second = Start-OwnedProcess -Name 'peer-answer' -FilePath $peer `
                -Arguments @('--signaling-role','answer','--timeout-ms','30000') `
                -Records $records
        } else {
            $first = Start-OwnedProcess -Name 'peer-offer' -FilePath $peer `
                -Arguments @('--signaling-role','offer','--timeout-ms','30000') `
                -Records $records
            Wait-SafeEvent -Path $records[0].stdout -Event 'description_exported'
            $second = Start-OwnedProcess -Name 'publisher-answer' -FilePath $client `
                -Arguments @('--media-role','publisher','--signaling-role','answer','--source','sample','--timeout-ms','30000') `
                -Records $records
        }
        foreach ($process in @($first, $second)) {
            if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
                throw "Week 4 process timeout: $($process.Id)"
            }
            if ($process.ExitCode -ne 0) {
                throw "Week 4 process failed with exit code $($process.ExitCode)."
            }
        }
        $peerRecord = $records | Where-Object { $_.name -like 'peer-*' } |
            Select-Object -First 1
        if ($null -eq $peerRecord) {
            throw 'Week 4 receiver process record is missing.'
        }
        Wait-SafeEvent -Path $peerRecord.stdout `
            -Event 'recoverable_keyframe_received' -Seconds 1
        Assert-SafeLogs
    } finally {
        Stop-OwnedProcesses
        & $peer --cleanup | Out-Null
    }
}

function Invoke-SelfTest {
    $valid = [pscustomobject]@{
        codec_name = 'h264'; profile = 'Constrained Baseline'; level = 31
        width = 1280; height = 720; has_b_frames = 0; r_frame_rate = '30/1'
    }
    Assert-Sample -Stream $valid
    $rejected = $false
    try {
        $invalid = $valid.PSObject.Copy()
        $invalid.profile = 'Main'
        Assert-Sample -Stream $invalid
    } catch { $rejected = $true }
    if (-not $rejected) { throw 'Sample profile rejection self-test failed.' }
    Write-Host 'Week 4 qualification self-test passed.'
}

switch ($Action) {
    'SelfTest' { Invoke-SelfTest; break }
    'Status' {
        $state = Read-State
        if (-not $state) { Write-Host 'Week 4 qualification is idle.'; break }
        foreach ($record in @($state.processes)) {
            $running = $null -ne (Get-Process -Id ([int]$record.pid) -ErrorAction SilentlyContinue)
            Write-Host "$($record.name): pid=$($record.pid) running=$running"
        }
        break
    }
    'Stop' { Stop-OwnedProcesses; Write-Host 'Week 4 owned processes stopped.'; break }
    'Check' { [void](Assert-Prerequisites); break }
    'Run' {
        $tools = Assert-Prerequisites
        New-QualificationSample -Tools $tools
        Invoke-BuildMatrix -Tools $tools
        Invoke-ClientPair -PublisherRole offer
        Invoke-ClientPair -PublisherRole answer
        Write-Host 'Week 4 automated qualification passed.'
        break
    }
}
