Set-StrictMode -Version Latest

$script:Week7PlanVersion = '0.3.0-beta.1'
$script:Week7ApplicationVersion = '0.1.0-alpha.1'

function ConvertTo-Week7RelativePath {
    param([string]$Root,[string]$Path)
    $rootUri = [Uri](([IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'))
    $pathUri = [Uri][IO.Path]::GetFullPath($Path)
    return [Uri]::UnescapeDataString(
        $rootUri.MakeRelativeUri($pathUri).ToString()
    ).Replace('/','\')
}

function New-Week7PackageManifest {
    param(
        [Parameter(Mandatory = $true)][string]$StageRoot,
        [Parameter(Mandatory = $true)][string]$SourceCommit,
        [Parameter(Mandatory = $true)]$SampleStream
    )
    $files = @(Get-ChildItem -LiteralPath $StageRoot -File -Recurse |
        Where-Object { $_.Name -ne 'package-manifest.json' } |
        Sort-Object FullName | ForEach-Object {
            [ordered]@{
                path = ConvertTo-Week7RelativePath -Root $StageRoot `
                    -Path $_.FullName
                size = [int64]$_.Length
            }
        })
    return [ordered]@{
        schemaVersion = 1
        packageId = "webrtc-v2-week7-$($SourceCommit.Substring(0,12))"
        planVersion = $script:Week7PlanVersion
        applicationVersion = $script:Week7ApplicationVersion
        sourceCommit = $SourceCommit
        architecture = 'windows-x64'
        configuration = 'Release'
        localConfigurationIncluded = $false
        sample = [ordered]@{
            path = 'webrtc-assets\sample.mp4'
            codec = [string]$SampleStream.codec_name
            profile = [string]$SampleStream.profile
            level = [int]$SampleStream.level
            width = [int]$SampleStream.width
            height = [int]$SampleStream.height
            frameRate = [string]$SampleStream.r_frame_rate
            hasBFrames = [int]$SampleStream.has_b_frames
        }
        files = $files
    }
}

function Read-Week7Manifest {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Week 7 manifest was not found: $Path"
    }
    $value = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 |
        ConvertFrom-Json
    if ([int]$value.schemaVersion -ne 1 -or
        [string]$value.planVersion -ne $script:Week7PlanVersion -or
        [bool]$value.localConfigurationIncluded -or
        [string]::IsNullOrWhiteSpace([string]$value.packageId)) {
        throw 'Week 7 manifest contract is invalid.'
    }
    return $value
}

function Test-Week7IceConfigValue {
    param([Parameter(Mandatory = $true)]$Value)
    if ($null -eq $Value -or
        @($Value.PSObject.Properties.Name).Count -ne 2 -or
        'schemaVersion' -notin @($Value.PSObject.Properties.Name) -or
        'stunUrl' -notin @($Value.PSObject.Properties.Name) -or
        [int]$Value.schemaVersion -ne 1) { return $false }
    $url = [string]$Value.stunUrl
    return $url -match '^stun:[^<>\s:@]+:\d{1,5}$' -and
        $url.Length -le 512 -and $url -notmatch '(?i)turn|user|pass'
}

function Test-Week7Round {
    param([Parameter(Mandatory = $true)]$Round)
    if (-not [bool]$Round.passed -or
        [string]$Round.stunObservation -ne 'srflx_observed' -or
        [string]$Round.localType -notin @('host','srflx') -or
        [string]$Round.remoteType -notin @('host','srflx') -or
        [string]$Round.localTransport -ne 'udp' -or
        [string]$Round.remoteTransport -ne 'udp' -or
        -not [bool]$Round.cleanupPassed) { return $false }
    if ([string]$Round.mediaRole -eq 'viewer') {
        return [int64]$Round.receivedRtpPackets -gt 0 -and
            [int64]$Round.receivedAccessUnits -gt 0 -and
            [int64]$Round.submittedAccessUnits -gt 0 -and
            [bool]$Round.decoded -and [bool]$Round.rendered -and
            [bool]$Round.presented -and [bool]$Round.nonBlack
    }
    return $true
}

function Resolve-Week7PublicResult {
    param([Parameter(Mandatory = $true)][object[]]$Reports)
    $normalReports = @($Reports | Where-Object {
        [string]$_.lifecycle -eq 'normal'
    })
    if ($normalReports.Count -ne 4) { return 'Inconclusive' }
    $packageIds = @($Reports | ForEach-Object { [string]$_.packageId } |
        Select-Object -Unique)
    if ($packageIds.Count -ne 1) { return 'ConfigurationError' }
    $keys = @($normalReports | ForEach-Object {
        "$([string]$_.mediaRole):$([string]$_.signalingRole)"
    })
    foreach ($required in @(
            'publisher:offer','viewer:answer',
            'viewer:offer','publisher:answer')) {
        if (@($keys | Where-Object { $_ -eq $required }).Count -ne 1) {
            return 'RoleRegression'
        }
    }
    foreach ($report in $Reports) {
        if ([string]$report.networkClass -notin @('company','mobile') -or
            -not [bool]$report.cleanupPassed -or
            [int]$report.roundsRequested -ne @($report.rounds).Count) {
            return 'ConfigurationError'
        }
    }
    $rounds = @($normalReports | ForEach-Object { @($_.rounds) })
    if ($rounds.Count -eq 0) { return 'Inconclusive' }
    if (@($rounds | Where-Object { Test-Week7Round $_ }).Count -eq
        $rounds.Count) { return 'Direct' }

    $eligibleRelay = $true
    foreach ($round in $rounds) {
        if ([string]$round.stunObservation -ne 'srflx_observed' -or
            [string]$round.iceState -ne 'failed' -or
            [string]$round.failureClass -ne 'ice_checks_failed' -or
            [bool]$round.nonRelayPairPresent) {
            $eligibleRelay = $false
        }
    }
    if ($eligibleRelay) { return 'NeedsRelay' }
    return 'Inconclusive'
}

Export-ModuleMember -Function @(
    'ConvertTo-Week7RelativePath',
    'New-Week7PackageManifest',
    'Read-Week7Manifest',
    'Test-Week7IceConfigValue',
    'Test-Week7Round',
    'Resolve-Week7PublicResult'
)
