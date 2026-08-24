[CmdletBinding()]
param(
    [ValidateSet('Configure','Check','SelfTest','Run','Status','Stop')]
    [string]$Action = 'Check',
    [ValidateSet('publisher','viewer')][string]$MediaRole,
    [ValidateSet('offer','answer')][string]$SignalingRole,
    [ValidateSet('company','mobile')][string]$NetworkClass,
    [ValidateSet('normal','viewer-first','publisher-first')]
    [string]$Lifecycle = 'normal',
    [int]$Rounds = 10,
    [int]$TimeoutSeconds = 90
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$packageRoot = [IO.Path]::GetFullPath($PSScriptRoot)
Import-Module (Join-Path $packageRoot 'QualificationCommon.psm1') -Force
Import-Module (Join-Path $packageRoot 'WebRtcHandoffCommon.psm1') -Force
Import-Module (Join-Path $packageRoot 'Week7QualificationCommon.psm1') -Force

$manifestPath = Join-Path $packageRoot 'package-manifest.json'
$clientPath = Join-Path $packageRoot 'rtmp_monitor_webrtc_client.exe'
$exchangeRoot = Join-Path $packageRoot 'session-exchange'
$inboxRoot = Join-Path $packageRoot 'handoff\inbox'
$outboxRoot = Join-Path $packageRoot 'handoff\outbox'
$logRoot = Join-Path $packageRoot 'logs'
$resultRoot = Join-Path $packageRoot 'results'
$configRoot = Join-Path $packageRoot 'local-config'
$configPath = Join-Path $configRoot 'ice-runtime.json'
$authorizationPath = Join-Path $configRoot 'authorization.json'
$statePath = Join-Path $packageRoot 'week7-public-state.json'

function Get-Value {
    param($Object,[string]$Name,$Default)
    if ($null -ne $Object -and
        $null -ne $Object.PSObject.Properties[$Name]) { return $Object.$Name }
    return $Default
}

function Assert-PackageReady {
    $manifest = Read-Week7Manifest -Path $manifestPath
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

function Clear-SessionFiles {
    Clear-WebRtcHandoffFiles -Roots @(
        $exchangeRoot,$inboxRoot,$outboxRoot
    ) -ManagedRoot $packageRoot
}

function Invoke-Configure {
    [void](Assert-PackageReady)
    Write-Host 'The STUN service and both networks must be authorized for this test.'
    $confirmation = Read-Host 'Type AUTHORIZED to continue'
    if ($confirmation -cne 'AUTHORIZED') { throw 'Authorization was not confirmed.' }
    $url = Read-Host 'Enter the authorized STUN URL (stun:host:port)'
    $candidate = [pscustomobject]@{ schemaVersion=1; stunUrl=$url }
    if (-not (Test-Week7IceConfigValue -Value $candidate)) {
        throw 'The STUN URL is invalid. TURN, credentials and placeholders are rejected.'
    }
    New-Item -ItemType Directory -Force -Path $configRoot | Out-Null
    $candidate | ConvertTo-Json -Compress |
        Set-Content -LiteralPath $configPath -Encoding UTF8
    [ordered]@{ schemaVersion=1; authorized=$true; configuredUtc=[DateTime]::UtcNow.ToString('o') } |
        ConvertTo-Json -Compress |
        Set-Content -LiteralPath $authorizationPath -Encoding UTF8
    $url = $null
    Write-Host 'Authorized STUN configuration stored only in this package runtime directory.'
}

function Assert-Configured {
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf) -or
        -not (Test-Path -LiteralPath $authorizationPath -PathType Leaf)) {
        throw 'Run Configure first.'
    }
    $value = Get-Content -LiteralPath $configPath -Raw -Encoding UTF8 |
        ConvertFrom-Json
    if (-not (Test-Week7IceConfigValue -Value $value)) {
        throw 'The local ICE configuration is invalid.'
    }
}

