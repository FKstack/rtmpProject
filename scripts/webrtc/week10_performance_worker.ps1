[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Runner,
    [Parameter(Mandatory=$true)][string]$Sample,
    [Parameter(Mandatory=$true)][string]$RuntimeRoot,
    [Parameter(Mandatory=$true)][string]$StatePath,
    [Parameter(Mandatory=$true)][string]$StopFile,
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [Parameter(Mandatory=$true)][string]$VcpkgRoot,
    [int]$SingleWarmupSeconds = 60,
    [int]$SingleSampleSeconds = 600,
    [int]$FourWarmupSeconds = 60,
    [int]$FourSampleSeconds = 1800,
    [int]$StopSecond = 600,
    [int]$RebuildSecond = 720
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RuntimeRoot = [IO.Path]::GetFullPath($RuntimeRoot)
$Runner = [IO.Path]::GetFullPath($Runner)
$Sample = [IO.Path]::GetFullPath($Sample)
$QtRoot = [IO.Path]::GetFullPath($QtRoot)
$VcpkgRoot = [IO.Path]::GetFullPath($VcpkgRoot)
[void](New-Item -ItemType Directory -Force -Path $RuntimeRoot)
Remove-Item -LiteralPath $StopFile -Force -ErrorAction SilentlyContinue

function Write-WorkerState($Value) {
    $temporary = $StatePath + '.tmp'
    $Value | ConvertTo-Json -Depth 12 |
        Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $StatePath -Force
}

function Get-ProcessRecord(
    [string]$Name,
    [Diagnostics.Process]$Process,
    [string]$ExpectedPath
) {
    $Process.Refresh()
    $startedAt = $Process.StartTime.ToUniversalTime()
    return [ordered]@{
        name = $Name
        pid = $Process.Id
        path = [IO.Path]::GetFullPath($ExpectedPath)
        startTimeUtc = $startedAt.ToString('o')
    }
}

function Get-Percentile([double[]]$Values, [double]$Fraction) {
    if ($Values.Count -eq 0) { return 0.0 }
    $sorted = @($Values | Sort-Object)
    $index = [Math]::Max(0, [Math]::Min(
        $sorted.Count - 1,
        [Math]::Ceiling($sorted.Count * $Fraction) - 1
    ))
    return [double]$sorted[$index]
}

function Get-LinearSlopeMiBPerMinute([object[]]$Samples) {
    if ($Samples.Count -lt 2) { return 0.0 }
    [double]$sumX = 0
    [double]$sumY = 0
    [double]$sumXY = 0
    [double]$sumXX = 0
    for ($index = 0; $index -lt $Samples.Count; ++$index) {
        [double]$x = $index
        [double]$y = $Samples[$index].workingSetBytes
        $sumX += $x
        $sumY += $y
        $sumXY += $x * $y
        $sumXX += $x * $x
    }
    [double]$denominator = $Samples.Count * $sumXX - $sumX * $sumX
    if ([Math]::Abs($denominator) -lt 0.0001) { return 0.0 }
    [double]$bytesPerSecond =
        ($Samples.Count * $sumXY - $sumX * $sumY) / $denominator
    return ($bytesPerSecond * 60.0) / 1MB
}

function Test-SensitiveOutput([string[]]$Paths) {
    $patterns = [ordered]@{
        signaling = '(?i)(candidate:|a=candidate|ice-ufrag|ice-pwd|fingerprint|"sdp"\s*:)'
        network = '(?i)(rtmps?://|stun:|turn:|(?:\d{1,3}\.){3}\d{1,3})'
        identifier = '(?i)([0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})'
        absolute_path = '(?i)([a-z]:[\\/])'
        camera_identifier = '(?i)(usb#vid_|vid_[0-9a-f]{4}.+pid_[0-9a-f]{4})'
    }
    $categories = [Collections.Generic.List[string]]::new()
    foreach ($path in $Paths) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        $text = Get-Content -LiteralPath $path -Raw
        foreach ($entry in $patterns.GetEnumerator()) {
            if ($text -match $entry.Value -and
                -not $categories.Contains([string]$entry.Key)) {
                [void]$categories.Add([string]$entry.Key)
            }
        }
    }
    return [pscustomobject]@{
        passed = $categories.Count -eq 0
        categories = @($categories)
    }
}

