[CmdletBinding()]
param(
    [ValidateSet('Check','SelfTest','Run','Status','Stop')]
    [string]$Action = 'Check',
    [ValidateSet('publisher','viewer')][string]$MediaRole,
    [ValidateSet('offer','answer')][string]$SignalingRole,
    [ValidateSet('normal','viewer-first','publisher-first')]
    [string]$Lifecycle = 'normal',
    [ValidateSet('PC-A','PC-B')][string]$EnvironmentId = 'PC-A',
    [int]$Rounds = 10,
    [int]$TimeoutSeconds = 75
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$packageRoot = [IO.Path]::GetFullPath($PSScriptRoot)
Import-Module (Join-Path $packageRoot 'QualificationCommon.psm1') -Force
Import-Module (Join-Path $packageRoot 'Week6LanCommon.psm1') -Force
Import-Module (Join-Path $packageRoot 'WebRtcHandoffCommon.psm1') -Force
$manifestPath = Join-Path $packageRoot 'package-manifest.json'
$clientPath = Join-Path $packageRoot 'rtmp_monitor_webrtc_client.exe'
$exchangeRoot = Join-Path $packageRoot 'session-exchange'
$handoffRoot = Join-Path $packageRoot 'handoff'
$inboxRoot = Join-Path $handoffRoot 'inbox'
$outboxRoot = Join-Path $handoffRoot 'outbox'
$logRoot = Join-Path $packageRoot 'logs'
$resultRoot = Join-Path $packageRoot 'results'
$statePath = Join-Path $packageRoot 'week6-lan-state.json'

function Assert-PackageReady {
    $manifest = Read-Week6Manifest -Path $manifestPath
    foreach ($path in @(
            $clientPath,
            (Join-Path $packageRoot 'webrtc-assets\sample.mp4'),
            (Join-Path $packageRoot 'platforms\qwindows.dll'),
            (Join-Path $packageRoot 'platforms\qoffscreen.dll'))) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required portable file is missing: $([IO.Path]::GetFileName($path))"
        }
    }
    return $manifest
}

function Clear-RoundFiles {
    Clear-WebRtcHandoffFiles -Roots @(
        $exchangeRoot,$inboxRoot,$outboxRoot
    ) -ManagedRoot $packageRoot
}

