[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BuildDir,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$')]
    [string]$Version,

    [switch]$DeferExecutableValidation
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [string]$LogPath
    )

    if ($LogPath) {
        & $FilePath @Arguments 2>&1 | Tee-Object -LiteralPath $LogPath
    } else {
        & $FilePath @Arguments
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($Arguments -join ' ')"
    }
}

function Get-CMakeCacheValue {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyString()][string[]]$CacheLines,
        [Parameter(Mandatory = $true)][string]$Name
    )

    $pattern = '^' + [regex]::Escape($Name) + '(?::[^=]+)?=(.*)$'
    $line = $CacheLines | Where-Object { $_ -match $pattern } | Select-Object -First 1
    if (-not $line) {
        throw "CMake cache entry is missing: $Name"
    }
    return ([regex]::Match($line, $pattern)).Groups[1].Value.Trim()
}

function Copy-PackageFile {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$SourceLabel,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()]
        [System.Collections.Generic.List[string]]$Manifest
    )

    Copy-Item -LiteralPath $Source -Destination $Destination
    $Manifest.Add("$(Split-Path -Leaf $Destination)`t$SourceLabel")
}

function Get-FFmpegBuildInformation {
    param([Parameter(Mandatory = $true)][string]$FFmpegBin)

    # Some Windows launchers can inherit both `PATH` and `Path`. Updating only
    # one leaves the other visible to LoadLibrary and can make this probe load
    # an unrelated FFmpeg build (for example one bundled with MinGW/Conda).
    # Use a deliberately small process-local search path while probing the DLL.
    $oldPath = [Environment]::GetEnvironmentVariable('Path', 'Process')
    $windowsDirectory = [Environment]::GetEnvironmentVariable('SystemRoot', 'Process')
    $systemDirectory = [Environment]::SystemDirectory
    try {
        [Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
        [Environment]::SetEnvironmentVariable('Path', $null, 'Process')
        [Environment]::SetEnvironmentVariable(
            'Path',
            "$FFmpegBin;$systemDirectory;$windowsDirectory",
            'Process'
        )
        if (-not ('RtmpMonitorFFmpegProbe' -as [type])) {
            Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class RtmpMonitorFFmpegProbe {
    [DllImport("avutil-60.dll", CallingConvention=CallingConvention.Cdecl)]
    public static extern IntPtr avutil_configuration();
    [DllImport("avutil-60.dll", CallingConvention=CallingConvention.Cdecl)]
    public static extern IntPtr av_version_info();
}
'@
        }
        $runtimeVersion = [Runtime.InteropServices.Marshal]::PtrToStringAnsi(
            [RtmpMonitorFFmpegProbe]::av_version_info())
        $configuration = [Runtime.InteropServices.Marshal]::PtrToStringAnsi(
            [RtmpMonitorFFmpegProbe]::avutil_configuration())
        return [pscustomobject]@{
            Version = $runtimeVersion
            Configuration = $configuration
        }
    } finally {
        [Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
        [Environment]::SetEnvironmentVariable('Path', $null, 'Process')
        [Environment]::SetEnvironmentVariable('Path', $oldPath, 'Process')
    }
}

if ($env:OS -ne 'Windows_NT') {
    throw 'scripts/package_windows.ps1 must run on Windows.'
}

$repoRoot = Resolve-FullPath -Path (Join-Path $PSScriptRoot '..')
$resolvedBuildDir = Resolve-FullPath -Path $BuildDir
$resolvedOutputDir = Resolve-FullPath -Path $OutputDir
$cachePath = Join-Path $resolvedBuildDir 'CMakeCache.txt'
$builtExe = Join-Path $resolvedBuildDir 'rtmp_monitor.exe'

foreach ($requiredPath in @($cachePath, $builtExe)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required Release build artifact is missing: $requiredPath"
    }
}

if (Test-Path -LiteralPath $resolvedOutputDir) {
    if (Get-ChildItem -LiteralPath $resolvedOutputDir -Force | Select-Object -First 1) {
        throw "OutputDir exists and is not empty; refusing to overwrite it: $resolvedOutputDir"
    }
} else {
    New-Item -ItemType Directory -Path $resolvedOutputDir | Out-Null
}

$stageName = "RtmpMonitor-$Version-windows-x64"
if ((Split-Path -Leaf $resolvedOutputDir) -ne $stageName) {
    throw "OutputDir leaf must be '$stageName' so the portable ZIP has a deterministic name."
}
$packageRoot = Split-Path -Parent $resolvedOutputDir
$zipPath = Join-Path $packageRoot "$stageName.zip"
$auditDir = Join-Path $packageRoot "audit-$stageName"
foreach ($protectedPath in @($zipPath, $auditDir)) {
    if (Test-Path -LiteralPath $protectedPath) {
        throw "Packaging output already exists; refusing to overwrite it: $protectedPath"
    }
}
New-Item -ItemType Directory -Path $auditDir | Out-Null

$cacheLines = Get-Content -LiteralPath $cachePath -Encoding UTF8
$buildType = Get-CMakeCacheValue -CacheLines $cacheLines -Name 'CMAKE_BUILD_TYPE'
$compilerPath = Get-CMakeCacheValue -CacheLines $cacheLines -Name 'CMAKE_CXX_COMPILER'
$qt6Dir = Get-CMakeCacheValue -CacheLines $cacheLines -Name 'Qt6_DIR'
$vcpkgInstalledDir = Get-CMakeCacheValue -CacheLines $cacheLines -Name 'VCPKG_INSTALLED_DIR'
$vcpkgTriplet = Get-CMakeCacheValue -CacheLines $cacheLines -Name 'VCPKG_TARGET_TRIPLET'
$cmakeCommand = Get-CMakeCacheValue -CacheLines $cacheLines -Name 'CMAKE_COMMAND'
$ninjaCommand = Get-CMakeCacheValue -CacheLines $cacheLines -Name 'CMAKE_MAKE_PROGRAM'
$compilerMetadata = Get-ChildItem -LiteralPath (Join-Path $resolvedBuildDir 'CMakeFiles') `
    -Filter 'CMakeCXXCompiler.cmake' -File -Recurse |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $compilerMetadata) {
    throw 'Unable to locate CMakeCXXCompiler.cmake for the MSVC version audit.'
}
$compilerVersionLine = Get-Content -LiteralPath $compilerMetadata.FullName -Encoding UTF8 |
    Where-Object { $_ -match '^set\(CMAKE_CXX_COMPILER_VERSION\s+"([^"]+)"\)' } |
    Select-Object -First 1
if (-not $compilerVersionLine) {
    throw "Unable to read the MSVC version from $($compilerMetadata.FullName)"
}
$compilerVersion = ([regex]::Match($compilerVersionLine, '"([^"]+)"')).Groups[1].Value

if ($buildType -ne 'Release') {
    throw "BuildDir is not a Release build: CMAKE_BUILD_TYPE=$buildType"
}
if ((Split-Path -Leaf $compilerPath) -ne 'cl.exe') {
    throw "BuildDir does not use MSVC cl.exe: $compilerPath"
}
if ($vcpkgTriplet -ne 'x64-windows') {
    throw "Unexpected vcpkg triplet: $vcpkgTriplet"
}

$qtRoot = Resolve-FullPath -Path (Join-Path $qt6Dir '..\..\..')
$qtBin = Join-Path $qtRoot 'bin'
$windeployqt = Join-Path $qtBin 'windeployqt.exe'
$qtVersionHeader = Join-Path $qtRoot 'include\QtCore\qtcoreversion.h'
$vcpkgRoot = Resolve-FullPath -Path (Join-Path $vcpkgInstalledDir '..')
$ffmpegBin = Join-Path $vcpkgInstalledDir "$vcpkgTriplet\bin"
$ffmpegShare = Join-Path $vcpkgInstalledDir "$vcpkgTriplet\share\ffmpeg"
$pahoShare = Join-Path $vcpkgInstalledDir "$vcpkgTriplet\share\paho-mqtt"

foreach ($requiredPath in @($windeployqt, $qtVersionHeader, $ffmpegBin, $ffmpegShare, $pahoShare)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Dependency source is missing: $requiredPath"
    }
}

$qtVersionLine = Get-Content -LiteralPath $qtVersionHeader -Encoding UTF8 |
    Where-Object { $_ -match '^#define QTCORE_VERSION_STR\s+"([^"]+)"' } |
    Select-Object -First 1
if (-not $qtVersionLine) {
    throw "Unable to read Qt version from $qtVersionHeader"
}
$qtVersion = ([regex]::Match($qtVersionLine, '"([^"]+)"')).Groups[1].Value
if ($qtVersion -ne '6.6.1') {
    throw "This release checklist requires Qt 6.6.1; found $qtVersion"
}

$ffmpegInfo = Get-FFmpegBuildInformation -FFmpegBin $ffmpegBin
if ($ffmpegInfo.Version -ne '8.1.2') {
    throw "This release checklist requires FFmpeg 8.1.2; found $($ffmpegInfo.Version)"
}
if ($ffmpegInfo.Configuration -match '(?:^|\s)--enable-(?:gpl|nonfree)(?:\s|$)') {
    throw 'The linked FFmpeg build enables GPL or nonfree components; packaging is blocked.'
}
$ffmpegInfo.Configuration | Set-Content -LiteralPath (
    Join-Path $auditDir 'ffmpeg-build-configuration.txt') -Encoding UTF8

Invoke-Checked -FilePath $cmakeCommand -Arguments @(
    '--install', $resolvedBuildDir, '--prefix', $resolvedOutputDir, '--config', 'Release'
) -LogPath (Join-Path $auditDir 'cmake-install.log')

$installedExe = Join-Path $resolvedOutputDir 'rtmp_monitor.exe'
if (-not (Test-Path -LiteralPath $installedExe -PathType Leaf)) {
    throw "CMake install did not place rtmp_monitor.exe at the package root: $installedExe"
}

Invoke-Checked -FilePath $windeployqt -Arguments @(
    '--release',
    '--compiler-runtime',
    '--no-opengl-sw',
    '--no-translations',
    '--skip-plugin-types', 'generic',
    '--include-plugins', 'qwindows,qwindowsvistastyle,qjpeg,qgif,qico,qschannelbackend,qcertonlybackend,windowsmediaplugin',
    '--exclude-plugins', 'qopensslbackend,qsvgicon,qsvg,qpdf,qicns,qtga,qtiff,qwbmp,qwebp,ffmpegmediaplugin',
    '--dir', $resolvedOutputDir,
    $installedExe
) -LogPath (Join-Path $auditDir 'windeployqt.log')

$sourceManifest = [System.Collections.Generic.List[string]]::new()
$appLocalRuntime = @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')
$compilerMinor = ([version]$compilerVersion).Minor
$compilerRedistDirectory = if ($env:VCToolsRedistDir) {
    Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC143.CRT'
} else {
    $null
}
foreach ($runtimeName in $appLocalRuntime) {
    $runtimePath = Join-Path $resolvedOutputDir $runtimeName
    if (-not (Test-Path -LiteralPath $runtimePath -PathType Leaf)) {
        $runtimeCandidates = @()
        if ($compilerRedistDirectory) {
            $runtimeCandidates += Join-Path $compilerRedistDirectory $runtimeName
        }
        $runtimeCandidates += Join-Path ([Environment]::SystemDirectory) $runtimeName
        $runtimeSource = $runtimeCandidates |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
            Where-Object {
                ([version](Get-Item -LiteralPath $_).VersionInfo.FileVersion).Minor -ge
                    $compilerMinor
            } |
            Select-Object -First 1
        if (-not $runtimeSource) {
            throw "No app-local $runtimeName at or above MSVC minor $compilerMinor is available."
        }
        Copy-Item -LiteralPath $runtimeSource -Destination $runtimePath
    }
    $runtimeVersion = (Get-Item -LiteralPath $runtimePath).VersionInfo.FileVersion
    $sourceManifest.Add("$runtimeName`tApp-local MSVC runtime $runtimeVersion")
}
$redistInstaller = Join-Path $resolvedOutputDir 'vc_redist.x64.exe'
if (Test-Path -LiteralPath $redistInstaller -PathType Leaf) {
    Move-Item -LiteralPath $redistInstaller -Destination (
        Join-Path $auditDir 'windeployqt-vc_redist.x64.exe'
    )
}

