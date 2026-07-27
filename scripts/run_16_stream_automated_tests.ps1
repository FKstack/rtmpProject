<#
.SYNOPSIS
    Week 4 16 路自动化验收总控。

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File .\scripts\run_16_stream_automated_tests.ps1 -Action Check -Suite All
.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File .\scripts\run_16_stream_automated_tests.ps1 -Action Run -Suite Unit
#>
[CmdletBinding()]
param(
    [ValidateSet("Check", "Run", "Stop")]
    [string]$Action = "Check",
    [ValidateSet("Unit", "Video", "LiveLatency", "All")]
    [string]$Suite = "All",
    [ValidateRange(30, 7200)]
    [int]$VideoDurationSeconds = 600,
    [ValidateRange(30, 7200)]
    [int]$LiveLatencyDurationSeconds = 600,
    [ValidateRange(1, 120)]
    [int]$WarmupSeconds = 20,
    [string]$BuildDirectory = "",
    [string]$VcvarsPath = "E:\C\VC\Auxiliary\Build\vcvars64.bat",
    [string]$QtRoot = "E:\QT6\6.6.1\msvc2019_64",
    [string]$OutputRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $ProjectRoot "out\build-windows-x64\debug"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $ProjectRoot "out\16-stream-validation"
}
$VideoScript = Join-Path $PSScriptRoot "test_16_stream_video.ps1"
$LiveLatencyScript = Join-Path $PSScriptRoot "test_16_stream_live_latency.ps1"
$SummaryJson = Join-Path $OutputRoot "summary.json"
$SummaryMarkdown = Join-Path $OutputRoot "summary.md"

function Write-Stage {
    param([string]$Text)
    Write-Host ""
    Write-Host ("=" * 72) -ForegroundColor DarkGray
    Write-Host $Text -ForegroundColor Cyan
    Write-Host ("=" * 72) -ForegroundColor DarkGray
}

function Test-IncludesSuite {
    param([string]$Name)
    return $Suite -eq "All" -or $Suite -eq $Name
}

function Invoke-ChildScript {
    param([string]$Path, [string[]]$Arguments)
    $powerShellPath = Join-Path $PSHOME "powershell.exe"
    & $powerShellPath -NoProfile -ExecutionPolicy Bypass -File $Path @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$(Split-Path -Leaf $Path) 退出码 $LASTEXITCODE"
    }
}

function Stop-TestProcesses {
    foreach ($script in @($VideoScript, $LiveLatencyScript)) {
        try {
            Invoke-ChildScript $script @("-Action", "Stop")
        } catch {
            Write-Warning $_.Exception.Message
        }
    }
}

function Invoke-Check {
    Write-Stage "总控：静态环境检查（Suite=$Suite）"
    foreach ($path in @($VideoScript, $LiveLatencyScript)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "缺少脚本：$path"
        }
        $tokens = $null
        $errors = $null
        [Management.Automation.Language.Parser]::ParseFile(
            $path, [ref]$tokens, [ref]$errors
        ) | Out-Null
        if ($errors.Count -ne 0) {
            throw "$path 存在 PowerShell 语法错误：$($errors[0].Message)"
        }
    }
    if (Test-IncludesSuite "Unit") {
        if (-not (Test-Path -LiteralPath $VcvarsPath -PathType Leaf)) {
            throw "找不到 Visual C++ 环境脚本：$VcvarsPath"
        }
        if (-not (Test-Path -LiteralPath (Join-Path $BuildDirectory "CTestTestfile.cmake"))) {
            Write-Host "提示：构建目录尚未配置；Run 将先执行 CMake preset 配置。"
        }
    }
    if (Test-IncludesSuite "Video") {
        Invoke-ChildScript $VideoScript @("-Action", "Check")
    }
    if (Test-IncludesSuite "LiveLatency") {
        Invoke-ChildScript $LiveLatencyScript @("-Action", "Check")
    }
    Write-Host "[通过] 总控及所选套件环境检查完成。"
}

