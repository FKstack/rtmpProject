[CmdletBinding()]
param(
    [ValidateSet('Check', 'Install')]
    [string]$Action = 'Install',

    [Parameter(Mandatory = $true)]
    [string]$VcpkgRoot,

    [string]$DownloadsRoot,
    [string]$BinaryCacheRoot,
    [string]$TemporaryRoot,
    [string]$ExpectedFFmpegVersion = '8.1.2',
    [string]$ExpectedPahoVersion = '1.3.16',
    [string]$VcpkgCommit = '4eb0f7cabb9ca18132d80009312411b9261bba7b',
    [string]$ProxyUrl,
    [switch]$PersistUserEnvironment,
    [switch]$RequireNonSystemDrive
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

function Assert-PathPolicy {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not $RequireNonSystemDrive) {
        return
    }
    $fullPath = Resolve-FullPath -Path $Path
    $systemDrive = [System.IO.Path]::GetPathRoot($env:SystemRoot)
    if ([System.IO.Path]::GetPathRoot($fullPath) -ieq $systemDrive) {
        throw "Task-owned FFmpeg files must not be written to the system drive: $fullPath"
    }
}

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($Arguments -join ' ')"
    }
}

function Add-UserPathEntry {
    param([Parameter(Mandatory = $true)][string]$Entry)

    $currentPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $entries = @($currentPath -split ';' | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and
        $_.TrimEnd('\') -ine $Entry.TrimEnd('\')
    })
    [Environment]::SetEnvironmentVariable(
        'Path',
        (@($Entry) + $entries -join ';') + ';',
        'User'
    )
}

function Read-KeyValueFile {
    param([Parameter(Mandatory = $true)][string]$Path)

    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path -Encoding UTF8) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            $values[$line.Substring(0, $separator)] = $line.Substring($separator + 1)
        }
    }
    return $values
}

function Test-FfmpegEnvironment {
    param([switch]$Quiet)

    $gitDirectory = Join-Path $script:ResolvedVcpkgRoot '.git'
    $vcpkgExecutable = Join-Path $script:ResolvedVcpkgRoot 'vcpkg.exe'
    foreach ($requiredPath in @($gitDirectory, $vcpkgExecutable)) {
        if (-not (Test-Path -LiteralPath $requiredPath)) {
            if ($Quiet) { return $false }
            throw "Required vcpkg path is missing: $requiredPath"
        }
    }

    $actualCommit = (& git -C $script:ResolvedVcpkgRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $VcpkgCommit) {
        if ($Quiet) { return $false }
        throw "vcpkg commit mismatch; expected $VcpkgCommit, found $actualCommit"
    }

    $portManifestPath = Join-Path $script:ResolvedVcpkgRoot 'ports\ffmpeg\vcpkg.json'
    $pahoManifestPath = Join-Path $script:ResolvedVcpkgRoot 'ports\paho-mqtt\vcpkg.json'
    if (-not (Test-Path -LiteralPath $portManifestPath -PathType Leaf)) {
        if ($Quiet) { return $false }
        throw "FFmpeg port manifest is missing: $portManifestPath"
    }
    $portManifest = Get-Content -LiteralPath $portManifestPath -Raw -Encoding UTF8 |
        ConvertFrom-Json
    if ($portManifest.version -ne $ExpectedFFmpegVersion) {
        if ($Quiet) { return $false }
        throw "The vcpkg FFmpeg port is $($portManifest.version); expected $ExpectedFFmpegVersion."
    }
    $pahoManifest = Get-Content -LiteralPath $pahoManifestPath -Raw -Encoding UTF8 |
        ConvertFrom-Json
    if ($pahoManifest.version -ne $ExpectedPahoVersion) {
        if ($Quiet) { return $false }
        throw "The vcpkg Paho MQTT port is $($pahoManifest.version); expected $ExpectedPahoVersion."
    }

    $installedRoot = Join-Path $script:ResolvedVcpkgRoot 'installed\x64-windows'
    $requiredFiles = @(
        'include\libavcodec\avcodec.h',
        'include\libavformat\avformat.h',
        'include\libavutil\avutil.h',
        'include\libswscale\swscale.h',
        'include\libswresample\swresample.h',
        'lib\avcodec.lib',
        'lib\avformat.lib',
        'lib\avutil.lib',
        'lib\swscale.lib',
        'lib\swresample.lib',
        'bin\avcodec-62.dll',
        'bin\avformat-62.dll',
        'bin\avutil-60.dll',
        'bin\swscale-9.dll',
        'bin\swresample-6.dll',
        'debug\bin\avcodec-62.dll',
        'debug\bin\avformat-62.dll',
        'debug\bin\avutil-60.dll',
        'debug\bin\swscale-9.dll',
        'debug\bin\swresample-6.dll'
        'include\MQTTAsync.h'
        'lib\paho-mqtt3a.lib'
        'bin\paho-mqtt3a.dll'
        'debug\bin\paho-mqtt3a.dll'
        'share\paho-mqtt\copyright'
    )
    foreach ($relativePath in $requiredFiles) {
        $fullPath = Join-Path $installedRoot $relativePath
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            if ($Quiet) { return $false }
            throw "FFmpeg installation verification failed; missing: $fullPath"
        }
    }

    $markerPath = Join-Path $installedRoot 'share\rtmp-monitor\ffmpeg-env.txt'
    if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf)) {
        if ($Quiet) { return $false }
        throw "RtmpMonitor FFmpeg environment marker is missing: $markerPath"
    }
    $marker = Read-KeyValueFile -Path $markerPath
    if (-not $marker.ContainsKey('FFmpegVersion') -or
        -not $marker.ContainsKey('PahoMqttVersion') -or
        -not $marker.ContainsKey('Triplet') -or
        -not $marker.ContainsKey('VcpkgCommit') -or
        $marker['FFmpegVersion'] -ne $ExpectedFFmpegVersion -or
        $marker['PahoMqttVersion'] -ne $ExpectedPahoVersion -or
        $marker['Triplet'] -ne 'x64-windows' -or
        $marker['VcpkgCommit'] -ne $VcpkgCommit) {
        if ($Quiet) { return $false }
        throw "RtmpMonitor FFmpeg environment marker does not match the required version."
    }

    if (-not $Quiet) {
        Write-Host "Windows x64 FFmpeg $ExpectedFFmpegVersion and Paho MQTT C $ExpectedPahoVersion environment is valid."
        Write-Host "vcpkg commit: $actualCommit"
        Write-Host "Installed root: $installedRoot"
    }
    return $true
}

