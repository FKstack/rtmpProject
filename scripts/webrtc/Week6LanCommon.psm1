Set-StrictMode -Version Latest

$script:Week6PlanVersion = '0.2.0-beta.1'
$script:Week6ApplicationVersion = '0.1.0-alpha.1'

function ConvertTo-Week6RelativePath {
    param([string]$Root, [string]$Path)
    $rootUri = [Uri](([IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'))
    $pathUri = [Uri][IO.Path]::GetFullPath($Path)
    return [Uri]::UnescapeDataString(
        $rootUri.MakeRelativeUri($pathUri).ToString()
    ).Replace('/', '\')
}

function New-Week6PackageManifest {
    param(
        [Parameter(Mandatory = $true)][string]$StageRoot,
        [Parameter(Mandatory = $true)][string]$SourceCommit,
        [Parameter(Mandatory = $true)]$SampleStream
    )
    $files = @(Get-ChildItem -LiteralPath $StageRoot -File -Recurse |
        Where-Object { $_.Name -ne 'package-manifest.json' } |
        Sort-Object FullName | ForEach-Object {
            [ordered]@{
                path = ConvertTo-Week6RelativePath -Root $StageRoot `
                    -Path $_.FullName
                size = [int64]$_.Length
            }
        })
    return [ordered]@{
        schemaVersion = 1
        packageId = "webrtc-v2-week6-$($SourceCommit.Substring(0, 12))"
        planVersion = $script:Week6PlanVersion
        applicationVersion = $script:Week6ApplicationVersion
        sourceCommit = $SourceCommit
        architecture = 'windows-x64'
        configuration = 'Release'
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

function Read-Week6Manifest {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Week 6 manifest was not found: $Path"
    }
    $value = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 |
        ConvertFrom-Json
    if ([int]$value.schemaVersion -ne 1 -or
        [string]$value.planVersion -ne $script:Week6PlanVersion -or
        [string]::IsNullOrWhiteSpace([string]$value.packageId)) {
        throw 'Week 6 manifest contract is invalid.'
    }
    return $value
}

function Test-Week6LanReport {
    param([Parameter(Mandatory = $true)]$Report)
    $rounds = @($Report.rounds)
    if ([int]$Report.schemaVersion -ne 1 -or
        [string]::IsNullOrWhiteSpace([string]$Report.packageId) -or
        [int]$Report.roundsRequested -lt 1 -or
        [int]$Report.roundsPassed -ne [int]$Report.roundsRequested -or
        $rounds.Count -ne [int]$Report.roundsRequested -or
        [string]$Report.lifecycle -notin @(
            'normal','viewer-first','publisher-first') -or
        ([string]$Report.lifecycle -ne 'normal' -and $rounds.Count -ne 1) -or
        -not [bool]$Report.cleanupPassed) {
        return $false
    }
    $roundNumbers = @($rounds | ForEach-Object { [int]$_.round } |
        Select-Object -Unique)
    if ($roundNumbers.Count -ne $rounds.Count) { return $false }
    foreach ($round in $rounds) {
        if (-not [bool]$round.passed) { return $false }
        if ([string]$round.localType -ne 'host' -or
            [string]$round.remoteType -ne 'host' -or
            [string]$round.localTransport -ne 'udp' -or
            [string]$round.remoteTransport -ne 'udp') { return $false }
        if ([string]$Report.mediaRole -eq 'viewer' -and
            ([int64]$round.receivedRtpPackets -le 0 -or
             [int64]$round.receivedAccessUnits -le 0 -or
             -not [bool]$round.decoded -or -not [bool]$round.presented)) {
            return $false
        }
    }
    return $true
}

function Test-Week6LanReportSet {
    param([Parameter(Mandatory = $true)][object[]]$Reports)
    if ($Reports.Count -ne 4) { return $false }
    $expected = @(
        'publisher:offer', 'viewer:answer',
        'viewer:offer', 'publisher:answer'
    )
    $packageIds = @($Reports | ForEach-Object { [string]$_.packageId } |
        Select-Object -Unique)
    if ($packageIds.Count -ne 1) { return $false }
    $actual = @($Reports | ForEach-Object {
        "$([string]$_.mediaRole):$([string]$_.signalingRole)"
    })
    foreach ($combination in $expected) {
        if ($combination -notin $actual) { return $false }
    }
    foreach ($report in $Reports) {
        if (-not (Test-Week6LanReport -Report $report)) { return $false }
    }
    return $true
}

Export-ModuleMember -Function @(
    'ConvertTo-Week6RelativePath',
    'New-Week6PackageManifest',
    'Read-Week6Manifest',
    'Test-Week6LanReport',
    'Test-Week6LanReportSet'
)
