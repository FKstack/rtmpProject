[CmdletBinding()]
param(
    [ValidateSet('Check','SelfTest','Run','Performance','Status','Stop','Package','Arm','Finalize')]
    [string]$Action = 'Check',
    [string]$QtRoot = $env:QTDIR,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$VsDevCmd,
    [string]$BuildRoot,
    [string]$ResultPath,
    [string]$SamplePath,
    [string]$PackageRoot,
    [string]$Distro = $(if ($env:RTMP_MONITOR_WSL_DISTRO) {
        $env:RTMP_MONITOR_WSL_DISTRO
    } else { 'Ubuntu-22.04-New' }),
    [int]$SingleWarmupSeconds = 60,
    [int]$SingleSampleSeconds = 600,
    [int]$FourWarmupSeconds = 60,
    [int]$FourSampleSeconds = 1800,
    [int]$StopSecond = 600,
    [int]$RebuildSecond = 720
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'QualificationCommon.psm1') -Force

$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $sourceRoot 'out\build-windows-x64\week10'
}
if ([string]::IsNullOrWhiteSpace($ResultPath)) {
    $ResultPath = Join-Path $sourceRoot 'out\webrtc-week10\qualification-result.json'
}
if ([string]::IsNullOrWhiteSpace($SamplePath)) {
    $SamplePath = Join-Path $sourceRoot 'out\webrtc-week7\webrtc-assets\sample.mp4'
}
if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
    $PackageRoot = Join-Path $sourceRoot 'out\packages\webrtc-week10'
}
$runtimeRoot = Join-Path $sourceRoot 'out\webrtc-week10\performance'
$statePath = Join-Path $runtimeRoot 'state.json'
$stopFile = Join-Path $runtimeRoot 'stop.request'
$packageResultPath = Join-Path $sourceRoot 'out\webrtc-week10\package-result.json'
$armResultPath = Join-Path $sourceRoot 'out\webrtc-week10\arm-result.json'
$fixtureRoot = Join-Path $sourceRoot 'out\webrtc-week8\fixtures'
$version = '0.2.0-beta.1'

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    $cache = Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'out') `
        -Filter CMakeCache.txt -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($cache) {
        $line = Get-Content -LiteralPath $cache.FullName |
            Where-Object { $_ -match '^Qt6_DIR:[^=]*=(.+)$' } |
            Select-Object -First 1
        if ($line -and $line -match '^Qt6_DIR:[^=]*=(.+)$') {
            $QtRoot = [IO.Path]::GetFullPath((Join-Path $Matches[1] '..\..\..'))
        }
    }
}
if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    $cache = Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'out') `
        -Filter CMakeCache.txt -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($cache) {
        $line = Get-Content -LiteralPath $cache.FullName |
            Where-Object { $_ -match '^VCPKG_INSTALLED_DIR:[^=]*=(.+)$' } |
            Select-Object -First 1
        if ($line -and $line -match '^VCPKG_INSTALLED_DIR:[^=]*=(.+)$') {
            $VcpkgRoot = [IO.Path]::GetFullPath((Join-Path $Matches[1] '..'))
        }
    }
}

function Get-Tools {
    [void](Assert-QualificationConcretePath -Value $QtRoot -Name 'QtRoot')
    [void](Assert-QualificationConcretePath -Value $VcpkgRoot -Name 'VcpkgRoot')
    foreach ($path in @(
        (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'),
        (Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'),
        $SamplePath,
        (Join-Path $fixtureRoot 'audio-only.mp4'),
        (Join-Path $fixtureRoot 'non-h264.mp4'),
        (Join-Path $fixtureRoot 'h264-bframes.mp4')
    )) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw 'week10_prerequisite_missing'
        }
    }
    return Resolve-QualificationTools -VsDevCmd $VsDevCmd
}

function Get-CtestCount([string]$CTest, [string]$Directory, [string]$Configuration) {
    $output = & $CTest --test-dir $Directory -C $Configuration -N 2>&1 |
        Out-String
    if ($LASTEXITCODE -ne 0 -or $output -notmatch 'Total Tests:\s*(\d+)') {
        throw 'ctest_count_unavailable'
    }
    return [int]$Matches[1]
}

