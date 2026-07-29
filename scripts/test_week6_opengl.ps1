<#
.SYNOPSIS
    Builds and runs the Week 6 OpenGL validation on Windows x86_64.

.DESCRIPTION
    This script does not install dependencies or persist environment changes.
    Writable caches, logs, and reports stay under out/week6-opengl. Output
    paths on the C drive are rejected.

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File .\scripts\test_week6_opengl.ps1
#>

[CmdletBinding()]
param(
    [string]$Preset = "Qt-Debug",
    [string]$BuildDirectory = "",
    [string]$OutputRoot = "",
    [string]$QtRoot = "E:\QT6\6.6.1\msvc2019_64",
    [string]$VcVarsPath = "E:\C\VC\Auxiliary\Build\vcvars64.bat",
    [switch]$SkipConfigure
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory =
        Join-Path $ProjectRoot "out\build-windows-x64\debug"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $ProjectRoot "out\week6-opengl"
}

function Resolve-OutputPath {
    param([Parameter(Mandatory)][string]$Path)

    return [System.IO.Path]::GetFullPath($Path)
}

function Assert-NonSystemDriveOutput {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Description
    )

    $absolutePath = Resolve-OutputPath -Path $Path
    $root = [System.IO.Path]::GetPathRoot($absolutePath)
    if ($root.TrimEnd([char]92) -ieq "C:") {
        throw "$Description must not write to the C drive: $absolutePath"
    }
}

function Assert-File {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description does not exist: $Path"
    }
}

function Invoke-LoggedCommand {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string]$ReportPath
    )

    $commandText = "$FilePath " + ($Arguments -join " ")
    Add-Content `
        -LiteralPath $ReportPath `
        -Value "`n> $commandText" `
        -Encoding UTF8
    $commandOutput = @(& $FilePath @Arguments 2>&1)
    $commandExitCode = $LASTEXITCODE
    if ($commandOutput.Count -gt 0) {
        $commandOutput | ForEach-Object { Write-Host $_ }
        Add-Content `
            -LiteralPath $ReportPath `
            -Value ([string]::Join(
                [Environment]::NewLine,
                [string[]]$commandOutput
            )) `
            -Encoding UTF8
    }
    if ($commandExitCode -ne 0) {
        throw "Command failed with exit code ${commandExitCode}: $commandText"
    }
}

function Import-VisualCppEnvironment {
    param([Parameter(Mandatory)][string]$BatchPath)

    $env:VSCMD_SKIP_SENDTELEMETRY = "1"
    $environmentLines = & $env:ComSpec /d /s /c `
        "`"$BatchPath`" >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Visual C++ environment initialization failed: $BatchPath"
    }

    foreach ($line in $environmentLines) {
        $separatorIndex = $line.IndexOf("=")
        if ($separatorIndex -le 0) {
            continue
        }
        $name = $line.Substring(0, $separatorIndex)
        $value = $line.Substring($separatorIndex + 1)
        [Environment]::SetEnvironmentVariable(
            $name,
            $value,
            [EnvironmentVariableTarget]::Process
        )
    }
}

$BuildDirectory = Resolve-OutputPath -Path $BuildDirectory
$OutputRoot = Resolve-OutputPath -Path $OutputRoot
$QtRoot = Resolve-OutputPath -Path $QtRoot
$VcVarsPath = Resolve-OutputPath -Path $VcVarsPath
$WritablePaths = @(
    $BuildDirectory,
    $OutputRoot,
    (Join-Path $OutputRoot "tmp"),
    (Join-Path $OutputRoot "profile\LocalAppData"),
    (Join-Path $OutputRoot "profile\AppData")
)
foreach ($writablePath in $WritablePaths) {
    Assert-NonSystemDriveOutput `
        -Path $writablePath `
        -Description "Week 6 writable path"
}

$QmakePath = Join-Path $QtRoot "bin\qmake.exe"
Assert-File -Path $QmakePath -Description "Qt MSVC qmake"
Assert-File -Path $VcVarsPath -Description "Visual C++ vcvars64.bat"
$CmakeCommand = Get-Command cmake.exe -ErrorAction Stop
$CtestCommand = Get-Command ctest.exe -ErrorAction Stop

New-Item -ItemType Directory -Force -Path $WritablePaths | Out-Null
$ReportPath = Join-Path $OutputRoot "windows-opengl-validation.txt"
Set-Content -LiteralPath $ReportPath -Value @(
    "RtmpMonitor Week 6 Windows OpenGL validation"
    "generatedAt=$([DateTime]::UtcNow.ToString('o'))"
    "projectRoot=$ProjectRoot"
    "buildDirectory=$BuildDirectory"
    "qtRoot=$QtRoot"
    "cmake=$($CmakeCommand.Source)"
) -Encoding UTF8

$OriginalEnvironment = @{
    TEMP = $env:TEMP
    TMP = $env:TMP
    APPDATA = $env:APPDATA
    LOCALAPPDATA = $env:LOCALAPPDATA
    PATH = $env:PATH
    QT_OPENGL = $env:QT_OPENGL
    QT_QPA_PLATFORM = $env:QT_QPA_PLATFORM
}

try {
    $env:TEMP = Join-Path $OutputRoot "tmp"
    $env:TMP = $env:TEMP
    $env:APPDATA = Join-Path $OutputRoot "profile\AppData"
    $env:LOCALAPPDATA = Join-Path $OutputRoot "profile\LocalAppData"
    Import-VisualCppEnvironment -BatchPath $VcVarsPath
    $env:PATH = (Join-Path $QtRoot "bin") + ";" + $env:PATH
    $env:QT_OPENGL = "desktop"
    $env:QT_QPA_PLATFORM = "windows"

    $QmakeSpec = & $QmakePath -query QMAKE_SPEC
    if ($LASTEXITCODE -ne 0 -or $QmakeSpec.Trim() -ne "win32-msvc") {
        throw "The Qt kit is not win32-msvc: $QmakeSpec"
    }
    Add-Content -LiteralPath $ReportPath -Value @(
        "qmake=$QmakePath"
        "qmakeSpec=$($QmakeSpec.Trim())"
        "temp=$env:TEMP"
        "appData=$env:APPDATA"
        "localAppData=$env:LOCALAPPDATA"
    ) -Encoding UTF8

    if (-not $SkipConfigure) {
        Invoke-LoggedCommand `
            -FilePath $CmakeCommand.Source `
            -Arguments @(
                "--preset", $Preset,
                "-DBUILD_TESTING=ON",
                "-DRTMP_MONITOR_BUILD_OPENGL_PROTOTYPE=ON"
            ) `
            -ReportPath $ReportPath
    }

    Invoke-LoggedCommand `
        -FilePath $CmakeCommand.Source `
        -Arguments @("--build", $BuildDirectory, "--parallel") `
        -ReportPath $ReportPath

    Invoke-LoggedCommand `
        -FilePath $CtestCommand.Source `
        -Arguments @(
            "--test-dir", $BuildDirectory,
            "-C", "Debug",
            "-L", "opengl",
            "-V"
        ) `
        -ReportPath $ReportPath

    Invoke-LoggedCommand `
        -FilePath $CtestCommand.Source `
        -Arguments @(
            "--test-dir", $BuildDirectory,
            "-C", "Debug",
            "--output-on-failure"
        ) `
        -ReportPath $ReportPath

    Write-Host "Week 6 Windows OpenGL validation passed." -ForegroundColor Green
    Write-Host "Report: $ReportPath"
}
finally {
    foreach ($name in $OriginalEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable(
            $name,
            $OriginalEnvironment[$name],
            [EnvironmentVariableTarget]::Process
        )
    }
}
