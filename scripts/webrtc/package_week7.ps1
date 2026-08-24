[CmdletBinding()]
param(
    [string]$BuildRoot,
    [string]$OutputRoot,
    [string]$QtRoot = $env:QTDIR,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$SamplePath,
    [string]$SourceCommit
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'QualificationCommon.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'WebRtcPackageCommon.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'Week7QualificationCommon.psm1') -Force

$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
[void](Assert-QualificationConcretePath -Value $QtRoot -Name 'QtRoot')
[void](Assert-QualificationConcretePath -Value $VcpkgRoot -Name 'VcpkgRoot')
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $sourceRoot 'out\build-windows-x64\week7\release-on'
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $sourceRoot 'out\packages\webrtc-week7'
}
if ([string]::IsNullOrWhiteSpace($SamplePath)) {
    $SamplePath = Join-Path $sourceRoot 'out\webrtc-week7\webrtc-assets\sample.mp4'
}
if ([string]::IsNullOrWhiteSpace($SourceCommit)) {
    $SourceCommit = (& git -C $sourceRoot rev-parse HEAD).Trim()
}

$package = Initialize-WebRtcPortableStage -SourceRoot $sourceRoot `
    -BuildRoot $BuildRoot -OutputRoot $OutputRoot -QtRoot $QtRoot `
    -VcpkgRoot $VcpkgRoot -SamplePath $SamplePath `
    -SourceCommit $SourceCommit -PackageLabel 'RtmpMonitor-WebRTC-Week7'
$stage = $package.Stage

foreach ($file in @(
        'week7_public_test.ps1',
        'QualificationCommon.psm1',
        'WebRtcHandoffCommon.psm1',
        'Week7QualificationCommon.psm1')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $stage
}
Copy-Item -LiteralPath `
    (Join-Path $sourceRoot 'docs\versions\webrtc-v2\weeks\week07\testing_guide.md') `
    -Destination (Join-Path $stage 'TESTING_GUIDE.md')

$tools = Resolve-QualificationTools
$stream = Get-WebRtcPackagedSampleStream -StageRoot $stage `
    -FfprobePath $tools.Ffprobe
Assert-QualificationSample -Stream $stream
$manifest = New-Week7PackageManifest -StageRoot $stage `
    -SourceCommit $SourceCommit -SampleStream $stream
$manifest | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $stage 'package-manifest.json') `
        -Encoding UTF8
[void](Read-Week7Manifest -Path (Join-Path $stage 'package-manifest.json'))
if (Test-Path -LiteralPath (Join-Path $stage 'local-config')) {
    throw 'Week 7 package must not include local ICE configuration.'
}
Complete-WebRtcPortablePackage -StageRoot $stage -ZipPath $package.Zip
Write-Host "Week 7 package: $stage"
Write-Host "Week 7 archive: $($package.Zip)"