function Invoke-Scenario(
    [string]$Scenario,
    [int]$WarmupSeconds,
    [int]$SampleSeconds
) {
    $stdout = Join-Path $RuntimeRoot "$Scenario-runner.jsonl"
    $stderr = Join-Path $RuntimeRoot "$Scenario-runner.stderr.txt"
    $processSamplesPath = Join-Path $RuntimeRoot "$Scenario-process.jsonl"
    foreach ($path in @($stdout,$stderr,$processSamplesPath)) {
        Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    }
    $arguments = @(
        "--scenario=$Scenario",
        "--sample=$Sample",
        "--warmup-seconds=$WarmupSeconds",
        "--sample-seconds=$SampleSeconds",
        "--stop-file=$StopFile"
    )
    if ($Scenario -eq 'four') {
        $arguments += @(
            "--stop-second=$StopSecond",
            "--rebuild-second=$RebuildSecond"
        )
    }
    $runnerConfigurationDirectory = Split-Path -Parent $Runner
    $runnerWebRtcDirectory = Split-Path -Parent $runnerConfigurationDirectory
    $runnerBuildDirectory = Split-Path -Parent $runnerWebRtcDirectory
    $mainConfigurationDirectory = Join-Path $runnerBuildDirectory 'Release'
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $QtRoot 'plugins\platforms'
    $env:Path = (@(
        $runnerConfigurationDirectory,
        $mainConfigurationDirectory,
        (Join-Path $QtRoot 'bin'),
        (Join-Path $VcpkgRoot 'installed\x64-windows\bin'),
        "$env:SystemRoot\System32",
        $env:SystemRoot
    ) -join ';')
    $process = Start-Process -FilePath $Runner -ArgumentList $arguments `
        -WorkingDirectory $RuntimeRoot -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $process.Refresh()
    $worker = Get-Process -Id $PID
    $state = [ordered]@{
        status = "running_$Scenario"
        stopFile = $StopFile
        processes = @(
            (Get-ProcessRecord 'week10-performance-worker' $worker `
                (Join-Path $PSHOME 'powershell.exe')),
            (Get-ProcessRecord "week10-$Scenario-runner" $process $Runner)
        )
        sameMachineSoftwareQualified = $false
        physicalLanQualified = $false
        performanceQualified = $false
    }
    Write-WorkerState $state

    $samples = [Collections.Generic.List[object]]::new()
    [double]$previousCpu = $process.TotalProcessorTime.TotalSeconds
    $previousAt = [DateTime]::UtcNow
    while (-not $process.HasExited) {
        Start-Sleep -Seconds 1
        $process.Refresh()
        if ($process.HasExited) { break }
        $now = [DateTime]::UtcNow
        [double]$seconds = [Math]::Max(0.001, ($now - $previousAt).TotalSeconds)
        [double]$totalCpu = $process.TotalProcessorTime.TotalSeconds
        [double]$cpu = [Math]::Max(
            0.0,
            (($totalCpu - $previousCpu) / $seconds /
             [Environment]::ProcessorCount) * 100.0
        )
        $previousCpu = $totalCpu
        $previousAt = $now
        $sampleValue = [pscustomobject][ordered]@{
            elapsedSeconds = [int](($now -
                $process.StartTime.ToUniversalTime()).TotalSeconds)
            workingSetBytes = [int64]$process.WorkingSet64
            processCpuPercent = [Math]::Round($cpu, 3)
        }
        [void]$samples.Add($sampleValue)
        ($sampleValue | ConvertTo-Json -Compress) |
            Add-Content -LiteralPath $processSamplesPath -Encoding UTF8
    }
    $process.WaitForExit()
    $process.Refresh()
    $exitCode = $process.ExitCode

    $runnerValues = [Collections.Generic.List[object]]::new()
    if (Test-Path -LiteralPath $stdout -PathType Leaf) {
        foreach ($line in Get-Content -LiteralPath $stdout) {
            try {
                [void]$runnerValues.Add(
                    ($line | ConvertFrom-Json -ErrorAction Stop)
                )
            } catch { }
        }
    }
    $runnerSamples = @($runnerValues | Where-Object { $_.type -eq 'sample' })
    $runnerResult = @($runnerValues | Where-Object { $_.type -eq 'result' } |
        Select-Object -Last 1)
    $measurementSamples = @($samples | Select-Object -Last (
        [Math]::Min($SampleSeconds, $samples.Count)
    ))
    $firstWindow = @($measurementSamples | Select-Object -First (
        [Math]::Min(60, $measurementSamples.Count)
    ))
    $lastWindow = @($measurementSamples | Select-Object -Last (
        [Math]::Min(60, $measurementSamples.Count)
    ))
    [double]$firstMean = if ($firstWindow.Count) {
        ($firstWindow.workingSetBytes | Measure-Object -Average).Average
    } else { 0 }
    [double]$lastMean = if ($lastWindow.Count) {
        ($lastWindow.workingSetBytes | Measure-Object -Average).Average
    } else { 0 }
    [double]$growthMiB = ($lastMean - $firstMean) / 1MB
    [double]$slope = Get-LinearSlopeMiBPerMinute $measurementSamples
    [double[]]$cpuValues = @($measurementSamples | ForEach-Object {
        [double]$_.processCpuPercent
    })
    [double]$cpuMean = if ($cpuValues.Count) {
        ($cpuValues | Measure-Object -Average).Average
    } else { 0 }
    [double]$cpuP95 = Get-Percentile $cpuValues 0.95
    [double]$cpuMax = if ($cpuValues.Count) {
        ($cpuValues | Measure-Object -Maximum).Maximum
    } else { 0 }
    $sensitive = Test-SensitiveOutput @($stdout,$stderr)
    $official = ($Scenario -eq 'single' -and $WarmupSeconds -eq 60 -and
        $SampleSeconds -eq 600) -or
        ($Scenario -eq 'four' -and $WarmupSeconds -eq 60 -and
         $SampleSeconds -eq 1800 -and $StopSecond -eq 600 -and
         $RebuildSecond -eq 720)
    $memoryPassed = -not $official -or
        ($slope -le 2.0 -and $growthMiB -le 64.0)
    $sampleCountPassed = $runnerSamples.Count -ge
        [Math]::Max(1, $WarmupSeconds + $SampleSeconds - 2)
    $runnerPassed = $runnerResult.Count -eq 1 -and
        [bool]$runnerResult[0].passed -and [bool]$runnerResult[0].queuesPassed
    $passed = $exitCode -eq 0 -and $runnerPassed -and $memoryPassed -and
        $sampleCountPassed -and [bool]$sensitive.passed
    return [pscustomobject][ordered]@{
        scenario = $Scenario
        passed = $passed
        officialDuration = $official
        exitCode = $exitCode
        runnerSampleCount = $runnerSamples.Count
        processSampleCount = $samples.Count
        workingSetSlopeMiBPerMinute = [Math]::Round($slope, 3)
        workingSetLastVsFirst60MiB = [Math]::Round($growthMiB, 3)
        memoryPassed = $memoryPassed
        processCpuMeanPercent = [Math]::Round($cpuMean, 3)
        processCpuP95Percent = [Math]::Round($cpuP95, 3)
        processCpuMaxPercent = [Math]::Round($cpuMax, 3)
        queuesPassed = $runnerPassed -and [bool]$runnerResult[0].queuesPassed
        continuityPassed = $runnerPassed -and [bool]$runnerResult[0].continuityPassed
        recoveryWithinTenSeconds = $runnerPassed -and
            [bool]$runnerResult[0].recoveryWithinTenSeconds
        oldPortRejected = $runnerPassed -and [bool]$runnerResult[0].oldPortRejected
        cleanupPassed = $runnerPassed -and [bool]$runnerResult[0].cleanupPassed
        sensitiveOutputPassed = [bool]$sensitive.passed
        sensitiveOutputCategories = @($sensitive.categories)
    }
}

