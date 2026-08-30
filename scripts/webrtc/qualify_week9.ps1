[CmdletBinding()]
param(
    [ValidateSet('Check','SelfTest','Run','Smoke','Status','Stop','Camera')]
    [string]$Action = 'Check',
    [string]$QtRoot = $env:QTDIR,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$VsDevCmd,
    [string]$BuildRoot,
    [string]$ResultPath,
    [int]$CameraIndex = -1,
    [ValidateRange(1,1800)][int]$SmokeDurationSeconds = 1800
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $sourceRoot 'out\build-windows-x64\week9'
}
if ([string]::IsNullOrWhiteSpace($ResultPath)) {
    $ResultPath = Join-Path $sourceRoot 'out\webrtc-week9\qualification-result.json'
}
$statePath = Join-Path $sourceRoot 'out\webrtc-week9\smoke-state.json'
$stateLockPath = Join-Path $sourceRoot 'out\webrtc-week9\smoke-state.lock'
$cameraResultPath = Join-Path $sourceRoot 'out\webrtc-week9\camera-result.json'
$week8 = Join-Path $PSScriptRoot 'qualify_week8.ps1'

function Invoke-Week8CompatibleMatrix([string]$MatrixAction) {
    $arguments = @(
        '-NoProfile','-ExecutionPolicy','Bypass','-File',$week8,
        '-Action',$MatrixAction,'-BuildRoot',$BuildRoot,
        '-ResultPath',$ResultPath
    )
    if (-not [string]::IsNullOrWhiteSpace($QtRoot)) {
        $arguments += @('-QtRoot',$QtRoot)
    }
    if (-not [string]::IsNullOrWhiteSpace($VcpkgRoot)) {
        $arguments += @('-VcpkgRoot',$VcpkgRoot)
    }
    if (-not [string]::IsNullOrWhiteSpace($VsDevCmd)) {
        $arguments += @('-VsDevCmd',$VsDevCmd)
    }
    & powershell.exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "Week 9 $MatrixAction failed." }
    if ($MatrixAction -eq 'Run') {
        $result = Get-Content -LiteralPath $ResultPath -Raw |
            ConvertFrom-Json
        $result.week = 9
        $result | Add-Member -NotePropertyName cameraQualified `
            -NotePropertyValue $false -Force
        $result | Add-Member -NotePropertyName smokePassed `
            -NotePropertyValue $false -Force
        $result | Add-Member -NotePropertyName lifecycleEndurancePassed `
            -NotePropertyValue $false -Force
        $result | Add-Member -NotePropertyName performanceQualified `
            -NotePropertyValue $false -Force
        $result | Add-Member -NotePropertyName physicalFourEndpointClaimed `
            -NotePropertyValue $false -Force
        $result | Add-Member -NotePropertyName sourceTreeModified `
            -NotePropertyValue $true -Force
        $result | Add-Member -NotePropertyName blockedReasons `
            -NotePropertyValue @('camera_environment','resource_smoke_not_run') -Force
        $result | Add-Member -NotePropertyName gate `
            -NotePropertyValue 'blocked(camera_environment,resource_smoke_not_run)' -Force
        $result | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $ResultPath -Encoding UTF8
    }
}

function Enter-StateLock {
    $directory = Split-Path -Parent $stateLockPath
    [void](New-Item -ItemType Directory -Force -Path $directory)
    for ($attempt = 0; $attempt -lt 200; ++$attempt) {
        try {
            return [IO.File]::Open(
                $stateLockPath,
                [IO.FileMode]::OpenOrCreate,
                [IO.FileAccess]::ReadWrite,
                [IO.FileShare]::None
            )
        } catch [IO.IOException] {
            Start-Sleep -Milliseconds 25
        }
    }
    throw 'smoke_state_lock_timeout'
}

function Test-OwnedIdentityValues(
    [string]$ExpectedPath,
    [DateTime]$ExpectedStart,
    [string]$ActualPath,
    [DateTime]$ActualStart
) {
    return $ActualPath -ieq [IO.Path]::GetFullPath($ExpectedPath) -and
        [Math]::Abs((
            $ActualStart.ToUniversalTime() -
            $ExpectedStart.ToUniversalTime()
        ).TotalSeconds) -le 1
}