$ResolvedVcpkgRoot = Resolve-FullPath -Path $VcpkgRoot
if ([string]::IsNullOrWhiteSpace($DownloadsRoot)) {
    $DownloadsRoot = "$ResolvedVcpkgRoot-downloads"
}
if ([string]::IsNullOrWhiteSpace($BinaryCacheRoot)) {
    $BinaryCacheRoot = "$ResolvedVcpkgRoot-binary-cache"
}
if ([string]::IsNullOrWhiteSpace($TemporaryRoot)) {
    $TemporaryRoot = "$ResolvedVcpkgRoot-temp"
}
$ResolvedDownloadsRoot = Resolve-FullPath -Path $DownloadsRoot
$ResolvedBinaryCacheRoot = Resolve-FullPath -Path $BinaryCacheRoot
$ResolvedTemporaryRoot = Resolve-FullPath -Path $TemporaryRoot

foreach ($path in @(
    $ResolvedVcpkgRoot,
    $ResolvedDownloadsRoot,
    $ResolvedBinaryCacheRoot,
    $ResolvedTemporaryRoot
)) {
    Assert-PathPolicy -Path $path
}

if ($Action -eq 'Check') {
    [void](Test-FfmpegEnvironment)
    return
}

foreach ($path in @(
    $ResolvedVcpkgRoot,
    $ResolvedDownloadsRoot,
    $ResolvedBinaryCacheRoot,
    $ResolvedTemporaryRoot
)) {
    New-Item -ItemType Directory -Path $path -Force | Out-Null
}

$env:TEMP = $ResolvedTemporaryRoot
$env:TMP = $ResolvedTemporaryRoot
$env:VCPKG_ROOT = $ResolvedVcpkgRoot
$env:VCPKG_DOWNLOADS = $ResolvedDownloadsRoot
$env:VCPKG_DEFAULT_BINARY_CACHE = $ResolvedBinaryCacheRoot
$env:VCPKG_DISABLE_METRICS = '1'
if (-not [string]::IsNullOrWhiteSpace($ProxyUrl)) {
    $proxy = [uri]$ProxyUrl
    if ($proxy.Scheme -notin @('http', 'https')) {
        throw 'ProxyUrl must use http or https.'
    }
    $env:HTTP_PROXY = $ProxyUrl
    $env:HTTPS_PROXY = $ProxyUrl
}