function Invoke-Run {
    $manifest = Assert-PackageReady
    Assert-Configured
    if ([string]::IsNullOrWhiteSpace($MediaRole) -or
        [string]::IsNullOrWhiteSpace($SignalingRole) -or
        [string]::IsNullOrWhiteSpace($NetworkClass)) {
        throw 'Run requires MediaRole, SignalingRole and NetworkClass.'
    }
    if ($Rounds -lt 1 -or $Rounds -gt 20) { throw 'Rounds must be 1..20.' }
    if ($Lifecycle -ne 'normal' -and $Rounds -ne 1) {
        throw 'Lifecycle scenarios must use one round.'
    }
    if (Test-Path -LiteralPath $statePath -PathType Leaf) {
        throw 'A Week 7 runner is active. Use Status or Stop.'
    }
    New-Item -ItemType Directory -Force -Path $logRoot,$resultRoot | Out-Null
    $results = [System.Collections.Generic.List[object]]::new()
    $records = [System.Collections.Generic.List[object]]::new()
    $previousPlatform = $env:QT_QPA_PLATFORM
    $env:QT_QPA_PLATFORM = 'offscreen'
    try {
        foreach ($round in 1..$Rounds) {
            Clear-SessionFiles
            $arguments = @(
                '--media-role',$MediaRole,'--signaling-role',$SignalingRole,
                '--ice-mode','stun','--timeout-ms',([string]($TimeoutSeconds * 1000)))
            if ($MediaRole -eq 'publisher') { $arguments += @('--source','sample') }
            $name = "$MediaRole-$SignalingRole-$round"
            $process = Start-QualificationOwnedProcess -Name $name `
                -FilePath $clientPath -Arguments $arguments `
                -WorkingDirectory $packageRoot -LogRoot $logRoot `
                -RuntimeRoot $packageRoot -StatePath $statePath `
                -Records $records
            $stdout = Join-Path $logRoot "$name.stdout.jsonl"
            $stderr = Join-Path $logRoot "$name.stderr.txt"
            $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds + 20)
            $stopThisSide =
                ($Lifecycle -eq 'viewer-first' -and $MediaRole -eq 'viewer') -or
                ($Lifecycle -eq 'publisher-first' -and $MediaRole -eq 'publisher')
            $intentionalStop = $false
            while (-not $process.HasExited -and [DateTime]::UtcNow -lt $deadline) {
                Sync-WebRtcHandoffFiles -ExchangeRoot $exchangeRoot `
                    -InboxRoot $inboxRoot -OutboxRoot $outboxRoot
                if ($stopThisSide -and -not $intentionalStop) {
                    $trigger = if ($MediaRole -eq 'viewer') {
                        Read-WebRtcJsonEvent -Path $stdout -Name 'frame_presented'
                    } else {
                        Read-WebRtcJsonEvent -Path $stdout -Name 'publishing'
                    }
                    if ($null -ne $trigger) {
                        $intentionalStop = $true
                        [void]$process.CloseMainWindow()
                        if (-not $process.WaitForExit(2000)) {
                            Stop-Process -Id $process.Id -Force
                        }
                    }
                }
                Start-Sleep -Milliseconds 100
                $process.Refresh()
            }
            if (-not $process.HasExited) { throw "Round $round timed out." }
            Sync-WebRtcHandoffFiles -ExchangeRoot $exchangeRoot `
                -InboxRoot $inboxRoot -OutboxRoot $outboxRoot

            $ready = Read-WebRtcJsonEvent -Path $stdout -Name 'runtime_ready'
            $loaded = Read-WebRtcJsonEvent -Path $stdout -Name 'ice_config_loaded'
            $gathered = Read-WebRtcJsonEvent -Path $stdout -Name 'ice_gathering_completed'
            $connected = Read-WebRtcJsonEvent -Path $stdout -Name 'connected'
            $completed = Read-WebRtcJsonEvent -Path $stdout -Name 'completed'
            $media = Read-WebRtcJsonEvent -Path $stdout -Name 'media_received'
            $decodedEvent = Read-WebRtcJsonEvent -Path $stdout -Name 'frame_decoded'
            $presentedEvent = Read-WebRtcJsonEvent -Path $stdout -Name 'frame_presented'
            $pair = Get-Value $connected 'selectedCandidatePair' $null
            $roundValue = [ordered]@{
                round=$round; mediaRole=$MediaRole
                passed=(($process.ExitCode -eq 0 -or $intentionalStop) -and
                    $null -ne $connected)
                stunObservation=[string](Get-Value $gathered 'stunObservation' '')
                iceState=[string](Get-Value $connected 'iceState' `
                    (Get-Value $gathered 'iceState' ''))
                localType=[string](Get-Value $pair 'localType' '')
                remoteType=[string](Get-Value $pair 'remoteType' '')
                localTransport=[string](Get-Value $pair 'localTransport' '')
                remoteTransport=[string](Get-Value $pair 'remoteTransport' '')
                nonRelayPairPresent=($null -ne $pair -and
                    [string](Get-Value $pair 'localType' '') -ne 'relay' -and
                    [string](Get-Value $pair 'remoteType' '') -ne 'relay')
                receivedRtpPackets=[int64](Get-Value $completed 'receivedRtpPackets' `
                    (Get-Value $media 'receivedRtpPackets' 0))
                receivedAccessUnits=[int64](Get-Value $completed 'receivedAccessUnits' `
                    (Get-Value $media 'receivedAccessUnits' 0))
                submittedAccessUnits=[int64](Get-Value $completed 'submittedAccessUnits' `
                    (Get-Value $media 'submittedAccessUnits' 0))
                decoded=($null -ne $decodedEvent -and
                    [bool](Get-Value $completed 'decoded' $false))
                rendered=($null -ne $presentedEvent -and
                    [int64](Get-Value $presentedEvent 'renderedFrames' 0) -gt 0)
                presented=($null -ne $presentedEvent -and
                    [bool](Get-Value $completed 'presented' $false))
                nonBlack=($null -ne $presentedEvent)
                cleanupPassed=$true
                failureClass=if ($process.ExitCode -eq 0) { '' } else { 'inconclusive' }
            }
            $roundValue.passed = [bool]$roundValue.passed -and
                $null -ne $ready -and [string]$ready.iceMode -eq 'stun' -and
                $null -ne $loaded -and [int]$loaded.serverCount -eq 1 -and
                (($Lifecycle -ne 'normal' -and $intentionalStop) -or
                 (Test-Week7Round -Round ([pscustomobject]$roundValue)))
            [void]$results.Add($roundValue)
            Assert-QualificationSafeLogs -Paths @($stdout,$stderr)
            $records.Clear()
            Remove-Item -LiteralPath $statePath -Force -ErrorAction SilentlyContinue
        }
        Clear-SessionFiles
        $report = [ordered]@{
            schemaVersion=1; packageId=[string]$manifest.packageId
            mediaRole=$MediaRole; signalingRole=$SignalingRole
            networkClass=$NetworkClass; lifecycle=$Lifecycle
            roundsRequested=$Rounds
            roundsPassed=@($results | Where-Object { [bool]$_.passed }).Count
            cleanupPassed=$true; rounds=@($results)
        }
        $reportPath = Join-Path $resultRoot `
            "$NetworkClass-$MediaRole-$SignalingRole-$Lifecycle.json"
        $report | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $reportPath -Encoding UTF8
        Write-Host "Week 7 public-test side completed: $reportPath"
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
    'Configure' { Invoke-Configure }
    'Check' { [void](Assert-PackageReady); Write-Host 'Week 7 package check passed.' }
    'SelfTest' {
        [void](Assert-PackageReady)
        [void][scriptblock]::Create((Get-Content -LiteralPath $PSCommandPath -Raw))
        $valid = [pscustomobject]@{ schemaVersion=1; stunUrl='stun:stun-host.invalid:3478' }
        if (-not (Test-Week7IceConfigValue $valid)) { throw 'Config self-test failed.' }
        $invalid = [pscustomobject]@{ schemaVersion=1; stunUrl='turn:relay.invalid:3478' }
        if (Test-Week7IceConfigValue $invalid) { throw 'TURN rejection self-test failed.' }
        Write-Host 'Week 7 package runner self-test passed.'
    }
    'Run' { Invoke-Run }
    'Status' {
        $state = Read-QualificationState -StatePath $statePath
        if ($null -eq $state) { Write-Host 'Week 7 package runner is idle.' }
        else { $state.processes | Format-Table name,pid,startTimeUtc }
    }
    'Stop' {
        Stop-QualificationOwnedProcesses -StatePath $statePath `
            -RuntimeRoot $packageRoot
        Clear-SessionFiles
        Remove-Item -LiteralPath $configPath,$authorizationPath `
            -Force -ErrorAction SilentlyContinue
        Write-Host 'Week 7 runner stopped; session and local ICE files removed.'
    }
}