$workerStage = 'single_scenario'
try {
    $single = Invoke-Scenario 'single' $SingleWarmupSeconds $SingleSampleSeconds
    if (Test-Path -LiteralPath $StopFile -PathType Leaf) {
        Write-WorkerState ([ordered]@{
            status = 'stopped'; single = $single
            sameMachineSoftwareQualified = $false
            physicalLanQualified = $false; performanceQualified = $false
        })
        exit 0
    }
    $workerStage = 'four_scenario'
    $four = Invoke-Scenario 'four' $FourWarmupSeconds $FourSampleSeconds
    if (Test-Path -LiteralPath $StopFile -PathType Leaf) {
        Write-WorkerState ([ordered]@{
            status = 'stopped'; single = $single; four = $four
            sameMachineSoftwareQualified = $false
            physicalLanQualified = $false; performanceQualified = $false
        })
        exit 0
    }
    $workerStage = 'final_result'
    $functionalPassed = [bool]$single.passed -and [bool]$four.passed
    $localPassed = $functionalPassed -and [bool]$single.officialDuration -and
        [bool]$four.officialDuration
    Write-WorkerState ([ordered]@{
        status = $(if($localPassed){'passed'}elseif($functionalPassed){
            'self_test_passed'
        }else{'failed'})
        single = $single
        four = $four
        w9ResourcePassed = [bool]$four.passed -and [bool]$four.officialDuration
        w9Gate = $(if([bool]$four.passed -and [bool]$four.officialDuration){
            'blocked(camera_environment)'
        }else{'blocked(camera_environment,resource_smoke_not_run)'})
        sameMachineSoftwareQualified = $localPassed
        localPerformanceQualified = $localPassed
        physicalLanQualified = $false
        performanceQualified = $false
        cameraQualified = $false
    })
    if (-not $functionalPassed) { exit 1 }
} catch {
    Write-WorkerState ([ordered]@{
        status = 'failed'
        reason = 'worker_failure'
        stage = $workerStage
        errorCategory = [string]$_.CategoryInfo.Category
        errorId = [string]$_.FullyQualifiedErrorId
        errorLine = [int]$_.InvocationInfo.ScriptLineNumber
        errorType = [string]$_.Exception.GetType().Name
        sameMachineSoftwareQualified = $false
        physicalLanQualified = $false
        performanceQualified = $false
    })
    exit 1
}
