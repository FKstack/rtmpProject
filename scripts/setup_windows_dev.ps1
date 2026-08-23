[CmdletBinding()]
param(
    [ValidateSet('Check', 'Setup', 'Configure', 'Build', 'Test', 'All', 'SelfTest')]
    [string]$Action = 'Check',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [string]$QtRoot = $env:QTDIR,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$ToolRoot,
    [string]$DownloadsRoot,
    [string]$BinaryCacheRoot,
    [string]$TemporaryRoot,
    [string]$BuildRoot,
    [string]$VsDevCmd,
    [string]$ProxyUrl,
    [switch]$ForceUserPreset,
    [switch]$PersistEnvironment,
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

function Assert-WritePathPolicy {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not $RequireNonSystemDrive) {
        return
    }
    $resolved = Resolve-FullPath -Path $Path
    $systemDrive = [System.IO.Path]::GetPathRoot($env:SystemRoot)
    if ([System.IO.Path]::GetPathRoot($resolved) -ieq $systemDrive) {
        throw "Task-owned files must not be written to the system drive: $resolved"
    }
}

function Find-VsDevCmd {
    if (-not [string]::IsNullOrWhiteSpace($VsDevCmd)) {
        $resolved = Resolve-FullPath -Path $VsDevCmd
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "VsDevCmd does not exist: $resolved"
        }
        return $resolved
    }

    if (-not [string]::IsNullOrWhiteSpace($env:VSINSTALLDIR)) {
        $candidate = Join-Path $env:VSINSTALLDIR 'Common7\Tools\VsDevCmd.bat'
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-FullPath -Path $candidate)
        }
    }

    $vswhereCandidates = [System.Collections.Generic.List[string]]::new()
    $vswhereCommand = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($vswhereCommand) {
        $vswhereCandidates.Add($vswhereCommand.Source)
    }
    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        $vswhereCandidates.Add((Join-Path $programFilesX86 `
            'Microsoft Visual Studio\Installer\vswhere.exe'))
    }
    foreach ($vswherePath in $vswhereCandidates) {
        if (-not (Test-Path -LiteralPath $vswherePath -PathType Leaf)) {
            continue
        }
        $installationPath = (& $vswherePath -latest -products '*' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath).Trim()
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($installationPath)) {
            $candidate = Join-Path $installationPath 'Common7\Tools\VsDevCmd.bat'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return (Resolve-FullPath -Path $candidate)
            }
        }
    }

    throw 'Visual Studio C++ tools were not found. Install the Desktop development with C++ workload or pass -VsDevCmd.'
}

function Get-DeveloperEnvironment {
    param([Parameter(Mandatory = $true)][string]$BatchPath)

    $cmd = Join-Path $env:SystemRoot 'System32\cmd.exe'
    $lines = & $cmd /d /s /c "`"$BatchPath`" -arch=x64 -host_arch=x64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to initialize Visual Studio environment: $BatchPath"
    }
    $result = [System.Collections.Generic.Dictionary[string,string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($line in $lines) {
        $separator = $line.IndexOf('=')
        if ($separator -le 0) {
            continue
        }
        $result[$line.Substring(0, $separator)] = $line.Substring($separator + 1)
    }
    if (-not $result.ContainsKey('VCToolsInstallDir')) {
        throw 'Visual Studio environment did not provide VCToolsInstallDir.'
    }
    return $result
}

function Find-ExecutableInEnvironment {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Environment,
        [switch]$AllowMissing
    )

    foreach ($directory in @($Environment['Path'] -split ';')) {
        if ([string]::IsNullOrWhiteSpace($directory)) {
            continue
        }
        $candidate = Join-Path $directory $Name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-FullPath -Path $candidate)
        }
    }
    if ($AllowMissing) {
        return $null
    }
    throw "Required command was not found in the Visual Studio environment: $Name"
}

function Get-VisualStudioInstallationRoot {
    param([Parameter(Mandatory = $true)][string]$BatchPath)

    $toolsDirectory = Split-Path -Parent (Resolve-FullPath -Path $BatchPath)
    $common7Directory = Split-Path -Parent $toolsDirectory
    return (Resolve-FullPath -Path (Split-Path -Parent $common7Directory))
}

