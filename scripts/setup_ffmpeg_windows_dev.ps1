[CmdletBinding()]
param(
    [string]$VcpkgRoot = "F:\DevTools\vcpkg",
    [string]$DownloadsRoot = "F:\DevTools\vcpkg-downloads",
    [string]$BinaryCacheRoot = "F:\DevTools\vcpkg-binary-cache",
    [string]$TemporaryRoot = "F:\Temp\rtmp-monitor-vcpkg",
    [string]$ExpectedFFmpegVersion = "8.1.2",
    [string]$VcpkgCommit = "4eb0f7cabb9ca18132d80009312411b9261bba7b"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-NonSystemDrivePath
{
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    if ([System.IO.Path]::GetPathRoot($fullPath) -ieq "C:\") {
        throw "FFmpeg development files must not be written to drive C: $fullPath"
    }
}

function Invoke-NativeCommand
{
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($Arguments -join ' ')"
    }
}

function Test-LocalProxy
{
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $connection = $client.BeginConnect("127.0.0.1", 7890, $null, $null)
        if (-not $connection.AsyncWaitHandle.WaitOne(1000)) {
            return $false
        }
        $client.EndConnect($connection)
        return $true
    }
    catch {
        return $false
    }
    finally {
        $client.Dispose()
    }
}

function Add-UserPathEntry
{
    param([Parameter(Mandatory = $true)][string]$Entry)

    $currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $entries = @($currentPath -split ";" | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and $_.TrimEnd("\") -ine $Entry.TrimEnd("\")
    })
    [Environment]::SetEnvironmentVariable(
        "Path",
        (@($Entry) + $entries -join ";") + ";",
        "User"
    )
}

foreach ($path in @($VcpkgRoot, $DownloadsRoot, $BinaryCacheRoot, $TemporaryRoot)) {
    Assert-NonSystemDrivePath -Path $path
    New-Item -ItemType Directory -Path $path -Force | Out-Null
}

$env:TEMP = $TemporaryRoot
$env:TMP = $TemporaryRoot
$env:VCPKG_ROOT = $VcpkgRoot
$env:VCPKG_DOWNLOADS = $DownloadsRoot
$env:VCPKG_DEFAULT_BINARY_CACHE = $BinaryCacheRoot
$env:VCPKG_DISABLE_METRICS = "1"

if (Test-LocalProxy) {
    $env:HTTP_PROXY = "http://127.0.0.1:7890"
    $env:HTTPS_PROXY = "http://127.0.0.1:7890"
    Write-Host "Using local proxy http://127.0.0.1:7890."
}

$gitDirectory = Join-Path $VcpkgRoot ".git"
if (-not (Test-Path -LiteralPath $gitDirectory)) {
    if ((Get-ChildItem -LiteralPath $VcpkgRoot -Force | Measure-Object).Count -ne 0) {
        throw "The vcpkg directory is non-empty and is not a Git repository: $VcpkgRoot"
    }
    Invoke-NativeCommand -FilePath "git" -Arguments @(
        "clone", "--filter=blob:none", "--no-checkout",
        "https://github.com/microsoft/vcpkg.git", $VcpkgRoot
    )
}
else {
    $pendingChanges = @(& git -C $VcpkgRoot status --porcelain | Where-Object {
        $_ -ne "?? rtmp-monitor-ffmpeg-env.txt"
    })
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to inspect the vcpkg working tree."
    }
    if ($pendingChanges) {
        throw "The vcpkg working tree contains local changes: $VcpkgRoot"
    }
}

& git -C $VcpkgRoot cat-file -e "$VcpkgCommit`^{commit}"
if ($LASTEXITCODE -ne 0) {
    Invoke-NativeCommand -FilePath "git" -Arguments @(
        "-C", $VcpkgRoot, "fetch", "--depth", "1", "origin", $VcpkgCommit
    )
}
Invoke-NativeCommand -FilePath "git" -Arguments @(
    "-C", $VcpkgRoot, "checkout", "--detach", $VcpkgCommit
)