function Sync-Handoff {
    Sync-WebRtcHandoffFiles -ExchangeRoot $exchangeRoot `
        -InboxRoot $inboxRoot -OutboxRoot $outboxRoot
}

function Read-Event {
    param([string]$Path, [string]$Name)
    return Read-WebRtcJsonEvent -Path $Path -Name $Name
}

function Get-JsonProperty {
    param($Object, [string]$Name, $Default)
    if ($null -ne $Object -and
        $null -ne $Object.PSObject.Properties[$Name]) {
        return $Object.$Name
    }
    return $Default
}

function Invoke-Run {
    if ([string]::IsNullOrWhiteSpace($MediaRole) -or
        [string]::IsNullOrWhiteSpace($SignalingRole)) {
        throw 'Run requires -MediaRole and -SignalingRole.'
    }
    if ($Rounds -lt 1 -or $Rounds -gt 20) { throw 'Rounds must be 1..20.' }
    if ($Lifecycle -ne 'normal' -and $Rounds -ne 1) {
        throw 'Lifecycle scenarios must use -Rounds 1.'
    }
    $manifest = Assert-PackageReady
    if (Test-Path -LiteralPath $statePath -PathType Leaf) {
        throw 'A package runner state exists. Use -Action Status or Stop.'
    }
    New-Item -ItemType Directory -Force -Path $logRoot,$resultRoot | Out-Null
    $roundResults = [System.Collections.Generic.List[object]]::new()
    $passed = 0
    $previousPlatform = $env:QT_QPA_PLATFORM
    $env:QT_QPA_PLATFORM = 'offscreen'
    try {
        foreach ($round in 1..$Rounds) {
            Clear-RoundFiles
            $stdout = Join-Path $logRoot `
                "$MediaRole-$SignalingRole-$round.stdout.jsonl"
            $stderr = Join-Path $logRoot `
                "$MediaRole-$SignalingRole-$round.stderr.txt"
            $arguments = @('--media-role',$MediaRole,
                '--signaling-role',$SignalingRole,
                '--timeout-ms',([string]($TimeoutSeconds * 1000)))
            if ($MediaRole -eq 'publisher') { $arguments += @('--source','sample') }
            $process = Start-Process -FilePath $clientPath `
                -ArgumentList $arguments -WorkingDirectory $packageRoot `
                -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout `
                -RedirectStandardError $stderr
            $process.Refresh()
            Write-QualificationState -StatePath $statePath -RuntimeRoot $packageRoot `
                -Processes @([pscustomobject]@{
                    name = "$MediaRole-$SignalingRole-$round"
                    pid = $process.Id; path = $clientPath
                    startTimeUtc = $process.StartTime.ToUniversalTime().ToString('o')
                    stdout = $stdout; stderr = $stderr
                })
            $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds + 15)
            $stopThisSide =
                ($Lifecycle -eq 'viewer-first' -and $MediaRole -eq 'viewer') -or
                ($Lifecycle -eq 'publisher-first' -and $MediaRole -eq 'publisher')
            $closeAt = $null
            $intentionalStop = $false
            while (-not $process.HasExited -and [DateTime]::UtcNow -lt $deadline) {
                Sync-Handoff
                if ($stopThisSide -and $null -eq $closeAt) {
                    $triggerName = if ($MediaRole -eq 'viewer') {
                        'frame_presented'
                    } else { 'publishing' }
                    if (Read-Event -Path $stdout -Name $triggerName) {
                        $closeAt = [DateTime]::UtcNow.AddSeconds(2)
                    }
                }
                if ($null -ne $closeAt -and
                    [DateTime]::UtcNow -ge $closeAt) {
                    $intentionalStop = $true
                    [void]$process.CloseMainWindow()
                    if (-not $process.WaitForExit(2000)) {
                        Stop-Process -Id $process.Id -Force
                    }
                }
                Start-Sleep -Milliseconds 100
                $process.Refresh()
            }
            if (-not $process.HasExited) { throw "Round $round timed out." }
            Sync-Handoff
            if ($process.ExitCode -ne 0 -and $Lifecycle -eq 'normal' -and
                -not $intentionalStop) {
                throw "Round $round client exit code was $($process.ExitCode)."
            }
            $connected = Read-Event -Path $stdout -Name 'connected'
            $completed = Read-Event -Path $stdout -Name 'completed'
            $ready = Read-Event -Path $stdout -Name 'runtime_ready'
            if (-not $ready -or -not $connected -or
                ($Lifecycle -eq 'normal' -and -not $completed) -or
                [string]$ready.layout -ne 'portable') {
                throw "Round $round did not produce portable connection evidence."
            }
            $pair = Get-JsonProperty -Object $connected `
                -Name 'selectedCandidatePair' -Default $null
            $localType = [string](Get-JsonProperty $pair 'localType' '')
            $remoteType = [string](Get-JsonProperty $pair 'remoteType' '')
            $localTransport = [string](Get-JsonProperty $pair 'localTransport' '')
            $remoteTransport = [string](Get-JsonProperty $pair 'remoteTransport' '')
            $media = Read-Event -Path $stdout -Name 'media_received'
            $decodedEvent = Read-Event -Path $stdout -Name 'frame_decoded'
            $presentedEvent = Read-Event -Path $stdout -Name 'frame_presented'
            $receivedRtp = [int64](Get-JsonProperty $completed `
                'receivedRtpPackets' (Get-JsonProperty $media `
                    'receivedRtpPackets' 0))
            $receivedAu = [int64](Get-JsonProperty $completed `
                'receivedAccessUnits' (Get-JsonProperty $media `
                    'receivedAccessUnits' 0))
            $decoded = [bool](Get-JsonProperty $completed 'decoded' `
                ($null -ne $decodedEvent))
            $presented = [bool](Get-JsonProperty $completed 'presented' `
                ($null -ne $presentedEvent))
            $roundPassed = $null -ne $pair -and
                $localType -eq 'host' -and $remoteType -eq 'host' -and
                $localTransport -eq 'udp' -and $remoteTransport -eq 'udp'
            if ($MediaRole -eq 'viewer') {
                $roundPassed = $roundPassed -and
                    $receivedRtp -gt 0 -and $receivedAu -gt 0 -and
                    $decoded -and $presented
            }
            if ($Lifecycle -ne 'normal') {
                $roundPassed = $roundPassed -and
                    ($stopThisSide -or
                     $null -ne (Read-Event -Path $stdout `
                         -Name 'connection_lost') -or
                     $process.ExitCode -ne 0)
            }
            if ($roundPassed) { $passed++ }
            [void]$roundResults.Add([ordered]@{
                round = $round; passed = $roundPassed
                localType = $localType; remoteType = $remoteType
                localTransport = $localTransport
                remoteTransport = $remoteTransport
                receivedRtpPackets = $receivedRtp
                receivedAccessUnits = $receivedAu
                decoded = $decoded; presented = $presented
            })
            Remove-Item -LiteralPath $statePath -Force -ErrorAction SilentlyContinue
        }
        Clear-RoundFiles
        $report = [ordered]@{
            schemaVersion = 1; packageId = [string]$manifest.packageId
            environmentId = $EnvironmentId
            os = [Environment]::OSVersion.VersionString
            architecture = $env:PROCESSOR_ARCHITECTURE
            mediaRole = $MediaRole; signalingRole = $SignalingRole
            lifecycle = $Lifecycle
            roundsRequested = $Rounds; roundsPassed = $passed
            cleanupPassed = $true; rounds = @($roundResults)
        }
        $reportPath = Join-Path $resultRoot `
            "$EnvironmentId-$MediaRole-$SignalingRole-$Lifecycle.json"
        $report | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $reportPath -Encoding UTF8
        if (-not (Test-Week6LanReport -Report ($report | ConvertTo-Json `
                    -Depth 8 | ConvertFrom-Json))) {
            throw 'LAN report did not satisfy the Week 6 gate contract.'
        }
        Write-Host "Week 6 LAN side passed: $reportPath"
    } finally {
        if (Test-Path -LiteralPath $statePath -PathType Leaf) {
            Stop-QualificationOwnedProcesses -StatePath $statePath `
                -RuntimeRoot $packageRoot
        }
        if ($null -eq $previousPlatform) {
            Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
        } else { $env:QT_QPA_PLATFORM = $previousPlatform }
    }
}

switch ($Action) {
    'Check' { [void](Assert-PackageReady); Write-Host 'Week 6 package check passed.' }
    'SelfTest' {
        $manifest = Assert-PackageReady
        if ([string]$manifest.configuration -ne 'Release') {
            throw 'Manifest configuration self-test failed.'
        }
        Write-Host 'Week 6 package runner self-test passed.'
    }
    'Status' {
        $state = Read-QualificationState -StatePath $statePath
        if (-not $state) { Write-Host 'Week 6 package runner is idle.' }
        else { $state.processes | Format-Table name,pid,startTimeUtc }
    }
    'Stop' {
        Stop-QualificationOwnedProcesses -StatePath $statePath `
            -RuntimeRoot $packageRoot
        Clear-RoundFiles
        Write-Host 'Week 6 package runner stopped and handoff files cleared.'
    }
    'Run' { Invoke-Run }
}