function Invoke-Matrix(
    $Tools,
    [string]$Name,
    [string]$Configuration,
    [bool]$WebRtc
) {
    $directory = Join-Path $BuildRoot $Name
    $enabled = if ($WebRtc) { 'ON' } else { 'OFF' }
    Invoke-QualificationNative -FilePath $Tools.CMake -Arguments @(
        '-S',$sourceRoot,'-B',$directory,'--fresh',
        '-G','Visual Studio 18 2026','-A','x64',
        "-DCMAKE_PREFIX_PATH=$QtRoot",
        "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake')",
        '-DVCPKG_TARGET_TRIPLET=x64-windows',
        '-DBUILD_TESTING=ON',
        "-DRTMP_MONITOR_ENABLE_WEBRTC=$enabled"
    ) | Out-Host
    Invoke-QualificationNative -FilePath $Tools.CMake -Arguments @(
        '--build',$directory,'--config',$Configuration,'--parallel','4'
    ) | Out-Host
    $count = Get-CtestCount $Tools.CTest $directory $Configuration
    $saved = @{}
    foreach ($environmentName in @(
            'Path','QT_QPA_PLATFORM','QT_PLUGIN_PATH',
            'QT_QPA_PLATFORM_PLUGIN_PATH','RTMP_MONITOR_WEEK4_SAMPLE',
            'RTMP_MONITOR_WEEK4_AUDIO_ONLY','RTMP_MONITOR_WEEK4_NON_H264',
            'RTMP_MONITOR_WEEK4_B_FRAMES')) {
        $saved[$environmentName] = [Environment]::GetEnvironmentVariable(
            $environmentName,'Process'
        )
    }
    try {
        $env:QT_QPA_PLATFORM = 'windows'
        $env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $QtRoot 'plugins\platforms'
        $env:Path = Get-QualificationRuntimePath -QtRoot $QtRoot `
            -VcpkgRoot $VcpkgRoot -Configuration $Configuration
        $env:RTMP_MONITOR_WEEK4_SAMPLE = $SamplePath
        $env:RTMP_MONITOR_WEEK4_AUDIO_ONLY = Join-Path $fixtureRoot 'audio-only.mp4'
        $env:RTMP_MONITOR_WEEK4_NON_H264 = Join-Path $fixtureRoot 'non-h264.mp4'
        $env:RTMP_MONITOR_WEEK4_B_FRAMES = Join-Path $fixtureRoot 'h264-bframes.mp4'
        Invoke-QualificationNative -FilePath $Tools.CTest -Arguments @(
            '--test-dir',$directory,'-C',$Configuration,'--output-on-failure'
        ) | Out-Host
    } finally {
        foreach ($environmentName in $saved.Keys) {
            [Environment]::SetEnvironmentVariable(
                $environmentName,$saved[$environmentName],'Process'
            )
        }
    }
    $actual = Get-CtestCount $Tools.CTest $directory $Configuration
    if ($actual -ne $count) { throw 'ctest_count_changed_during_run' }
    return [pscustomobject][ordered]@{
        name = $Name
        configuration = $Configuration
        webRtcEnabled = $WebRtc
        testCount = $count
        passed = $true
    }
}

function Assert-OffBuild(
    [string]$Directory,
    [string]$Configuration,
    [string]$CMakeCommand
) {
    $ctestFile = Get-Content -LiteralPath (Join-Path $Directory 'CTestTestfile.cmake') -Raw
    if ($ctestFile -match '(?i)webrtc') { throw 'off_ctest_contains_webrtc' }
    $forbidden = @(Get-ChildItem -LiteralPath $Directory -Recurse -File |
        Where-Object {
            $_.Name -match '(?i)webrtc' -and
            $_.Extension -in @('.exe','.dll','.lib','.vcxproj')
        })
    if ($forbidden.Count -ne 0) { throw 'off_artifact_contains_webrtc' }
    $executable = Join-Path $Directory "$Configuration\rtmp_monitor.exe"
    $exchange = Join-Path $sourceRoot 'out\webrtc-p2p\session-exchange'
    $existed = Test-Path -LiteralPath $exchange
    & $CMakeCommand "-DEXECUTABLE_PATH=$executable" `
        "-DEXPECTED_VERSION=$version" `
        "-DWORKING_DIRECTORY=$(Split-Path -Parent $executable)" `
        '-P' (Join-Path $sourceRoot 'cmake\VerifyExecutableVersion.cmake')
    if ($LASTEXITCODE -ne 0) {
        throw 'off_version_mismatch'
    }
    if (-not $existed -and (Test-Path -LiteralPath $exchange)) {
        throw 'off_version_probe_created_exchange'
    }
}

function Get-ValidatedProcess($Record) {
    $process = Get-Process -Id ([int]$Record.pid) -ErrorAction SilentlyContinue
    if (-not $process) { return $null }
    $process.Refresh()
    $expected = [DateTime]::Parse(
        [string]$Record.startTimeUtc,
        [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::RoundtripKind
    ).ToUniversalTime()
    if ($process.Path -ine [IO.Path]::GetFullPath([string]$Record.path) -or
        [Math]::Abs(($process.StartTime.ToUniversalTime() - $expected).TotalSeconds) -gt 1) {
        return $null
    }
    return $process
}

function Read-State {
    if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) { return $null }
    return Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
}

function Write-Result($Value, [string]$Path = $ResultPath) {
    [void](New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path))
    $Value | ConvertTo-Json -Depth 14 |
        Set-Content -LiteralPath $Path -Encoding UTF8
}

