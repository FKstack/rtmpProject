[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$StatePath,
    [Parameter(Mandatory=$true)][string]$LaunchGatePath,
    [int]$DurationSeconds = 1800
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $StatePath
$stateLockPath = Join-Path $root 'smoke-state.lock'
[void](New-Item -ItemType Directory -Force -Path $root)
$stdoutPath = Join-Path $root 'smoke-stdout.txt'
$stderrPath = Join-Path $root 'smoke-stderr.txt'
$samplesPath = Join-Path $root 'smoke-samples.jsonl'
foreach ($path in @($stdoutPath,$stderrPath,$samplesPath)) {
    Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
}
function Enter-StateLock {
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
for ($attempt = 0; $attempt -lt 400 -and
     -not (Test-Path -LiteralPath $LaunchGatePath -PathType Leaf);
     ++$attempt) {
    Start-Sleep -Milliseconds 25
}
if (-not (Test-Path -LiteralPath $LaunchGatePath -PathType Leaf)) {
    throw 'smoke_launch_gate_timeout'
}
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_LOGGING_RULES = 'qt.qpa.fonts.warning=false'
$env:RTMP_MONITOR_W9_SMOKE_SECONDS = [string]$DurationSeconds
$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $Executable
$startInfo.Arguments = 'fourPeerMediaSessionsRemainIsolated -o "' +
    $stdoutPath + '",txt -nocrashhandler'
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardError = $true
$process = [Diagnostics.Process]::new()
$process.StartInfo = $startInfo
$stateLock = Enter-StateLock
try {
    $launchState = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json
    if ($launchState.status -ne 'starting') { exit 0 }
    if (-not $process.Start()) { throw 'smoke_process_start_failed' }
    $workerProcess = Get-Process -Id $PID
    $workerProcess.Refresh()
    $process.Refresh()
    $ownedProcesses = @(
        [ordered]@{
            name = 'week9-smoke-worker'; pid = $PID
            startTimeUtc = $workerProcess.StartTime.ToUniversalTime().ToString('o')
        },
        [ordered]@{
            name = 'week9-product-test'; pid = $process.Id
            startTimeUtc = $process.StartTime.ToUniversalTime().ToString('o')
        }
    )
    [ordered]@{
        schemaVersion = 1; status = 'running'; processes = $ownedProcesses
        startedAtUtc = [DateTime]::UtcNow.ToString('o')
        physicalFourEndpointClaimed = $false; performanceQualified = $false
        smokePassed = $false; lifecycleEndurancePassed = $false
        cameraQualified = $false
    } | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath $StatePath -Encoding UTF8
} finally {
    $stateLock.Dispose()
    Remove-Item -LiteralPath $LaunchGatePath -Force -ErrorAction SilentlyContinue
}

$samples = [Collections.Generic.List[object]]::new()
$previousCpu = 0.0
$previousAt = Get-Date
while (-not $process.HasExited) {
    Start-Sleep -Seconds 1
    $process.Refresh()
    if ($process.HasExited) { break }
    $now = Get-Date
    $elapsedSeconds = [Math]::Max(0.001, ($now - $previousAt).TotalSeconds)
    $cpu = [Math]::Max(0.0,
        (($process.TotalProcessorTime.TotalSeconds - $previousCpu) /
         $elapsedSeconds / [Environment]::ProcessorCount) * 100.0)
    $previousCpu = $process.TotalProcessorTime.TotalSeconds
    $previousAt = $now
    $sample = [ordered]@{
        elapsedSeconds = [int](($now.ToUniversalTime() -
            $process.StartTime.ToUniversalTime()).TotalSeconds)
        workingSetBytes = [int64]$process.WorkingSet64
        processCpuPercent = [Math]::Round($cpu, 3)
    }
    $samples.Add([pscustomobject]$sample)
    ($sample | ConvertTo-Json -Compress) | Add-Content `
        -LiteralPath $samplesPath -Encoding UTF8
}
$process.WaitForExit()
$process.Refresh()
$exitCode = $process.ExitCode
$process.StandardError.ReadToEnd() |
    Set-Content -LiteralPath $stderrPath -Encoding UTF8

$working = @($samples | ForEach-Object { [double]$_.workingSetBytes })
$cpuSamples = @($samples | ForEach-Object { [double]$_.processCpuPercent } |
    Sort-Object)
$slope = 0.0
$growth = 0.0
if ($working.Count -ge 2) {
    $minutes = [Math]::Max(1.0/60.0,
        ($samples[$samples.Count-1].elapsedSeconds -
         $samples[0].elapsedSeconds) / 60.0)
    $slope = (($working[-1] - $working[0]) / 1MB) / $minutes
    $first = @($working | Select-Object -First ([Math]::Min(60,$working.Count)))
    $last = @($working | Select-Object -Last ([Math]::Min(60,$working.Count)))
    $growth = (($last | Measure-Object -Average).Average -
               ($first | Measure-Object -Average).Average) / 1MB
}
$cpuMean = if($cpuSamples.Count) {
    ($cpuSamples | Measure-Object -Average).Average
} else { 0.0 }
$cpuP95 = if($cpuSamples.Count) {
    $cpuSamples[[Math]::Min($cpuSamples.Count-1,
        [Math]::Floor($cpuSamples.Count * 0.95))]
} else { 0.0 }
$cpuMax = if($cpuSamples.Count) {
    ($cpuSamples | Measure-Object -Maximum).Maximum
} else { 0.0 }
$memoryPassed = $DurationSeconds -lt 1800 -or
    ($slope -le 2.0 -and $growth -le 64.0)
$sensitiveCategories = [Collections.Generic.List[string]]::new()
$combinedOutput = ''
foreach ($path in @(
    $stdoutPath,$stderrPath,$StatePath,
    (Join-Path $root 'qualification-result.json'),
    (Join-Path $root 'camera-result.json')
)) {
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $combinedOutput += Get-Content -LiteralPath $path -Raw
    }
}
$sensitivePatterns = [ordered]@{
    signaling = '(?i)(candidate:|a=candidate|ice-ufrag|ice-pwd|fingerprint|sdp[=:])'
    network = '(?i)(rtmps?://|stun:|turn:|(?:\d{1,3}\.){3}\d{1,3}|(?:address|port)[=:]\s*\d+)'
    ipv6 = '(?i)(?<![0-9a-f:])(?:(?:[0-9a-f]{1,4}:){3,7}[0-9a-f]{1,4}|(?=[0-9a-f:]*[0-9])(?:[0-9a-f]{1,4}:){1,6}:[0-9a-f]{1,4}|::[0-9]+)(?![0-9a-f:])'
    credential = '(?i)(bearer\s+[a-z0-9._~+/-]+=*|(?:token|authorization)[=:]\s*[^\s,}\"]+)'
    identifier = '(?i)([0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})'
    absolute_path = '(?i)([a-z]:[\\/])'
    session_path = '(?i)(?:^|[\\/])session-\d{2}(?:[\\/]|$)'
    camera_identifier = '(?i)(\\\\\?\\|usb#vid_|vid_[0-9a-f]{4}.+pid_[0-9a-f]{4})'
}
foreach ($entry in $sensitivePatterns.GetEnumerator()) {
    if ($combinedOutput -match $entry.Value) {
        [void]$sensitiveCategories.Add($entry.Key)
    }
}
$sensitiveOutputPassed = $sensitiveCategories.Count -eq 0
if (-not $sensitiveOutputPassed) {
    $categoryText = [string]::Join(',', @($sensitiveCategories))
    foreach ($path in @($stdoutPath,$stderrPath)) {
        "sensitive_output_blocked:$categoryText" |
            Set-Content -LiteralPath $path -Encoding UTF8
    }
}
$passed = $exitCode -eq 0 -and $memoryPassed -and
    $sensitiveOutputPassed -and
    $samples.Count -ge [Math]::Max(1,$DurationSeconds - 5)
$lifecycleEndurancePassed = $passed -and $DurationSeconds -ge 1800
$smokePassed = $false

$finalState = [ordered]@{
    schemaVersion = 1
    status = $(if(-not $passed){'failed'}elseif($lifecycleEndurancePassed){'lifecycle_passed'}else{'self_test_passed'})
    exitCode = $exitCode; sampleCount = $samples.Count
    workingSetSlopeMiBPerMinute = [Math]::Round($slope,3)
    workingSetLastVsFirst60MiB = [Math]::Round($growth,3)
    processCpuMeanPercent = [Math]::Round($cpuMean,3)
    processCpuP95Percent = [Math]::Round($cpuP95,3)
    processCpuMaxPercent = [Math]::Round($cpuMax,3)
    sensitiveOutputPassed = $sensitiveOutputPassed
    sensitiveOutputCategories = @($sensitiveCategories)
    physicalFourEndpointClaimed = $false
    performanceQualified = $false
    smokePassed = $smokePassed
    lifecycleEndurancePassed = $lifecycleEndurancePassed
    resourceQualificationBlocked = $true
    resourceQualificationReason = 'resource_smoke_not_run'
    requestedDurationSeconds = $DurationSeconds
    cameraQualified = $false
}
$stateLock = Enter-StateLock
try {
    if (Test-Path -LiteralPath $StatePath -PathType Leaf) {
        $current = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json
        foreach ($name in @('stopRequested','stopCompleted')) {
            if ($current.PSObject.Properties.Name -contains $name) {
                $finalState[$name] = $current.$name
            }
        }
        if ($current.status -eq 'stopping') {
            $finalState.status = 'stopping'
        }
    }
    $finalState | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $StatePath -Encoding UTF8
} finally {
    $stateLock.Dispose()
}
