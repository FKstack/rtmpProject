Set-StrictMode -Version Latest

function Initialize-WebRtcPortableStage {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$BuildRoot,
        [Parameter(Mandatory = $true)][string]$OutputRoot,
        [Parameter(Mandatory = $true)][string]$QtRoot,
        [Parameter(Mandatory = $true)][string]$VcpkgRoot,
        [Parameter(Mandatory = $true)][string]$SamplePath,
        [Parameter(Mandatory = $true)][string]$SourceCommit,
        [Parameter(Mandatory = $true)][string]$PackageLabel
    )
    if ($SourceCommit -notmatch '^[0-9a-f]{40}$') {
        throw 'SourceCommit must be a full Git object id.'
    }
    $runtime = Join-Path ([IO.Path]::GetFullPath($BuildRoot)) 'webrtc\Release'
    $stage = Join-Path ([IO.Path]::GetFullPath($OutputRoot)) `
        "$PackageLabel-$($SourceCommit.Substring(0,12))-windows-x64"
    $zip = $stage + '.zip'
    $managedRoot = [IO.Path]::GetFullPath(
        (Join-Path $SourceRoot 'out')
    ).TrimEnd('\') + '\'
    foreach ($target in @($stage,$zip)) {
        $full = [IO.Path]::GetFullPath($target)
        if (-not $full.StartsWith(
                $managedRoot,[StringComparison]::OrdinalIgnoreCase)) {
            throw "Package target escaped repository out/: $full"
        }
        if (Test-Path -LiteralPath $full) {
            Remove-Item -LiteralPath $full -Recurse -Force
        }
    }

    $client = Join-Path $runtime 'rtmp_monitor_webrtc_client.exe'
    foreach ($required in @($client,$SamplePath)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
            throw "Required package input is missing: $required"
        }
    }
    foreach ($directory in @(
            $stage,
            (Join-Path $stage 'platforms'),
            (Join-Path $stage 'webrtc-assets'),
            (Join-Path $stage 'licenses'))) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }
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
    return [pscustomobject]@{ Stage=$stage; Zip=$zip; Runtime=$runtime }
}

function Get-WebRtcPackagedSampleStream {
    param(
        [Parameter(Mandatory = $true)][string]$StageRoot,
        [Parameter(Mandatory = $true)][string]$FfprobePath
    )
    $probe = & $FfprobePath -v error -select_streams v:0 `
        -show_entries stream=codec_name,profile,level,width,height,r_frame_rate,has_b_frames `
        -of json (Join-Path $StageRoot 'webrtc-assets\sample.mp4') |
        Out-String | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or @($probe.streams).Count -ne 1) {
        throw 'Packaged sample ffprobe validation failed.'
    }
    return $probe.streams[0]
}

function Complete-WebRtcPortablePackage {
    param(
        [Parameter(Mandatory = $true)][string]$StageRoot,
        [Parameter(Mandatory = $true)][string]$ZipPath
    )
    if (Test-Path -LiteralPath $ZipPath) {
        Remove-Item -LiteralPath $ZipPath -Force
    }
    Compress-Archive -Path (Join-Path $StageRoot '*') `
        -DestinationPath $ZipPath
}

Export-ModuleMember -Function @(
    'Initialize-WebRtcPortableStage',
    'Get-WebRtcPackagedSampleStream',
    'Complete-WebRtcPortablePackage'
)