function Import-VsEnvironment([string]$BatchPath) {
    $lines = & cmd.exe /d /s /c "`"$BatchPath`" -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) { throw 'vs_environment_failed' }
    foreach ($line in $lines) {
        $separator = $line.IndexOf('=')
        if ($separator -le 0) { continue }
        [Environment]::SetEnvironmentVariable(
            $line.Substring(0,$separator), $line.Substring($separator + 1),
            'Process'
        )
    }
}

switch ($Action) {
    'Check' {
        $tools = Get-Tools
        $cmake = Get-Content -LiteralPath (Join-Path $sourceRoot 'CMakeLists.txt') -Raw
        if ($cmake -notmatch 'VERSION 0\.2\.0' -or
            $cmake -notmatch 'RTMP_MONITOR_PRERELEASE "beta\.1"') {
            throw 'week10_version_not_frozen'
        }
        Write-Host 'Week 10 prerequisites passed.'
        break
    }
    'SelfTest' {
        $tools = Get-Tools
        foreach ($relative in @(
            'tests\WebRtcQualificationRunnerMain.cpp',
            'scripts\webrtc\qualify_week10.ps1',
            'scripts\webrtc\week10_performance_worker.ps1',
            'scripts\webrtc\package_week10_beta.ps1'
        )) {
            $path = Join-Path $sourceRoot $relative
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw 'week10_source_missing'
            }
            if ($path.EndsWith('.ps1')) {
                [void][scriptblock]::Create((Get-Content -LiteralPath $path -Raw))
            }
        }
        $newScripts = Get-Content -LiteralPath (
            Join-Path $PSScriptRoot 'week10_performance_worker.ps1') -Raw
        $newScripts += Get-Content -LiteralPath (
            Join-Path $PSScriptRoot 'package_week10_beta.ps1') -Raw
        if ($newScripts -match '(?i)(sha-?256|Get-FileHash)') {
            throw 'content_hash_implementation_forbidden'
        }
        $runner = Join-Path $BuildRoot `
            'debug-on\webrtc\Debug\rtmp_monitor_webrtc_qualification_runner.exe'
        if (Test-Path -LiteralPath $runner -PathType Leaf) {
            $saved = $env:QT_QPA_PLATFORM
            try {
                $env:QT_QPA_PLATFORM = 'offscreen'
                & $runner --self-test
                if ($LASTEXITCODE -ne 0) { throw 'runner_self_test_failed' }
            } finally { $env:QT_QPA_PLATFORM = $saved }
        }
        Write-Host 'Week 10 self-test passed.'
        break
    }
    'Run' {
        $tools = Get-Tools
        $matrices = [Collections.Generic.List[object]]::new()
        [void]$matrices.Add((Invoke-Matrix $tools 'debug-off' 'Debug' $false))
        Assert-OffBuild (Join-Path $BuildRoot 'debug-off') 'Debug' $tools.CMake
        [void]$matrices.Add((Invoke-Matrix $tools 'release-off' 'Release' $false))
        Assert-OffBuild (Join-Path $BuildRoot 'release-off') 'Release' $tools.CMake
        [void]$matrices.Add((Invoke-Matrix $tools 'debug-on' 'Debug' $true))
        [void]$matrices.Add((Invoke-Matrix $tools 'release-on' 'Release' $true))
        Write-Result ([ordered]@{
            week = 10
            version = $version
            matrices = @($matrices)
            fourMatrixPassed = $true
            offBehaviorPassed = $true
            cameraQualified = $false
            physicalLanQualified = $false
            performanceQualified = $false
        })
        break
    }
    'Performance' {
        [void](Get-Tools)
        $runner = Join-Path $BuildRoot `
            'release-on\webrtc\Release\rtmp_monitor_webrtc_qualification_runner.exe'
        if (-not (Test-Path -LiteralPath $runner -PathType Leaf)) {
            throw 'release_runner_missing_run_action_required'
        }
        $existing = Read-State
        if ($existing -and [string]$existing.status -like 'running_*') {
            throw 'performance_already_running'
        }
        [void](New-Item -ItemType Directory -Force -Path $runtimeRoot)
        Remove-Item -LiteralPath $stopFile -Force -ErrorAction SilentlyContinue
        $workerPath = Join-Path $PSScriptRoot 'week10_performance_worker.ps1'
        $arguments = @(
            '-NoProfile','-ExecutionPolicy','Bypass','-File',$workerPath,
            '-Runner',$runner,'-Sample',$SamplePath,
            '-RuntimeRoot',$runtimeRoot,'-StatePath',$statePath,
            '-StopFile',$stopFile,
            '-QtRoot',$QtRoot,'-VcpkgRoot',$VcpkgRoot,
            '-SingleWarmupSeconds',[string]$SingleWarmupSeconds,
            '-SingleSampleSeconds',[string]$SingleSampleSeconds,
            '-FourWarmupSeconds',[string]$FourWarmupSeconds,
            '-FourSampleSeconds',[string]$FourSampleSeconds,
            '-StopSecond',[string]$StopSecond,
            '-RebuildSecond',[string]$RebuildSecond
        )
        $powerShellHost = (Get-Process -Id $PID).Path
        $worker = Start-Process -FilePath $powerShellHost -ArgumentList $arguments `
            -WorkingDirectory $sourceRoot -WindowStyle Hidden -PassThru
        $worker.Refresh()
        Write-Result ([ordered]@{
            status = 'starting'
            stopFile = $stopFile
            processes = @([ordered]@{
                name = 'week10-performance-worker'
                pid = $worker.Id
                path = [IO.Path]::GetFullPath($worker.Path)
                startTimeUtc = $worker.StartTime.ToUniversalTime().ToString('o')
            })
            sameMachineSoftwareQualified = $false
            physicalLanQualified = $false
            performanceQualified = $false
        }) $statePath
        Write-Host "Week 10 performance worker started (pid=$($worker.Id))."
        break
    }
    'Status' {
        $state = Read-State
        if (-not $state) {
            [ordered]@{status='not_started'} | ConvertTo-Json
            break
        }
        $projection = [ordered]@{}
        foreach ($property in $state.PSObject.Properties) {
            if ($property.Name -notin @('processes','stopFile')) {
                $projection[$property.Name] = $property.Value
            }
        }
        if ($state.PSObject.Properties.Name -contains 'processes') {
            $projection.processes = @($state.processes | ForEach-Object {
                [ordered]@{name=$_.name;pid=$_.pid}
            })
        }
        $projection | ConvertTo-Json -Depth 12
        break
    }
    'Stop' {
        $state = Read-State
        if (-not $state) { break }
        [void](New-Item -ItemType Directory -Force -Path $runtimeRoot)
        Set-Content -LiteralPath $stopFile -Value 'stop' -Encoding ASCII
        $workerRecord = @()
        if ($state.PSObject.Properties.Name -contains 'processes') {
            $workerRecord = @($state.processes | Where-Object {
                $_.name -eq 'week10-performance-worker'
            } | Select-Object -First 1)
        }
        if ($workerRecord.Count -eq 1) {
            $worker = Get-ValidatedProcess $workerRecord[0]
            if ($worker -and -not $worker.WaitForExit(20000)) {
                $latest = Read-State
                $latestProcesses = if ($latest.PSObject.Properties.Name -contains
                    'processes') { @($latest.processes) } else { @() }
                foreach ($record in @($latestProcesses | Sort-Object `
                        @{Expression={if($_.name -like '*runner'){0}else{1}}})) {
                    $process = Get-ValidatedProcess $record
                    if (-not $process) { continue }
                    [void]$process.CloseMainWindow()
                    if (-not $process.WaitForExit(3000)) {
                        Stop-Process -Id $process.Id -Force
                        [void]$process.WaitForExit(5000)
                    }
                }
            }
        }
        Write-Host 'Week 10 managed performance processes stopped.'
        break
    }
    'Package' {
        $tools = Get-Tools
        Import-VsEnvironment $tools.VsDevCmd
        $packageBuild = Join-Path $BuildRoot 'package-release-on'
        Invoke-QualificationNative -FilePath $tools.CMake -Arguments @(
            '-S',$sourceRoot,'-B',$packageBuild,'--fresh','-G','Ninja',
            '-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_PREFIX_PATH=$QtRoot",
            "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake')",
            '-DVCPKG_TARGET_TRIPLET=x64-windows','-DBUILD_TESTING=ON',
            '-DRTMP_MONITOR_ENABLE_WEBRTC=ON'
        )
        Invoke-QualificationNative -FilePath $tools.CMake -Arguments @(
            '--build',$packageBuild,'--parallel','4'
        )
        $sourceCommit = (& git -C $sourceRoot rev-parse HEAD).Trim()
        & (Join-Path $PSScriptRoot 'package_week10_beta.ps1') `
            -BuildRoot $packageBuild -OutputRoot $PackageRoot `
            -QtRoot $QtRoot -VcpkgRoot $VcpkgRoot `
            -SamplePath $SamplePath -SourceCommit $sourceCommit `
            -Version $version -ResultPath $packageResultPath
        if ($LASTEXITCODE -ne 0) { throw 'week10_package_failed' }
        break
    }
    'Arm' {
        $commands = @(
            'cmake --preset Linux-ARM64-RASTER-Debug -DRTMP_MONITOR_ENABLE_WEBRTC=OFF',
            'cmake --build --preset Linux-ARM64-RASTER-Debug',
            'cmake --preset Linux-ARM64-GLES3-Debug -DRTMP_MONITOR_ENABLE_WEBRTC=OFF',
            'cmake --build --preset Linux-ARM64-GLES3-Debug',
            'file out/build-linux-arm64/raster-debug/rtmp_monitor',
            'file out/build-linux-arm64/gles3-debug/rtmp_monitor',
            'aarch64-linux-gnu-readelf -d out/build-linux-arm64/raster-debug/rtmp_monitor',
            'aarch64-linux-gnu-readelf -d out/build-linux-arm64/gles3-debug/rtmp_monitor'
        ) -join ' && '
        & wsl.exe -d $Distro -- bash -lc `
            "cd /mnt/e/rtmpProject && $commands"
        $passed = $LASTEXITCODE -eq 0
        Write-Result ([ordered]@{
            armCrossBuildPassed = $passed
            armWebRtcQualified = $false
            armDeviceQualified = $false
        }) $armResultPath
        if (-not $passed) { throw 'arm_cross_build_failed' }
        break
    }
    'Finalize' {
        $base = if (Test-Path -LiteralPath $ResultPath) {
            Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json
        } else { $null }
        $performance = Read-State
        $package = if (Test-Path -LiteralPath $packageResultPath) {
            Get-Content -LiteralPath $packageResultPath -Raw | ConvertFrom-Json
        } else { $null }
        $arm = if (Test-Path -LiteralPath $armResultPath) {
            Get-Content -LiteralPath $armResultPath -Raw | ConvertFrom-Json
        } else { $null }
        $blocked = [Collections.Generic.List[string]]::new()
        [void]$blocked.Add('camera_environment')
        [void]$blocked.Add('physical_lan_environment')
        if (-not $base -or -not [bool]$base.fourMatrixPassed) {
            [void]$blocked.Add('four_matrix_not_passed')
        }
        if (-not $performance -or
            -not [bool]$performance.sameMachineSoftwareQualified) {
            [void]$blocked.Add('same_machine_performance_not_passed')
        }
        if (-not $package -or -not [bool]$package.packagePassed) {
            [void]$blocked.Add('package_not_passed')
        }
        if (-not $arm -or -not [bool]$arm.armCrossBuildPassed) {
            [void]$blocked.Add('arm_cross_build_not_passed')
        }
        $final = [ordered]@{
            week = 10
            version = $version
            sourceCommit = (& git -C $sourceRoot rev-parse HEAD).Trim()
            fourMatrixPassed = $null -ne $base -and [bool]$base.fourMatrixPassed
            offBehaviorPassed = $null -ne $base -and [bool]$base.offBehaviorPassed
            sameMachineSoftwareQualified = $null -ne $performance -and
                [bool]$performance.sameMachineSoftwareQualified
            localPerformanceQualified = $null -ne $performance -and
                [bool]$performance.localPerformanceQualified
            packagePassed = $null -ne $package -and [bool]$package.packagePassed
            armCrossBuildPassed = $null -ne $arm -and [bool]$arm.armCrossBuildPassed
            armWebRtcQualified = $false
            armDeviceQualified = $false
            cameraQualified = $false
            physicalLanQualified = $false
            performanceQualified = $false
            releaseQualified = $false
            blockedReasons = @($blocked)
            gate = 'blocked(' + ([string]::Join(',', @($blocked))) + ')'
        }
        Write-Result $final
        $final | ConvertTo-Json -Depth 12
        break
    }
}