$vcpkgExecutable = Join-Path $VcpkgRoot "vcpkg.exe"
if (-not (Test-Path -LiteralPath $vcpkgExecutable)) {
    $toolMetadataPath = Join-Path $VcpkgRoot "scripts\vcpkg-tool-metadata.txt"
    $toolMetadata = ConvertFrom-StringData (Get-Content -LiteralPath $toolMetadataPath -Raw)
    $toolRelease = $toolMetadata.VCPKG_TOOL_RELEASE_TAG
    if ([string]::IsNullOrWhiteSpace($toolRelease)) {
        throw "Unable to determine the required vcpkg tool release."
    }

    $vcpkgDownload = Join-Path $TemporaryRoot "vcpkg.exe.download"
    $vcpkgDownloadUrl = "https://github.com/microsoft/vcpkg-tool/releases/download/$toolRelease/vcpkg.exe"
    $curlArguments = @("--fail", "--location", "--retry", "3")
    if (Test-LocalProxy) {
        $curlArguments += @("--proxy", "http://127.0.0.1:7890")
    }
    $curlArguments += @("--output", $vcpkgDownload, $vcpkgDownloadUrl)
    Invoke-NativeCommand -FilePath "$env:SystemRoot\System32\curl.exe" -Arguments $curlArguments
    Move-Item -LiteralPath $vcpkgDownload -Destination $vcpkgExecutable -Force
}

Invoke-NativeCommand -FilePath $vcpkgExecutable -Arguments @("version", "--disable-metrics")
New-Item -ItemType File -Path (Join-Path $VcpkgRoot "vcpkg.disable-metrics") -Force | Out-Null

$portManifestPath = Join-Path $VcpkgRoot "ports\ffmpeg\vcpkg.json"
$portManifest = Get-Content -LiteralPath $portManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($portManifest.version -ne $ExpectedFFmpegVersion) {
    throw "The vcpkg FFmpeg port is $($portManifest.version); expected $ExpectedFFmpegVersion."
}

Invoke-NativeCommand -FilePath $vcpkgExecutable -Arguments @(
    "install",
    "ffmpeg[core,avcodec,avformat,swscale]:x64-windows",
    "--clean-after-build"
)

$installedRoot = Join-Path $VcpkgRoot "installed\x64-windows"
$requiredFiles = @(
    "include\libavcodec\avcodec.h",
    "include\libavformat\avformat.h",
    "include\libavutil\avutil.h",
    "include\libswscale\swscale.h",
    "lib\avcodec.lib",
    "lib\avformat.lib",
    "lib\avutil.lib",
    "lib\swscale.lib"
)
foreach ($relativePath in $requiredFiles) {
    $fullPath = Join-Path $installedRoot $relativePath
    if (-not (Test-Path -LiteralPath $fullPath)) {
        throw "FFmpeg installation verification failed; missing: $fullPath"
    }
}

$releaseDlls = @(Get-ChildItem -LiteralPath (Join-Path $installedRoot "bin") -Filter "av*.dll")
$debugDlls = @(Get-ChildItem -LiteralPath (Join-Path $installedRoot "debug\bin") -Filter "av*.dll")
if ($releaseDlls.Count -eq 0 -or $debugDlls.Count -eq 0) {
    throw "FFmpeg DLL verification failed for the Release or Debug configuration."
}

[Environment]::SetEnvironmentVariable("VCPKG_ROOT", $VcpkgRoot, "User")
[Environment]::SetEnvironmentVariable("VCPKG_DOWNLOADS", $DownloadsRoot, "User")
[Environment]::SetEnvironmentVariable("VCPKG_DEFAULT_BINARY_CACHE", $BinaryCacheRoot, "User")
Add-UserPathEntry -Entry $VcpkgRoot

$vcpkgCommit = (& git -C $VcpkgRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Unable to read the vcpkg commit."
}

$markerDirectory = Join-Path $installedRoot "share\rtmp-monitor"
New-Item -ItemType Directory -Path $markerDirectory -Force | Out-Null
$markerPath = Join-Path $markerDirectory "ffmpeg-env.txt"
@(
    "FFmpegVersion=$ExpectedFFmpegVersion",
    "Triplet=x64-windows",
    "Features=core,avcodec,avformat,swscale",
    "VcpkgCommit=$vcpkgCommit",
    "InstalledRoot=$installedRoot"
) | Set-Content -LiteralPath $markerPath -Encoding UTF8

$legacyMarkerPath = Join-Path $VcpkgRoot "rtmp-monitor-ffmpeg-env.txt"
if (Test-Path -LiteralPath $legacyMarkerPath) {
    Remove-Item -LiteralPath $legacyMarkerPath -Force
}

Write-Host "Windows x64 FFmpeg $ExpectedFFmpegVersion LGPL development files are ready."
Write-Host "Installed root: $installedRoot"
Write-Host "Open a new terminal to load the user environment variables and PATH."
