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
Import-Module (Join-Path $PSScriptRoot 'Week6LanCommon.psm1') -Force

$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
[void](Assert-QualificationConcretePath -Value $QtRoot -Name 'QtRoot')
[void](Assert-QualificationConcretePath -Value $VcpkgRoot -Name 'VcpkgRoot')
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $sourceRoot 'out\build-windows-x64\week6\release-on'
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $sourceRoot 'out\packages\webrtc-week6'
}
if ([string]::IsNullOrWhiteSpace($SamplePath)) {
    $SamplePath = Join-Path $sourceRoot 'out\webrtc-week6\webrtc-assets\sample.mp4'
}
if ([string]::IsNullOrWhiteSpace($SourceCommit)) {
    $SourceCommit = (& git -C $sourceRoot rev-parse HEAD).Trim()
}
if ($SourceCommit -notmatch '^[0-9a-f]{40}$') {
    throw 'SourceCommit must be a full Git object id.'
}

$runtime = Join-Path ([IO.Path]::GetFullPath($BuildRoot)) 'webrtc\Release'
$stage = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) `
    "RtmpMonitor-WebRTC-$($SourceCommit.Substring(0,12))-windows-x64"
$zip = $stage + '.zip'
$managedRoot = [IO.Path]::GetFullPath((Join-Path $sourceRoot 'out')).TrimEnd('\') + '\'
foreach ($target in @($stage, $zip)) {
    $full = [IO.Path]::GetFullPath($target)
    if (-not $full.StartsWith($managedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Package target escaped repository out/: $full"
    }
    if (Test-Path -LiteralPath $full) {
        Remove-Item -LiteralPath $full -Recurse -Force
    }
}

$client = Join-Path $runtime 'rtmp_monitor_webrtc_client.exe'
if (-not (Test-Path -LiteralPath $client -PathType Leaf)) {
    throw "Release client is missing: $client"
}
if (-not (Test-Path -LiteralPath $SamplePath -PathType Leaf)) {
    throw "Qualified Week 6 sample is missing: $SamplePath"
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'platforms') |
    Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'webrtc-assets') |
    Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'licenses') |
    Out-Null

Copy-Item -LiteralPath $client -Destination $stage
foreach ($dll in Get-ChildItem -LiteralPath $runtime -Filter '*.dll' -File) {
    Copy-Item -LiteralPath $dll.FullName -Destination $stage
}
foreach ($plugin in @('qwindows.dll','qoffscreen.dll')) {
    $path = Join-Path $runtime "platforms\$plugin"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required Qt platform plugin is missing: $plugin"
    }
    Copy-Item -LiteralPath $path -Destination (Join-Path $stage 'platforms')
}
Copy-Item -LiteralPath $SamplePath `
    -Destination (Join-Path $stage 'webrtc-assets\sample.mp4')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'week6_lan_test.ps1') `
    -Destination $stage
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Week6LanCommon.psm1') `
    -Destination $stage
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'QualificationCommon.psm1') `
    -Destination $stage
Copy-Item -LiteralPath `
    (Join-Path $sourceRoot 'docs\versions\webrtc-v2\weeks\week06\testing_guide.md') `
    -Destination (Join-Path $stage 'TESTING_GUIDE.md')

$licenseSources = [ordered]@{
    'Qt-LICENSE.txt' = (Join-Path (Split-Path -Parent $QtRoot) '..\Licenses\LICENSE')
    'FFmpeg-COPYRIGHT.txt' = (Join-Path $VcpkgRoot 'installed\x64-windows\share\ffmpeg\copyright')
    'libdatachannel-COPYRIGHT.txt' = (Join-Path $VcpkgRoot 'installed\x64-windows\share\libdatachannel\copyright')
    'libjuice-COPYRIGHT.txt' = (Join-Path $VcpkgRoot 'installed\x64-windows\share\libjuice\copyright')
    'libsrtp-COPYRIGHT.txt' = (Join-Path $VcpkgRoot 'installed\x64-windows\share\libsrtp\copyright')
    'OpenSSL-COPYRIGHT.txt' = (Join-Path $VcpkgRoot 'installed\x64-windows\share\openssl\copyright')
}
foreach ($entry in $licenseSources.GetEnumerator()) {
    $resolved = [IO.Path]::GetFullPath($entry.Value)
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "Required package license is missing: $($entry.Key)"
    }
    Copy-Item -LiteralPath $resolved `
        -Destination (Join-Path $stage "licenses\$($entry.Key)")
}

$tools = Resolve-QualificationTools
$probe = & $tools.Ffprobe -v error -select_streams v:0 `
    -show_entries stream=codec_name,profile,level,width,height,r_frame_rate,has_b_frames `
    -of json (Join-Path $stage 'webrtc-assets\sample.mp4') |
    Out-String | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or @($probe.streams).Count -ne 1) {
    throw 'Packaged sample ffprobe validation failed.'
}
Assert-QualificationSample -Stream $probe.streams[0]
$manifest = New-Week6PackageManifest -StageRoot $stage `
    -SourceCommit $SourceCommit -SampleStream $probe.streams[0]
$manifest | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath (Join-Path $stage 'package-manifest.json') `
        -Encoding UTF8
[void](Read-Week6Manifest -Path (Join-Path $stage 'package-manifest.json'))
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
Write-Host "Week 6 package: $stage"
Write-Host "Week 6 archive: $zip"