function Find-VisualStudioBundledExecutable {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Environment,
        [Parameter(Mandatory = $true)][string]$BatchPath
    )

    $fromPath = Find-ExecutableInEnvironment -Name $Name `
        -Environment $Environment -AllowMissing
    if (-not [string]::IsNullOrWhiteSpace($fromPath)) {
        return $fromPath
    }

    $installationRoot = Get-VisualStudioInstallationRoot -BatchPath $BatchPath
    $relativeCandidates = switch ($Name.ToLowerInvariant()) {
        'cmake.exe' {
            'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        }
        'ctest.exe' {
            'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
        }
        'ninja.exe' {
            'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
        }
        default { @() }
    }
    foreach ($relativePath in @($relativeCandidates)) {
        $candidate = Join-Path $installationRoot $relativePath
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-FullPath -Path $candidate)
        }
    }

    throw "Required command was not found in the Visual Studio environment or installation: $Name"
}

function ConvertTo-NativeArgument {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Value)

    if ($Value.Length -eq 0) {
        return '""'
    }
    if ($Value -notmatch '[\s"]') {
        return $Value
    }
    return '"' + $Value.Replace('"', '\"') + '"'
}

function New-CleanEnvironment {
    param([Parameter(Mandatory = $true)]$DeveloperEnvironment)

    $result = [System.Collections.Generic.Dictionary[string,string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $DeveloperEnvironment.GetEnumerator()) {
        $result[$entry.Key] = $entry.Value
    }

    $filteredPath = @($DeveloperEnvironment['Path'] -split ';' | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_) -and
        $_ -notmatch '(?i)[\\/](mingw|msys)(?:[\\/]|$)'
    })
    $prefix = @(
        (Join-Path $script:ResolvedQtRoot 'bin'),
        (Join-Path $script:ResolvedVcpkgRoot 'installed\x64-windows\bin'),
        (Join-Path $script:ResolvedVcpkgRoot 'installed\x64-windows\debug\bin'),
        $script:BuildDirectory
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    $result['Path'] = (@($prefix) + $filteredPath -join ';')
    $result['TEMP'] = $script:ResolvedTemporaryRoot
    $result['TMP'] = $script:ResolvedTemporaryRoot
    $result['QTDIR'] = $script:ResolvedQtRoot
    $result['VCPKG_ROOT'] = $script:ResolvedVcpkgRoot
    $result['VCPKG_DOWNLOADS'] = $script:ResolvedDownloadsRoot
    $result['VCPKG_DEFAULT_BINARY_CACHE'] = $script:ResolvedBinaryCacheRoot
    $result['VCPKG_DISABLE_METRICS'] = '1'
    return $result
}

function Invoke-CleanNative {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Arguments,
        [Parameter(Mandatory = $true)]$Environment
    )

    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $FilePath
    $startInfo.Arguments = ($Arguments | ForEach-Object {
        ConvertTo-NativeArgument -Value $_
    }) -join ' '
    $startInfo.WorkingDirectory = $script:SourceRoot
    $startInfo.UseShellExecute = $false
    $startInfo.EnvironmentVariables.Clear()
    foreach ($entry in $Environment.GetEnumerator()) {
        if (-not $startInfo.EnvironmentVariables.ContainsKey($entry.Key)) {
            $startInfo.EnvironmentVariables[$entry.Key] = $entry.Value
        }
    }

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    [void]$process.Start()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "Command failed with exit code $($process.ExitCode): $FilePath $($Arguments -join ' ')"
    }
}

function Test-QtInstallation {
    $required = @(
        (Join-Path $script:ResolvedQtRoot 'bin\qmake.exe'),
        (Join-Path $script:ResolvedQtRoot 'lib\cmake\Qt6\Qt6Config.cmake')
    )
    foreach ($path in $required) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required Qt MSVC file is missing: $path"
        }
    }
    $qmake = $required[0]
    $qtVersion = (& $qmake -query QT_VERSION).Trim()
    $qtSpec = (& $qmake -query QMAKE_XSPEC).Trim()
    if ($LASTEXITCODE -ne 0 -or $qtVersion -ne '6.6.1') {
        throw "Qt 6.6.1 is required; qmake reported '$qtVersion'."
    }
    if ($qtSpec -notmatch '(?i)msvc') {
        throw "The selected Qt kit is not an MSVC kit: $qtSpec"
    }
    Write-Host "Qt: $qtVersion ($qtSpec)"
}

function Invoke-FfmpegSetup {
    param(
        [Parameter(Mandatory = $true)][ValidateSet('Check', 'Install')]
        [string]$FfmpegAction,
        [Parameter(Mandatory = $true)]$Environment
    )

    $arguments = @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', (Join-Path $PSScriptRoot 'setup_ffmpeg_windows_dev.ps1'),
        '-Action', $FfmpegAction,
        '-VcpkgRoot', $script:ResolvedVcpkgRoot,
        '-DownloadsRoot', $script:ResolvedDownloadsRoot,
        '-BinaryCacheRoot', $script:ResolvedBinaryCacheRoot,
        '-TemporaryRoot', $script:ResolvedTemporaryRoot
    )
    if (-not [string]::IsNullOrWhiteSpace($ProxyUrl)) {
        $arguments += @('-ProxyUrl', $ProxyUrl)
    }
    if ($PersistEnvironment) {
        $arguments += '-PersistUserEnvironment'
    }
    if ($RequireNonSystemDrive) {
        $arguments += '-RequireNonSystemDrive'
    }
    Invoke-CleanNative -FilePath (Join-Path $env:SystemRoot `
        'System32\WindowsPowerShell\v1.0\powershell.exe') `
        -Arguments $arguments -Environment $Environment
}

function Write-UserPreset {
    $examplePath = Join-Path $script:SourceRoot 'CMakeUserPresets.example.json'
    $userPath = Join-Path $script:SourceRoot 'CMakeUserPresets.json'
    if (Test-Path -LiteralPath $userPath) {
        if (-not $ForceUserPreset) {
            throw "CMakeUserPresets.json already exists; refusing to overwrite it. Use -ForceUserPreset only after backing it up."
        }
    }
    $content = Get-Content -LiteralPath $examplePath -Raw -Encoding UTF8
    $content = $content.Replace('<Qt-msvc-root>',
        $script:ResolvedQtRoot.Replace('\', '/'))
    $content = $content.Replace('<vcpkg-root>',
        $script:ResolvedVcpkgRoot.Replace('\', '/'))
    [void]($content | ConvertFrom-Json)
    Set-Content -LiteralPath $userPath -Value $content -Encoding UTF8
    Write-Host "Generated ignored local preset: $userPath"
}

function Invoke-SelfTest {
    $examplePath = Join-Path $script:SourceRoot 'CMakeUserPresets.example.json'
    $content = Get-Content -LiteralPath $examplePath -Raw -Encoding UTF8
    [void]($content | ConvertFrom-Json)
    foreach ($placeholder in @('<Qt-msvc-root>', '<vcpkg-root>')) {
        if (-not $content.Contains($placeholder)) {
            throw "Preset example placeholder is missing: $placeholder"
        }
    }
    if ($content -match '(?i)[A-Z]:[\\/]Users[\\/]') {
        throw 'Preset example contains a personal Windows path.'
    }
    if ($RequireNonSystemDrive) {
        $blocked = $false
        try {
            Assert-WritePathPolicy -Path (Join-Path `
                ([System.IO.Path]::GetPathRoot($env:SystemRoot)) 'rtmp-monitor-self-test')
        } catch {
            $blocked = $true
        }
        if (-not $blocked) {
            throw 'System-drive path policy self-test failed.'
        }
    }

    $selfTestRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
        ("rtmp-monitor-vs-tool-self-test-{0}" -f [Guid]::NewGuid().ToString('N'))
    try {
        $fakeVsDevCmd = Join-Path $selfTestRoot 'Common7\Tools\VsDevCmd.bat'
        $fakeNinja = Join-Path $selfTestRoot `
            'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
        [void](New-Item -ItemType Directory -Force `
            -Path (Split-Path -Parent $fakeVsDevCmd))
        [void](New-Item -ItemType Directory -Force `
            -Path (Split-Path -Parent $fakeNinja))
        [void](New-Item -ItemType File -Force -Path $fakeVsDevCmd)
        [void](New-Item -ItemType File -Force -Path $fakeNinja)

        $emptyEnvironment = [System.Collections.Generic.Dictionary[string,string]]::new(
            [System.StringComparer]::OrdinalIgnoreCase)
        $emptyEnvironment['Path'] = ''
        $resolvedNinja = Find-VisualStudioBundledExecutable `
            -Name 'ninja.exe' -Environment $emptyEnvironment `
            -BatchPath $fakeVsDevCmd
        if ($resolvedNinja -ne (Resolve-FullPath -Path $fakeNinja)) {
            throw 'Visual Studio bundled Ninja fallback self-test failed.'
        }
    } finally {
        if (Test-Path -LiteralPath $selfTestRoot) {
            Remove-Item -LiteralPath $selfTestRoot -Recurse -Force
        }
    }
    Write-Host 'Windows development setup self-test passed.'
}

$SourceRoot = Resolve-FullPath -Path (Join-Path $PSScriptRoot '..')
if ($Action -eq 'SelfTest') {
    Invoke-SelfTest
    return
}

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    throw 'QtRoot is required. Pass -QtRoot or set QTDIR to a Qt 6.6.1 MSVC x64 kit.'
}
$ResolvedQtRoot = Resolve-FullPath -Path $QtRoot

if ([string]::IsNullOrWhiteSpace($ToolRoot) -and
    [string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    throw 'Pass -ToolRoot for a project-owned dependency area, or pass -VcpkgRoot/set VCPKG_ROOT.'
}
if (-not [string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ResolvedToolRoot = Resolve-FullPath -Path $ToolRoot
} else {
    $ResolvedToolRoot = Resolve-FullPath -Path (Split-Path -Parent $VcpkgRoot)
}
if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    $VcpkgRoot = Join-Path $ResolvedToolRoot 'vcpkg'
}
$ResolvedVcpkgRoot = Resolve-FullPath -Path $VcpkgRoot
if ([string]::IsNullOrWhiteSpace($DownloadsRoot)) {
    $DownloadsRoot = Join-Path $ResolvedToolRoot 'vcpkg-downloads'
}
if ([string]::IsNullOrWhiteSpace($BinaryCacheRoot)) {
    $BinaryCacheRoot = Join-Path $ResolvedToolRoot 'vcpkg-binary-cache'
}
if ([string]::IsNullOrWhiteSpace($TemporaryRoot)) {
    $TemporaryRoot = Join-Path $ResolvedToolRoot 'temp'
}
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $SourceRoot 'out\build-windows-x64'
}
$ResolvedDownloadsRoot = Resolve-FullPath -Path $DownloadsRoot
$ResolvedBinaryCacheRoot = Resolve-FullPath -Path $BinaryCacheRoot
$ResolvedTemporaryRoot = Resolve-FullPath -Path $TemporaryRoot
$ResolvedBuildRoot = Resolve-FullPath -Path $BuildRoot
$BuildDirectory = Join-Path $ResolvedBuildRoot $Configuration.ToLowerInvariant()

foreach ($path in @(
    $ResolvedToolRoot,
    $ResolvedVcpkgRoot,
    $ResolvedDownloadsRoot,
    $ResolvedBinaryCacheRoot,
    $ResolvedTemporaryRoot,
    $ResolvedBuildRoot
)) {
    Assert-WritePathPolicy -Path $path
}
New-Item -ItemType Directory -Path $ResolvedTemporaryRoot -Force | Out-Null

$resolvedVsDevCmd = Find-VsDevCmd
$developerEnvironment = Get-DeveloperEnvironment -BatchPath $resolvedVsDevCmd
$cleanEnvironment = New-CleanEnvironment -DeveloperEnvironment $developerEnvironment
$cmake = Find-VisualStudioBundledExecutable -Name 'cmake.exe' `
    -Environment $cleanEnvironment -BatchPath $resolvedVsDevCmd
$ctest = Find-VisualStudioBundledExecutable -Name 'ctest.exe' `
    -Environment $cleanEnvironment -BatchPath $resolvedVsDevCmd
$ninja = Find-VisualStudioBundledExecutable -Name 'ninja.exe' `
    -Environment $cleanEnvironment -BatchPath $resolvedVsDevCmd
$cl = Find-ExecutableInEnvironment -Name 'cl.exe' -Environment $cleanEnvironment

Write-Host "MSVC: $((Get-Item -LiteralPath $cl).VersionInfo.FileVersion)"
Write-Host "CMake: $cmake"
Write-Host "Ninja: $ninja"
Test-QtInstallation

if ($Action -in @('Setup', 'All')) {
    Invoke-FfmpegSetup -FfmpegAction Install -Environment $cleanEnvironment
} else {
    Invoke-FfmpegSetup -FfmpegAction Check -Environment $cleanEnvironment
}

if ($Action -eq 'Check' -or $Action -eq 'Setup') {
    Write-Host 'Windows development environment check passed.'
    return
}

if ($Action -in @('Configure', 'All')) {
    Write-UserPreset
    Invoke-CleanNative -FilePath $cmake -Arguments @(
        '--preset', "Qt-$Configuration", '--fresh', '-B', $BuildDirectory
    ) -Environment $cleanEnvironment
}
if ($Action -in @('Build', 'All')) {
    Invoke-CleanNative -FilePath $cmake -Arguments @(
        '--build', $BuildDirectory, '--parallel'
    ) -Environment $cleanEnvironment
}
if ($Action -in @('Test', 'All')) {
    Invoke-CleanNative -FilePath $ctest -Arguments @(
        '--test-dir', $BuildDirectory, '--output-on-failure'
    ) -Environment $cleanEnvironment
}

Write-Host "Windows development action '$Action' completed for $Configuration."