function Invoke-UnitSuite {
    Write-Stage "Unit：Windows Debug 配置、构建和完整 CTest"
    if (-not (Test-Path -LiteralPath $QtRoot -PathType Container)) {
        throw "Qt MSVC 目录不存在：$QtRoot"
    }
    $configure = ""
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDirectory "CMakeCache.txt"))) {
        $configure = 'cmake -S "{0}" -B "{1}" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH="{2}" && ' -f `
            $ProjectRoot, $BuildDirectory, $QtRoot
    }
    $command = 'call "{0}" >nul && {1}cmake --build "{2}" --config Debug && ctest --test-dir "{2}" -C Debug --output-on-failure' -f `
        $VcvarsPath, $configure, $BuildDirectory
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw "Windows Debug 构建或 CTest 失败。" }
}

function Invoke-Run {
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
    $results = New-Object Collections.Generic.List[object]
    $failed = $false
    try {
        Invoke-Check
        if (Test-IncludesSuite "Unit") {
            try {
                Invoke-UnitSuite
                $results.Add([pscustomobject]@{ Suite = "Unit"; Passed = $true; Detail = "完整 CTest 通过" })
            } catch {
                $failed = $true
                $results.Add([pscustomobject]@{ Suite = "Unit"; Passed = $false; Detail = $_.Exception.Message })
            }
        }
        if ((Test-IncludesSuite "Video") -and -not $failed) {
            try {
                Write-Stage "Video：16 路预录视频连续验收"
                Invoke-ChildScript $VideoScript @(
                    "-Action", "RunAutomated",
                    "-DurationSeconds", [string]$VideoDurationSeconds,
                    "-WarmupSeconds", [string]$WarmupSeconds
                )
                $results.Add([pscustomobject]@{ Suite = "Video"; Passed = $true; Detail = "视频报告通过" })
            } catch {
                $failed = $true
                $results.Add([pscustomobject]@{ Suite = "Video"; Passed = $false; Detail = $_.Exception.Message })
            }
        }
        if ((Test-IncludesSuite "LiveLatency") -and -not $failed) {
            try {
                Write-Stage "LiveLatency：16 路双屏源到显示延迟验收"
                Invoke-ChildScript $LiveLatencyScript @(
                    "-Action", "RunAutomated",
                    "-DurationSeconds", [string]$LiveLatencyDurationSeconds,
                    "-WarmupSeconds", [string]$WarmupSeconds
                )
                $results.Add([pscustomobject]@{ Suite = "LiveLatency"; Passed = $true; Detail = "延迟报告通过" })
            } catch {
                $failed = $true
                $results.Add([pscustomobject]@{ Suite = "LiveLatency"; Passed = $false; Detail = $_.Exception.Message })
            }
        }
    } finally {
        Stop-TestProcesses
        $summary = [ordered]@{
            SchemaVersion = 1
            CompletedAtUtc = [DateTime]::UtcNow.ToString("O")
            RequestedSuite = $Suite
            Passed = -not $failed
            Results = $results
            VideoReport = Join-Path $ProjectRoot "out\16-stream-video\automated-report.json"
            LiveLatencyReport = Join-Path $ProjectRoot "out\16-stream-live-latency\latency-report.json"
        }
        $summary | ConvertTo-Json -Depth 6 |
            Set-Content -LiteralPath $SummaryJson -Encoding UTF8
        $rows = @($results | ForEach-Object {
            "| $($_.Suite) | $($_.Passed) | $($_.Detail -replace '\|','/') |"
        })
        @(
            "# Week 4 16 路自动化验收汇总", "",
            "- 完成时间（UTC）：$($summary.CompletedAtUtc)",
            "- 请求套件：$Suite",
            "- 总结果：$(if ($summary.Passed) {'通过'} else {'未通过'})", "",
            "| 套件 | 通过 | 说明 |",
            "|---|---|---|"
        ) + $rows | Set-Content -LiteralPath $SummaryMarkdown -Encoding UTF8
        Write-Host "总报告：$SummaryMarkdown"
    }
    if ($failed) { throw "一个或多个 16 路验收套件未通过，请查看总报告。" }
}

switch ($Action) {
    "Check" { Invoke-Check }
    "Run" { Invoke-Run }
    "Stop" { Stop-TestProcesses }
}
