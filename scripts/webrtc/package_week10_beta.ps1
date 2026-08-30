[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [Parameter(Mandatory=$true)][string]$VcpkgRoot,
    [Parameter(Mandatory=$true)][string]$SamplePath,
    [Parameter(Mandatory=$true)][string]$SourceCommit,
    [Parameter(Mandatory=$true)][string]$Version,
    [Parameter(Mandatory=$true)][string]$ResultPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$cmakeCommand = (Get-Command cmake -ErrorAction Stop).Source
$versionVerifier = Join-Path $sourceRoot 'cmake\VerifyExecutableVersion.cmake'
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$SamplePath = [IO.Path]::GetFullPath($SamplePath)
if ($SourceCommit -notmatch '^[0-9a-f]{40}$') {
    throw 'invalid_source_commit'
}
[void](New-Item -ItemType Directory -Force -Path $OutputRoot)
$stageName = "RtmpMonitor-$Version-windows-x64"
$stage = Join-Path $OutputRoot $stageName
$zip = Join-Path $OutputRoot "$stageName.zip"
$audit = Join-Path $OutputRoot "audit-$stageName"
$managedRoot = $OutputRoot.TrimEnd('\') + '\'
foreach ($target in @($stage,$zip,$audit)) {
    $full = [IO.Path]::GetFullPath($target)
    if (-not $full.StartsWith($managedRoot,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'package_target_outside_managed_root'
    }
    if (Test-Path -LiteralPath $full) {
        Remove-Item -LiteralPath $full -Recurse -Force
    }
}

& (Join-Path $sourceRoot 'scripts\package_windows.ps1') `
    -BuildDir $BuildRoot -OutputDir $stage -Version $Version `
    -DeferExecutableValidation
if ($LASTEXITCODE -ne 0) { throw 'base_package_failed' }
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }

$runtime = Join-Path $BuildRoot 'webrtc\Release'
$client = Join-Path $runtime 'rtmp_monitor_webrtc_client.exe'
foreach ($required in @($client,$SamplePath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw 'webrtc_package_input_missing'
    }
}
Copy-Item -LiteralPath $client -Destination $stage
$runtimeDlls = @(
    'datachannel.dll','juice.dll','srtp2.dll',
    'libssl-3-x64.dll','libcrypto-3-x64.dll'
)
foreach ($name in $runtimeDlls) {
    $source = Join-Path $runtime $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw 'webrtc_runtime_missing'
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $stage $name)
}
$offscreen = Join-Path $QtRoot 'plugins\platforms\qoffscreen.dll'
if (-not (Test-Path -LiteralPath $offscreen -PathType Leaf)) {
    throw 'qoffscreen_missing'
}
Copy-Item -LiteralPath $offscreen -Destination (Join-Path $stage 'platforms')
[void](New-Item -ItemType Directory -Force -Path (
    Join-Path $stage 'webrtc-assets'))
Copy-Item -LiteralPath $SamplePath -Destination (
    Join-Path $stage 'webrtc-assets\sample.mp4')

$licenseRoot = Join-Path $stage 'licenses'
$licenseSources = [ordered]@{
    'libdatachannel-COPYRIGHT.txt' = Join-Path $VcpkgRoot 'installed\x64-windows\share\libdatachannel\copyright'
    'libjuice-COPYRIGHT.txt' = Join-Path $VcpkgRoot 'installed\x64-windows\share\libjuice\copyright'
    'libsrtp-COPYRIGHT.txt' = Join-Path $VcpkgRoot 'installed\x64-windows\share\libsrtp\copyright'
    'OpenSSL-COPYRIGHT.txt' = Join-Path $VcpkgRoot 'installed\x64-windows\share\openssl\copyright'
}
foreach ($entry in $licenseSources.GetEnumerator()) {
    if (-not (Test-Path -LiteralPath $entry.Value -PathType Leaf)) {
        throw 'webrtc_license_missing'
    }
    Copy-Item -LiteralPath $entry.Value -Destination (
        Join-Path $licenseRoot $entry.Key)
}
Copy-Item -LiteralPath (
    Join-Path $sourceRoot 'docs\versions\webrtc-v2\weeks\week10\summary.md') `
    -Destination (Join-Path $stage 'WEBRTC_BETA_README.md')
Copy-Item -LiteralPath (
    Join-Path $sourceRoot 'docs\versions\webrtc-v2\weeks\week10\testing_guide.md') `
    -Destination (Join-Path $stage 'WEBRTC_BETA_TESTING_GUIDE.md')

$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File |
    Sort-Object FullName | ForEach-Object {
        [ordered]@{
            path = $_.FullName.Substring($stage.Length + 1).Replace('\','/')
            size = $_.Length
        }
    })
[ordered]@{
    version = $Version
    sourceCommit = $SourceCommit
    files = $files
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (
    Join-Path $stage 'package-manifest.json') -Encoding UTF8

$forbidden = @(Get-ChildItem -LiteralPath $stage -Recurse -File |
    Where-Object {
        $_.Name -match '(?i)(\.pdb$|\.lib$|\.exp$|\.ilk$|\.log$|\.dump$)' -or
        $_.Name -match '(?i)(test|qualification_runner|stun_fixture)' -and
            $_.Extension -eq '.exe' -or
        $_.Name -match '(?i)(^Qt6.+d\.dll$|^qwindowsd\.dll$|^Qt6Test\.dll$)'
    })
if ($forbidden.Count -ne 0) { throw 'forbidden_package_artifact' }
$executables = @(Get-ChildItem -LiteralPath $stage -Filter '*.exe' -File |
    Select-Object -ExpandProperty Name)
if ($executables.Count -ne 2 -or
    $executables -notcontains 'rtmp_monitor.exe' -or
    $executables -notcontains 'rtmp_monitor_webrtc_client.exe') {
    throw 'unexpected_package_executable_set'
}

$sensitiveCategories = [Collections.Generic.List[string]]::new()
$textFiles = @(Get-ChildItem -LiteralPath $stage -Recurse -File |
    Where-Object { $_.Extension -in @('.txt','.md','.json','.ini') })
$patterns = [ordered]@{
    signaling = '(?i)(candidate:|a=candidate|ice-ufrag|ice-pwd|a=fingerprint:)'
    port = '(?i)"(?:port|localPort|remotePort)"\s*:'
    address = '(?i)\b(?!(?:127\.0\.0\.1|0\.0\.0\.0|10\.\d{1,3}\.\d{1,3}\.\d{1,3}|192\.168\.\d{1,3}\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3})\b)(?:\d{1,3}\.){3}\d{1,3}\b'
    identifier = '(?i)([0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})'
    absolute_path = '(?i)([a-z]:[\\/])'
    camera_identifier = '(?i)(usb#vid_|vid_[0-9a-f]{4}.+pid_[0-9a-f]{4})'
    actual_rtmp = '(?i)rtmps?://(?!<rtmp-host>|localhost|127\.0\.0\.1|10\.|192\.168\.|172\.(?:1[6-9]|2\d|3[01])\.)'
}
foreach ($file in $textFiles) {
    $text = Get-Content -LiteralPath $file.FullName -Raw
    foreach ($entry in $patterns.GetEnumerator()) {
        if ($text -match $entry.Value -and
            -not $sensitiveCategories.Contains([string]$entry.Key)) {
            [void]$sensitiveCategories.Add([string]$entry.Key)
        }
    }
}
if ($sensitiveCategories.Count -ne 0) {
    throw ('sensitive_package_content:' +
        [string]::Join(',', @($sensitiveCategories)))
}

Compress-Archive -LiteralPath $stage -DestinationPath $zip `
    -CompressionLevel Optimal
[void](New-Item -ItemType Directory -Force -Path $audit)

function Invoke-CleanPackageTest([int]$Index) {
    $copyRoot = Join-Path $audit "copy-$Index"
    [void](New-Item -ItemType Directory -Force -Path $copyRoot)
    Expand-Archive -LiteralPath $zip -DestinationPath $copyRoot
    $root = Join-Path $copyRoot $stageName
    $main = Join-Path $root 'rtmp_monitor.exe'
    $portableClient = Join-Path $root 'rtmp_monitor_webrtc_client.exe'
    $oldPath = $env:Path
    $oldPlatform = $env:QT_QPA_PLATFORM
    $oldPluginPath = $env:QT_PLUGIN_PATH
    $oldPlatformPluginPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
    $oldQtDir = $env:QTDIR
    try {
        $env:Path = "$root;$([Environment]::SystemDirectory);$env:SystemRoot"
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:QT_PLUGIN_PATH = $null
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = $null
        $env:QTDIR = $null
        & $cmakeCommand "-DEXECUTABLE_PATH=$main" `
            "-DEXPECTED_VERSION=$Version" `
            "-DWORKING_DIRECTORY=$root" `
            '-P' $versionVerifier
        if ($LASTEXITCODE -ne 0) {
            throw 'package_main_version_failed'
        }
        & $portableClient --help | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'package_client_help_failed' }
        & $portableClient --invalid-argument | Out-Null
        if ($LASTEXITCODE -eq 0) { throw 'package_client_invalid_argument_failed' }
        $exchange = Join-Path $root 'session-exchange'
        Remove-Item -LiteralPath $exchange -Recurse -Force `
            -ErrorAction SilentlyContinue
        $viewerOut = Join-Path $audit "copy-$Index-viewer.jsonl"
        $publisherOut = Join-Path $audit "copy-$Index-publisher.jsonl"
        $viewerErr = Join-Path $audit "copy-$Index-viewer.stderr.txt"
        $publisherErr = Join-Path $audit "copy-$Index-publisher.stderr.txt"
        $viewer = Start-Process -FilePath $portableClient -ArgumentList @(
            '--media-role=viewer','--signaling-role=answer',
            '--ice-mode=host','--timeout-ms=30000'
        ) -WorkingDirectory $root -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput $viewerOut -RedirectStandardError $viewerErr
        Start-Sleep -Milliseconds 300
        $publisher = Start-Process -FilePath $portableClient -ArgumentList @(
            '--media-role=publisher','--signaling-role=offer',
            '--source=sample','--ice-mode=host','--timeout-ms=30000'
        ) -WorkingDirectory $root -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput $publisherOut `
            -RedirectStandardError $publisherErr
        if (-not $publisher.WaitForExit(60000) -or
            -not $viewer.WaitForExit(60000)) {
            throw 'package_local_closure_timeout'
        }
        if ($publisher.ExitCode -ne 0 -or $viewer.ExitCode -ne 0) {
            throw 'package_local_closure_failed'
        }
        foreach ($path in @($viewerOut,$publisherOut,$viewerErr,$publisherErr)) {
            $payload = Get-Content -LiteralPath $path -Raw
            if ($payload -match '(?i)(candidate:|a=candidate|ice-ufrag|ice-pwd|a=fingerprint:|"(?:port|localPort|remotePort)"\s*:|[A-Z]:[\\/]|(?:\d{1,3}\.){3}\d{1,3}|rtmps?://|usb#vid_|[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12})') {
                throw 'package_runtime_sensitive_output'
            }
        }
    } finally {
        $env:Path = $oldPath
        $env:QT_QPA_PLATFORM = $oldPlatform
        $env:QT_PLUGIN_PATH = $oldPluginPath
        $env:QT_QPA_PLATFORM_PLUGIN_PATH = $oldPlatformPluginPath
        $env:QTDIR = $oldQtDir
        foreach ($process in @(
            (Get-Process -Name 'rtmp_monitor' -ErrorAction SilentlyContinue),
            (Get-Process -Name 'rtmp_monitor_webrtc_client' -ErrorAction SilentlyContinue)
        )) {
            if ($process -and $process.Path.StartsWith(
                    $root,[StringComparison]::OrdinalIgnoreCase)) {
                Stop-Process -Id $process.Id -Force
            }
        }
    }
}

Invoke-CleanPackageTest 1
Invoke-CleanPackageTest 2
$residual = @(Get-Process -Name 'rtmp_monitor','rtmp_monitor_webrtc_client' `
    -ErrorAction SilentlyContinue | Where-Object {
        $_.Path.StartsWith($audit,[StringComparison]::OrdinalIgnoreCase)
    })
if ($residual.Count -ne 0) { throw 'package_residual_process' }

[void](New-Item -ItemType Directory -Force -Path (
    Split-Path -Parent $ResultPath))
[ordered]@{
    version = $Version
    sourceCommit = $SourceCommit
    packagePassed = $true
    stageName = $stageName
    archiveName = [IO.Path]::GetFileName($zip)
    fileCount = @(Get-ChildItem -LiteralPath $stage -Recurse -File).Count
    cleanExpansionCount = 2
    localRoleClosuresPassed = 2
    sensitiveOutputPassed = $true
    debugArtifactsAbsent = $true
    residualProcessesAbsent = $true
} | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $ResultPath -Encoding UTF8
Write-Host "Week 10 package candidate: $stageName"