function Resolve-OwnedExecutable([string]$Name) {
    if ($Name -eq 'week9-product-test') {
        return [IO.Path]::GetFullPath((Join-Path $BuildRoot `
            'release-on\webrtc\Release\rtmp_monitor_webrtc_product_test.exe'))
    }
    if ($Name -eq 'week9-smoke-worker') {
        return [IO.Path]::GetFullPath((Get-Command powershell.exe).Source)
    }
    throw "unknown_owned_process_role:$Name"
}

function Get-StopTerminalStatus([string]$Status) {
    if ($Status -in @('passed','self_test_passed','failed','lifecycle_passed')) {
        return $Status
    }
    return 'stopped'
}

function Get-ValidatedOwnedProcess($Record) {
    $process = $null
    $actualPath = $null
    $actualStart = $null
    for ($attempt = 0; $attempt -lt 20; ++$attempt) {
        $process = Get-Process -Id ([int]$Record.pid) `
            -ErrorAction SilentlyContinue
        if (-not $process) { return $null }
        try {
            $process.Refresh()
            $actualPath = $process.Path
            $actualStart = $process.StartTime
        } catch {
            $actualPath = $null
            $actualStart = $null
        }
        if (-not [string]::IsNullOrWhiteSpace([string]$actualPath) -and
            $null -ne $actualStart) { break }
        Start-Sleep -Milliseconds 25
    }
    if ([string]::IsNullOrWhiteSpace([string]$actualPath) -or
        $null -eq $actualStart) {
        throw "Owned process identity unavailable for PID $($Record.pid); no process was stopped."
    }
    $expectedStart = [DateTime]::Parse(
        [string]$Record.startTimeUtc,
        [Globalization.CultureInfo]::InvariantCulture,
        [Globalization.DateTimeStyles]::RoundtripKind
    )
    $expectedPath = Resolve-OwnedExecutable ([string]$Record.name)
    if (-not (Test-OwnedIdentityValues $expectedPath $expectedStart `
            $actualPath $actualStart)) {
        throw "Owned process identity mismatch for PID $($Record.pid); no process was stopped."
    }
    return $process
}

function Write-State([string]$status, [string]$reason) {
    $directory = Split-Path -Parent $statePath
    [void](New-Item -ItemType Directory -Force -Path $directory)
    [ordered]@{
        schemaVersion = 1
        status = $status
        reason = $reason
        physicalFourEndpointClaimed = $false
        performanceQualified = $false
        smokePassed = $false
        cameraQualified = $false
    } | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
}

function Stop-OwnedProcesses {
    if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) { return }
    $stateLock = Enter-StateLock
    try {
        $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        $previousStatus = [string]$state.status
        $state | Add-Member -NotePropertyName stopRequested `
            -NotePropertyValue $true -Force
        if ($previousStatus -in @('starting','running','stopping')) {
            $state.status = 'stopping'
        }
        $state | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $statePath -Encoding UTF8
        $records = if ($state.PSObject.Properties.Name -contains 'processes') {
            @($state.processes)
        } else { @() }
    } finally {
        $stateLock.Dispose()
    }
    foreach ($record in $records) {
        [void](Get-ValidatedOwnedProcess $record)
    }
    $ordered = @($records | Sort-Object @{Expression={
        if ($_.name -eq 'week9-product-test') { 0 } else { 1 }
    }})
    foreach ($record in $ordered) {
        $process = Get-ValidatedOwnedProcess $record
        if (-not $process) { continue }
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(3000)) {
            Stop-Process -Id $process.Id -Force
            [void]$process.WaitForExit(5000)
        }
    }
    $stateLock = Enter-StateLock
    try {
        $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        $state | Add-Member -NotePropertyName stopRequested `
            -NotePropertyValue $true -Force
        $state | Add-Member -NotePropertyName stopCompleted `
            -NotePropertyValue $true -Force
        $state.status = Get-StopTerminalStatus $previousStatus
        $state | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $statePath -Encoding UTF8
    } finally {
        $stateLock.Dispose()
    }
}

switch ($Action) {
    'Check' { Invoke-Week8CompatibleMatrix 'Check'; break }
    'SelfTest' {
        Invoke-Week8CompatibleMatrix 'SelfTest'
        foreach ($path in @(
            'include\common\publisher\CameraH264PublisherSource.h',
            'src\common\publisher\CameraH264PublisherSource.cpp',
            'src\common\publisher\CameraH264Policy.cpp',
            'tests\CameraH264PublisherSourceTest.cpp'
        )) {
            if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot $path))) {
                throw "Week 9 source missing: $path"
            }
        }
        foreach ($script in @(
            (Join-Path $PSScriptRoot 'qualify_week9.ps1'),
            (Join-Path $PSScriptRoot 'week9_smoke_worker.ps1')
        )) {
            [void][scriptblock]::Create(
                (Get-Content -LiteralPath $script -Raw)
            )
        }
        $cmake = Get-Content -LiteralPath (Join-Path $sourceRoot 'CMakeLists.txt') -Raw
        foreach ($library in @('mf','mfplat','mfreadwrite','mfuuid','ole32')) {
            if ($cmake -notmatch "(?m)\b$library\b") {
                throw "Media Foundation link missing: $library"
            }
        }
        $identityPath = Join-Path $sourceRoot 'ownership-test.exe'
        $identityStart = [DateTime]::UtcNow
        if (-not (Test-OwnedIdentityValues $identityPath $identityStart `
                $identityPath $identityStart)) {
            throw 'owned_identity_match_self_test_failed'
        }
        if (Test-OwnedIdentityValues $identityPath $identityStart `
                (Join-Path $sourceRoot 'unrelated.exe') $identityStart) {
            throw 'unrelated_process_identity_self_test_failed'
        }
        if (Test-OwnedIdentityValues $identityPath $identityStart `
                $identityPath $identityStart.AddMinutes(1)) {
            throw 'owned_start_time_mismatch_self_test_failed'
        }
        if ((Get-StopTerminalStatus 'passed') -ne 'passed' -or
            (Get-StopTerminalStatus 'self_test_passed') -ne
                'self_test_passed' -or
            (Get-StopTerminalStatus 'lifecycle_passed') -ne
                'lifecycle_passed' -or
            (Get-StopTerminalStatus 'running') -ne 'stopped') {
            throw 'stop_terminal_state_self_test_failed'
        }
        Write-Host 'Week 9 self-test passed.'
        break
    }
    'Run' {
        Invoke-Week8CompatibleMatrix 'Run'
        $onCtest = Get-Content -LiteralPath (
            Join-Path $BuildRoot 'debug-on\CTestTestfile.cmake'
        ) -Raw
        if ($onCtest -notmatch 'rtmp_monitor_camera_h264_publisher_source_test') {
            throw 'WebRTC ON must register the camera component test.'
        }
        foreach ($relative in @(
            'debug-on\rtmp_monitor_h264_publisher_source.vcxproj',
            'release-on\rtmp_monitor_h264_publisher_source.vcxproj',
            'release-on\webrtc\Release\rtmp_monitor_camera_h264_publisher_source_test.exe'
        )) {
            if (-not (Test-Path -LiteralPath (Join-Path $BuildRoot $relative))) {
                throw "WebRTC ON camera artifact missing: $relative"
            }
        }
        $offCtest = Get-Content -LiteralPath (
            Join-Path $BuildRoot 'debug-off\CTestTestfile.cmake'
        ) -Raw
        if ($offCtest -match 'camera_h264_publisher_source') {
            throw 'WebRTC OFF unexpectedly registered a camera test.'
        }
        foreach ($relative in @(
            'debug-off\rtmp_monitor_h264_publisher_source.vcxproj',
            'debug-off\rtmp_monitor_camera_h264_publisher_source_test.vcxproj'
        )) {
            if (Test-Path -LiteralPath (Join-Path $BuildRoot $relative)) {
                throw "WebRTC OFF unexpectedly produced camera artifact: $relative"
            }
        }
        break
    }
    'Smoke' {
        $executable = Join-Path $BuildRoot `
            'release-on\webrtc\Release\rtmp_monitor_webrtc_product_test.exe'
        if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
            throw 'Release ON product test is missing; run -Action Run first.'
        }
        [void](New-Item -ItemType Directory -Force -Path (
            Join-Path (Split-Path -Parent $executable) 'lib\fonts'
        ))
        $stateLock = Enter-StateLock
        try {
            if (Test-Path -LiteralPath $statePath) {
                $existing = Get-Content -LiteralPath $statePath -Raw |
                    ConvertFrom-Json
                if ($existing.status -in @('starting','running','stopping')) {
                    throw 'Week 9 smoke is already active.'
                }
            }
            [ordered]@{
                schemaVersion = 1; status = 'starting'
                reason = 'dispatcher_reserving_worker'
                physicalFourEndpointClaimed = $false
                performanceQualified = $false; smokePassed = $false
                lifecycleEndurancePassed = $false; cameraQualified = $false
            } | ConvertTo-Json -Depth 5 |
                Set-Content -LiteralPath $statePath -Encoding UTF8
        } finally { $stateLock.Dispose() }
        $worker = Join-Path $PSScriptRoot 'week9_smoke_worker.ps1'
        $launchGatePath = Join-Path (Split-Path -Parent $statePath) `
            'smoke-launch.gate'
        Remove-Item -LiteralPath $launchGatePath -Force `
            -ErrorAction SilentlyContinue
        $process = Start-Process -FilePath 'powershell.exe' -ArgumentList @(
            '-NoProfile','-ExecutionPolicy','Bypass','-File',$worker,
            '-Executable',$executable,'-StatePath',$statePath,
            '-LaunchGatePath',$launchGatePath,
            '-DurationSeconds',[string]$SmokeDurationSeconds
        ) -PassThru -WindowStyle Hidden
        $process.Refresh()
        $stateLock = Enter-StateLock
        try {
            $reserved = Get-Content -LiteralPath $statePath -Raw |
                ConvertFrom-Json
            if ($reserved.status -ne 'starting') {
                $process.Kill()
                [void]$process.WaitForExit(5000)
                throw 'smoke_dispatch_cancelled_before_handoff'
            }
            [ordered]@{
                schemaVersion = 1; status = 'starting'
                reason = 'worker_dispatched'
                processes = @([ordered]@{
                    name = 'week9-smoke-worker'; pid = $process.Id
                    startTimeUtc = $process.StartTime.ToUniversalTime().ToString('o')
                })
                physicalFourEndpointClaimed = $false
                performanceQualified = $false; smokePassed = $false
                lifecycleEndurancePassed = $false; cameraQualified = $false
            } | ConvertTo-Json -Depth 5 |
                Set-Content -LiteralPath $statePath -Encoding UTF8
            [IO.File]::WriteAllText($launchGatePath, 'ready')
        } finally { $stateLock.Dispose() }
        Write-Host "Week 9 smoke worker started (pid=$($process.Id))."
        break
    }
    'Status' {
        if (Test-Path -LiteralPath $statePath) {
            $state = Get-Content -LiteralPath $statePath -Raw |
                ConvertFrom-Json
            $projection = [ordered]@{}
            foreach ($property in $state.PSObject.Properties) {
                if ($property.Name -ne 'processes') {
                    $projection[$property.Name] = $property.Value
                }
            }
            if ($state.PSObject.Properties.Name -contains 'processes') {
                $projection.processes = @($state.processes | ForEach-Object {
                    [ordered]@{name=$_.name; pid=$_.pid}
                })
            }
            $projection | ConvertTo-Json -Depth 8
        } else {
            Write-State 'blocked' 'long_duration_environment_not_started'
            Get-Content -LiteralPath $statePath -Raw
        }
        break
    }
    'Stop' {
        Stop-OwnedProcesses
        Write-Host 'Week 9 managed smoke processes stopped.'
        break
    }
    'Camera' {
        if ($CameraIndex -lt 0) {
            throw 'camera_index_required'
        }
        [void](New-Item -ItemType Directory -Force -Path (
            Split-Path -Parent $cameraResultPath
        ))
        [ordered]@{
            schemaVersion = 1; status = 'blocked'
            reason = 'camera_environment'; cameraIndex = $CameraIndex
            cameraQualified = $false
        } | ConvertTo-Json | Set-Content -LiteralPath $cameraResultPath `
            -Encoding UTF8
        throw 'Camera blocked(camera_environment): no authorized paired receiver qualification was executed; the aggregate gate also retains resource_smoke_not_run.'
    }
}