$gitDirectory = Join-Path $ResolvedVcpkgRoot '.git'
if (-not (Test-Path -LiteralPath $gitDirectory)) {
    if ((Get-ChildItem -LiteralPath $ResolvedVcpkgRoot -Force | Measure-Object).Count -ne 0) {
        throw "The vcpkg directory is non-empty and is not a Git repository: $ResolvedVcpkgRoot"
    }
    Invoke-NativeCommand -FilePath 'git' -Arguments @(
        'clone', '--filter=blob:none', '--no-checkout',
        'https://github.com/microsoft/vcpkg.git', $ResolvedVcpkgRoot
    )
} else {
    $pendingChanges = @(& git -C $ResolvedVcpkgRoot status --porcelain)
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to inspect the vcpkg working tree.'
    }
    if ($pendingChanges) {
        throw "The vcpkg working tree contains local changes: $ResolvedVcpkgRoot"
    }
}

& git -C $ResolvedVcpkgRoot cat-file -e "$VcpkgCommit`^{commit}"
if ($LASTEXITCODE -ne 0) {
    Invoke-NativeCommand -FilePath 'git' -Arguments @(
        '-C', $ResolvedVcpkgRoot, 'fetch', '--depth', '1', 'origin', $VcpkgCommit
    )
}
Invoke-NativeCommand -FilePath 'git' -Arguments @(
    '-C', $ResolvedVcpkgRoot, 'checkout', '--detach', $VcpkgCommit
)

$vcpkgExecutable = Join-Path $ResolvedVcpkgRoot 'vcpkg.exe'
if (-not (Test-Path -LiteralPath $vcpkgExecutable -PathType Leaf)) {
    $bootstrap = Join-Path $ResolvedVcpkgRoot 'bootstrap-vcpkg.bat'
    Invoke-NativeCommand -FilePath $bootstrap -Arguments @('-disableMetrics')
}

Invoke-NativeCommand -FilePath $vcpkgExecutable -Arguments @('version', '--disable-metrics')
New-Item -ItemType File -Path (Join-Path $ResolvedVcpkgRoot 'vcpkg.disable-metrics') -Force |
    Out-Null

$portManifestPath = Join-Path $ResolvedVcpkgRoot 'ports\ffmpeg\vcpkg.json'
$portManifest = Get-Content -LiteralPath $portManifestPath -Raw -Encoding UTF8 |
    ConvertFrom-Json
if ($portManifest.version -ne $ExpectedFFmpegVersion) {
    throw "The vcpkg FFmpeg port is $($portManifest.version); expected $ExpectedFFmpegVersion."
}
$pahoManifestPath = Join-Path $ResolvedVcpkgRoot 'ports\paho-mqtt\vcpkg.json'
$pahoManifest = Get-Content -LiteralPath $pahoManifestPath -Raw -Encoding UTF8 |
    ConvertFrom-Json
if ($pahoManifest.version -ne $ExpectedPahoVersion) {
    throw "The vcpkg Paho MQTT C port is $($pahoManifest.version); expected $ExpectedPahoVersion."
}

Invoke-NativeCommand -FilePath $vcpkgExecutable -Arguments @(
    'install',
    'ffmpeg[core,avcodec,avformat,swscale,swresample]:x64-windows',
    'paho-mqtt:x64-windows',
    '--clean-after-build'
)

$installedRoot = Join-Path $ResolvedVcpkgRoot 'installed\x64-windows'
$markerDirectory = Join-Path $installedRoot 'share\rtmp-monitor'
New-Item -ItemType Directory -Path $markerDirectory -Force | Out-Null
@(
    "FFmpegVersion=$ExpectedFFmpegVersion",
    "PahoMqttVersion=$ExpectedPahoVersion",
    'Triplet=x64-windows',
    'Features=core,avcodec,avformat,swscale,swresample',
    "VcpkgCommit=$VcpkgCommit",
    "InstalledRoot=$installedRoot"
) | Set-Content -LiteralPath (Join-Path $markerDirectory 'ffmpeg-env.txt') -Encoding UTF8

if ($PersistUserEnvironment) {
    [Environment]::SetEnvironmentVariable('VCPKG_ROOT', $ResolvedVcpkgRoot, 'User')
    [Environment]::SetEnvironmentVariable('VCPKG_DOWNLOADS', $ResolvedDownloadsRoot, 'User')
    [Environment]::SetEnvironmentVariable(
        'VCPKG_DEFAULT_BINARY_CACHE', $ResolvedBinaryCacheRoot, 'User')
    Add-UserPathEntry -Entry $ResolvedVcpkgRoot
}

[void](Test-FfmpegEnvironment)
Write-Host 'Open a new terminal only if -PersistUserEnvironment was requested.'