$ffmpegComponents = @('avcodec', 'avformat', 'avutil', 'swscale', 'swresample')
foreach ($component in $ffmpegComponents) {
    $matches = @(Get-ChildItem -LiteralPath $ffmpegBin -Filter "$component-*.dll" -File)
    if ($matches.Count -ne 1) {
        throw "Expected exactly one Release $component DLL in $ffmpegBin; found $($matches.Count)."
    }
    Copy-PackageFile -Source $matches[0].FullName `
        -Destination (Join-Path $resolvedOutputDir $matches[0].Name) `
        -SourceLabel "vcpkg $vcpkgTriplet FFmpeg $($ffmpegInfo.Version)" `
        -Manifest $sourceManifest
}

$pahoDll = Join-Path $ffmpegBin 'paho-mqtt3a.dll'
if (-not (Test-Path -LiteralPath $pahoDll -PathType Leaf)) {
    throw "Paho MQTT runtime is missing: $pahoDll"
}
Copy-PackageFile -Source $pahoDll `
    -Destination (Join-Path $resolvedOutputDir 'paho-mqtt3a.dll') `
    -SourceLabel "vcpkg $vcpkgTriplet Paho MQTT C 1.3.16" `
    -Manifest $sourceManifest

if (-not $DeferExecutableValidation) {
    Invoke-Checked -FilePath $cmakeCommand -Arguments @(
        "-DEXECUTABLE_PATH=$installedExe",
        "-DEXPECTED_VERSION=$Version",
        "-DWORKING_DIRECTORY=$resolvedOutputDir",
        '-P',
        (Join-Path $repoRoot 'cmake\VerifyExecutableVersion.cmake')
    )
}

$qtPlatformSource = Join-Path $qtRoot 'plugins\platforms\qwindows.dll'
$qtPlatformTarget = Join-Path $resolvedOutputDir 'platforms\qwindows.dll'
if (-not (Test-Path -LiteralPath $qtPlatformTarget -PathType Leaf)) {
    throw 'windeployqt did not deploy platforms/qwindows.dll.'
}
$sourceManifest.Add("platforms/qwindows.dll`tQt $qtVersion MSVC configured installation")

$requiredPackagePaths = @(
    'rtmp_monitor.exe',
    'styles\app.qss',
    'platforms\qwindows.dll',
    'imageformats\qjpeg.dll',
    'tls\qschannelbackend.dll',
    'Qt6Multimedia.dll',
    'multimedia\windowsmediaplugin.dll',
    'media-server.example.ini',
    'LICENSE',
    'README_WINDOWS_TEST_PACKAGE.txt',
    'THIRD_PARTY_NOTICES'
    'paho-mqtt3a.dll'
    'msvcp140.dll'
    'vcruntime140.dll'
    'vcruntime140_1.dll'
)
foreach ($relativePath in $requiredPackagePaths) {
    if (-not (Test-Path -LiteralPath (Join-Path $resolvedOutputDir $relativePath) -PathType Leaf)) {
        throw "Required package file is missing: $relativePath"
    }
}

$licenseDir = Join-Path $resolvedOutputDir 'licenses'
New-Item -ItemType Directory -Path $licenseDir | Out-Null
$qtInstallerRoot = Resolve-FullPath -Path (Join-Path $qtRoot '..\..')
$qtLicense = Join-Path $qtInstallerRoot 'Licenses\LICENSE'
$qtThirdParty = Join-Path $qtInstallerRoot 'Licenses\ThirdPartySoftware_Listing.txt'
$ffmpegLicense = Join-Path $ffmpegShare 'copyright'
$pahoLicense = Join-Path $pahoShare 'copyright'
Copy-Item -LiteralPath $qtLicense -Destination (Join-Path $licenseDir 'Qt-LGPL-3.0.txt')
Copy-Item -LiteralPath $qtThirdParty -Destination (Join-Path $licenseDir 'Qt-Third-Party-Software.txt')
Copy-Item -LiteralPath $ffmpegLicense -Destination (Join-Path $licenseDir 'FFmpeg-LGPL-2.1.txt')
Copy-Item -LiteralPath $pahoLicense -Destination (Join-Path $licenseDir 'Paho-MQTT-C-EPL-2.0.txt')

$gitCommand = (Get-Command git -ErrorAction Stop).Source
$gitCommit = (& $gitCommand -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $gitCommit -notmatch '^[0-9a-f]{40}$') {
    throw 'Unable to determine the full Git commit.'
}
$cmakeVersion = (& $cmakeCommand --version | Select-Object -First 1).Trim()
$ninjaVersion = (& $ninjaCommand --version).Trim()
$buildUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')

@(
    "RtmpMonitor version: $Version"
    "Git commit: $gitCommit"
    "Build time (UTC): $buildUtc"
    "Compiler: MSVC $compilerVersion"
    'VC runtime: app-local DLL deployment via windeployqt --compiler-runtime'
    "CMake: $cmakeVersion"
    "Ninja: $ninjaVersion"
    "Qt: $qtVersion (MSVC x64, dynamic)"
    "FFmpeg: $($ffmpegInfo.Version) (vcpkg $vcpkgTriplet, dynamic)"
    'Paho MQTT C: 1.3.16 (vcpkg x64-windows, paho-mqtt3a dynamic, non-TLS API)'
    'FFmpeg restricted flags: --enable-gpl=false; --enable-nonfree=false'
    'SRS compatibility target: 6.0.184 (not bundled)'
    "vcpkg triplet: $vcpkgTriplet"
) | Set-Content -LiteralPath (Join-Path $resolvedOutputDir 'VERSION.txt') -Encoding UTF8

$forbiddenFiles = @(Get-ChildItem -LiteralPath $resolvedOutputDir -Recurse -File |
    Where-Object {
        $_.Name -match '(?i)(\.pdb$|\.ilk$|\.exp$|\.lib$|\.log$|\.dump$)' -or
        $_.Name -match '(?i)(^Qt6.+d\.dll$|^qwindowsd\.dll$|^q.*backendd\.dll$|^q(?:gif|ico|jpeg)d\.dll$|^.*_d\.dll$|^av.*d\.dll$)' -or
        $_.Name -in @('CMakeCache.txt', '.ninja_deps', '.ninja_log')
    })
if ($forbiddenFiles.Count -gt 0) {
    $names = ($forbiddenFiles | ForEach-Object FullName) -join [Environment]::NewLine
    throw "Forbidden build/debug files were found in the package:`n$names"
}

$sourceManifest | Sort-Object | Set-Content -LiteralPath (
    Join-Path $resolvedOutputDir 'DEPENDENCY_SOURCES.txt') -Encoding UTF8
Get-ChildItem -LiteralPath $resolvedOutputDir -Recurse -File |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($resolvedOutputDir.Length + 1).Replace('\', '/')
        "$relative`t$($_.Length)"
    } | Set-Content -LiteralPath (Join-Path $auditDir 'package-files.tsv') -Encoding UTF8

Compress-Archive -LiteralPath $resolvedOutputDir -DestinationPath $zipPath -CompressionLevel Optimal

Write-Output "PACKAGE_DIR=$resolvedOutputDir"
Write-Output "PACKAGE_ZIP=$zipPath"
Write-Output "PACKAGE_AUDIT_DIR=$auditDir"
