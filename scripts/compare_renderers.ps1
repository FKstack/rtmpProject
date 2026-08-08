<#
.SYNOPSIS
    在同一台 Windows 机器上对 CPU 与 OpenGL 视频后端做可复现 A/B 验收。

.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File .\scripts\compare_renderers.ps1 -Action Check -Suite All
.EXAMPLE
    powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File .\scripts\compare_renderers.ps1 -Action Run -Suite All `
        -VideoDurationSeconds 600 -LiveLatencyDurationSeconds 600
#>
[CmdletBinding()]
param(
    [ValidateSet("Check", "Run", "Status", "Stop", "SelfTest")]
    [string]$Action = "Check",
    [ValidateSet("Video", "LiveLatency", "Quality", "All")]
    [string]$Suite = "All",
    [ValidateRange(30, 7200)]
    [int]$VideoDurationSeconds = 600,
    [ValidateRange(30, 7200)]
    [int]$LiveLatencyDurationSeconds = 600,
    [ValidateRange(1, 120)]
    [int]$WarmupSeconds = 20,
    [ValidateRange(0, 300)]
    [int]$CooldownSeconds = 30,
    [ValidatePattern("^[A-Za-z0-9_-]+$")]
    [string]$StreamPrefix = "renderer_comparison_",
    [string]$BuildDirectory = "",
    [string]$OutputRoot = "",
    [switch]$RecalculateSummary
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $ProjectRoot "out\build-windows-x64\debug"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $ProjectRoot "out\renderer-comparison"
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$VideoScript = Join-Path $PSScriptRoot "test_16_stream_video.ps1"
$LatencyScript = Join-Path $PSScriptRoot "test_16_stream_live_latency.ps1"
$OpenGlScript = Join-Path $PSScriptRoot "test_week6_opengl.ps1"
$StatePath = Join-Path $OutputRoot "comparison-state.json"
$ReportJsonPath = Join-Path $OutputRoot "comparison.json"
$ReportMarkdownPath = Join-Path $OutputRoot "comparison.md"
$PowerShellPath = Join-Path $PSHOME "powershell.exe"

function Test-IncludesSuite {
    param([string]$Name)
    return $Suite -eq "All" -or $Suite -eq $Name
}

function Write-Stage {
    param([string]$Text)
    Write-Host ""
    Write-Host ("=" * 76) -ForegroundColor DarkGray
    Write-Host $Text -ForegroundColor Cyan
    Write-Host ("=" * 76) -ForegroundColor DarkGray
}

function Invoke-ChildScript {
    param([string]$Path, [string[]]$Arguments, [switch]$AllowReportFailure)
    $previousErrorAction = $ErrorActionPreference
    try {
        # A child that intentionally reports a failed performance gate writes to
        # stderr. Capture it without turning that native stderr record into a
        # terminating PowerShell exception; the numeric exit code remains the
        # authoritative result below.
        $ErrorActionPreference = "Continue"
        $childOutput = @(& $PowerShellPath -NoProfile -ExecutionPolicy Bypass `
            -File $Path @Arguments 2>&1)
        $exitCode = [int]$LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorAction
    }
    $childOutput | ForEach-Object { Write-Host $_ }
    if ($exitCode -ne 0 -and -not $AllowReportFailure) {
        throw "$(Split-Path -Leaf $Path) 退出码 $exitCode"
    }
    return $exitCode
}

function Read-Json {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "缺少报告：$Path"
    }
    return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Get-Percentile {
    param([double[]]$Values, [double]$Fraction)
    if ($Values.Count -eq 0) { return -1.0 }
    $sorted = @($Values | Sort-Object)
    $index = [Math]::Max(0, [Math]::Min(
        $sorted.Count - 1, [Math]::Ceiling($Fraction * $sorted.Count) - 1
    ))
    return [double]$sorted[$index]
}

function Get-ImprovementPercent {
    param([double]$Baseline, [double]$Candidate)
    if ($Baseline -le 0) { return 0.0 }
    return 100.0 * ($Baseline - $Candidate) / $Baseline
}

function Get-LinearSlopePerMinute {
    param([double[]]$Values)
    if ($Values.Count -lt 2) { return 0.0 }
    $meanX = ($Values.Count - 1) / 2.0
    $meanY = [double](($Values | Measure-Object -Average).Average)
    $numerator = 0.0
    $denominator = 0.0
    for ($index = 0; $index -lt $Values.Count; ++$index) {
        $dx = $index - $meanX
        $numerator += $dx * ($Values[$index] - $meanY)
        $denominator += $dx * $dx
    }
    if ($denominator -le 0) { return 0.0 }
    return 60.0 * $numerator / $denominator
}

function Get-WindowGrowth {
    param([object[]]$Samples, [string]$Property)
    if ($Samples.Count -eq 0) { return 0.0 }
    $window = [Math]::Max(1, [Math]::Min(60, [int]($Samples.Count / 3)))
    $first = [double](($Samples | Select-Object -First $window |
        Measure-Object -Property $Property -Average).Average)
    $last = [double](($Samples | Select-Object -Last $window |
        Measure-Object -Property $Property -Average).Average)
    return $last - $first
}

function Test-TextureStable {
    param([object[]]$Samples)
    $steady = @($Samples | Select-Object -Skip ([Math]::Min(60, $Samples.Count)))
    if ($steady.Count -eq 0) { $steady = $Samples }
    if ($steady.Count -eq 0) { return $false }
    $expected = [double]$steady[0].TextureBytes
    $minimum = [double](($steady | Measure-Object TextureBytes -Minimum).Minimum)
    $maximum = [double](($steady | Measure-Object TextureBytes -Maximum).Maximum)
    $tail = @($steady | Select-Object -Last ([Math]::Min(60, $steady.Count)))
    $tailMinimum = [double](($tail | Measure-Object TextureBytes -Minimum).Minimum)
    $tailMaximum = [double](($tail | Measure-Object TextureBytes -Maximum).Maximum)
    # Planned Camera 03 failure is allowed to release one route temporarily.
    # Stability means no allocation above the warmed baseline and a complete
    # return to that baseline for the final minute.
    return $expected -gt 0 -and $minimum -gt 0 -and
        $maximum -le $expected -and
        $tailMinimum -eq $expected -and $tailMaximum -eq $expected
}

function Test-SensitiveText {
    param([string]$Text)
    return $Text -match '(?i)rtmp://' -or
        $Text -match '(?i)[A-Z]:\\' -or
        $Text -match '(?i)(token|password|api[_-]?key)\s*[:=]'
}

function Add-Gate {
    param(
        [Collections.Generic.List[object]]$Gates,
        [string]$Name,
        [bool]$Passed,
        [object]$Cpu,
        [object]$OpenGL,
        [string]$Requirement
    )
    $Gates.Add([pscustomobject]@{
        Name = $Name
        Passed = $Passed
        CPU = $Cpu
        OpenGL = $OpenGL
        Requirement = $Requirement
    })
}

function Set-State {
    param([string]$Status, [string]$Stage, [string]$Message)
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
    $json = [ordered]@{
        SchemaVersion = 1
        UpdatedAtUtc = [DateTime]::UtcNow.ToString("O")
        Status = $Status
        Stage = $Stage
        Message = $Message
    } | ConvertTo-Json
    $temporaryPath = "$StatePath.$PID.tmp"
    [IO.File]::WriteAllText(
        $temporaryPath,
        $json,
        [Text.UTF8Encoding]::new($false)
    )
    for ($attempt = 1; $attempt -le 20; ++$attempt) {
        try {
            Move-Item -LiteralPath $temporaryPath -Destination $StatePath -Force
            return
        } catch [IO.IOException] {
            if ($attempt -eq 20) { throw }
            Start-Sleep -Milliseconds 100
        }
    }
}

function Invoke-Check {
    Write-Stage "Renderer A/B：环境与脚本检查（Suite=$Suite）"
    foreach ($path in @($VideoScript, $LatencyScript, $OpenGlScript)) {
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
    if (-not (Test-Path -LiteralPath (Join-Path $BuildDirectory "rtmp_monitor.exe"))) {
        throw "缺少 Windows Debug 主程序：$BuildDirectory"
    }
    if (Test-IncludesSuite "Video") {
        Invoke-ChildScript $VideoScript @("-Action", "Check") | Out-Null
    }
    if (Test-IncludesSuite "LiveLatency") {
        Invoke-ChildScript $LatencyScript @("-Action", "Check") | Out-Null
    }
    Write-Host "[通过] 对照测试前置检查完成。" -ForegroundColor Green
}

function Invoke-Quality {
    $qualityRoot = Join-Path $OutputRoot "quality"
    $artifactRoot = Join-Path $qualityRoot "artifacts"
    New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
    $originalArtifactRoot = $env:RTMP_MONITOR_TEST_ARTIFACT_DIR
    try {
        $env:RTMP_MONITOR_TEST_ARTIFACT_DIR = $artifactRoot
        Invoke-ChildScript $OpenGlScript @(
            "-SkipConfigure",
            "-BuildDirectory", $BuildDirectory,
            "-OutputRoot", $qualityRoot
        ) | Out-Null
    } finally {
        $env:RTMP_MONITOR_TEST_ARTIFACT_DIR = $originalArtifactRoot
    }
    return $true
}

function Invoke-ScenarioRun {
    param([string]$Scenario, [string]$Renderer, [string]$SharedAssetDirectory = "")
    $scenarioRoot = Join-Path $OutputRoot ("{0}\{1}" -f $Scenario, $Renderer)
    $script = if ($Scenario -eq "video") { $VideoScript } else { $LatencyScript }
    $duration = if ($Scenario -eq "video") {
        $VideoDurationSeconds
    } else {
        $LiveLatencyDurationSeconds
    }
    Set-State "running" "$Scenario-$Renderer" "正在进行正式采样。"
    $arguments = @(
        "-Action", "RunAutomated",
        "-DurationSeconds", [string]$duration,
        "-WarmupSeconds", [string]$WarmupSeconds,
        "-Renderer", $Renderer,
        "-OutputRoot", $scenarioRoot,
        "-StreamPrefix", $(if ($Scenario -eq "video") {
            "${StreamPrefix}video_"
        } else {
            "${StreamPrefix}latency_"
        })
    )
    if ($Scenario -eq "video") {
        $arguments += @("-AssetDirectory", $SharedAssetDirectory)
    }
    $exitCode = Invoke-ChildScript $script $arguments -AllowReportFailure
    $reportName = if ($Scenario -eq "video") {
        "automated-report.json"
    } else {
        "latency-report.json"
    }
    $reportPath = Join-Path $scenarioRoot $reportName
    if (-not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
        throw "$Scenario/$Renderer 未生成报告，退出码 $exitCode。"
    }
    if ($exitCode -ne 0) {
        Write-Warning "$Scenario/$Renderer 子门禁失败；保留报告并继续完成 A/B 汇总。"
    }
    return Read-Json $reportPath
}

function Add-CommonGates {
    param(
        [Collections.Generic.List[object]]$Gates,
        [object]$Cpu,
        [object]$Gl,
        [string]$Prefix
    )
    Add-Gate $Gates "$Prefix 实际使用 OpenGL" `
        ($Gl.ActiveRenderer -eq "opengl" -and $Gl.Renderer.vendor -and
         $Gl.Renderer.renderer -and $Gl.Renderer.version -and $Gl.TextureEvidence) `
        $Cpu.ActiveRenderer $Gl.ActiveRenderer "active=opengl、GL 身份非空、纹理字节>0"
    $validFrameAge = $Cpu.LatestFrameAgeP95Ms -ge 0 -and
        $Gl.LatestFrameAgeP95Ms -ge 0
    Add-Gate $Gates "$Prefix 最新帧年龄" `
        ($validFrameAge -and
         $Gl.LatestFrameAgeP95Ms -le $Cpu.LatestFrameAgeP95Ms * 1.10) `
        $Cpu.LatestFrameAgeP95Ms $Gl.LatestFrameAgeP95Ms "OpenGL P95 不恶化超过 10%"
    $validInternalLatency = $Cpu.InternalLatencyP95Ms -ge 0 -and
        $Gl.InternalLatencyP95Ms -ge 0
    Add-Gate $Gates "$Prefix 内部延迟" `
        ($validInternalLatency -and
         $Gl.InternalLatencyP95Ms -le $Cpu.InternalLatencyP95Ms * 1.10) `
        $Cpu.InternalLatencyP95Ms $Gl.InternalLatencyP95Ms "OpenGL P95 不恶化超过 10%"
    Add-Gate $Gates "$Prefix 工作集斜率" `
        ($Gl.WorkingSetSlopeMiBPerMinute -le 2.0) `
        $Cpu.WorkingSetSlopeMiBPerMinute $Gl.WorkingSetSlopeMiBPerMinute "≤2 MiB/min"
    $growth = Get-WindowGrowth @($Gl.Samples) "WorkingSetMiB"
    Add-Gate $Gates "$Prefix 工作集窗口增长" ($growth -le 64.0) `
        (Get-WindowGrowth @($Cpu.Samples) "WorkingSetMiB") $growth "末/首窗口均值差≤64 MiB"
    $glSamples = @($Gl.Samples)
    $lastTextureBytes = if ($glSamples.Count -gt 0) {
        $glSamples[-1].TextureBytes
    } else { 0 }
    Add-Gate $Gates "$Prefix 纹理稳定" `
        ($glSamples.Count -gt 0 -and (Test-TextureStable $glSamples)) `
        0 $lastTextureBytes "预热后纹理字节恒定且>0"
}

function New-ComparisonReport {
    param([object]$VideoCpu, [object]$VideoGl,
          [object]$LatencyCpu, [object]$LatencyGl, [bool]$QualityPassed)
    $gates = New-Object Collections.Generic.List[object]
    if ($null -ne $VideoCpu -and $null -ne $VideoGl) {
        $cpuImprovement = Get-ImprovementPercent `
            $VideoCpu.AverageApplicationCpuPercent `
            $VideoGl.AverageApplicationCpuPercent
        Add-Gate $gates "16 路应用 CPU" ($cpuImprovement -ge 15.0) `
            $VideoCpu.AverageApplicationCpuPercent `
            $VideoGl.AverageApplicationCpuPercent "OpenGL 至少降低 15%（实际 $([Math]::Round($cpuImprovement,2))%）"
        Add-Gate $gates "16 路显示帧率" ($VideoGl.AverageDisplayFps -ge 14.0) `
            $VideoCpu.AverageDisplayFps $VideoGl.AverageDisplayFps "OpenGL 平均≥14 FPS"
        Add-Gate $gates "16 路 UI 调度" ($VideoGl.MaximumUiTimerGapMs -lt 500) `
            $VideoCpu.MaximumUiTimerGapMs $VideoGl.MaximumUiTimerGapMs "<500 ms"
        Add-Gate $gates "16 路压缩包队列" ($VideoGl.MaximumQueuePackets -le 45) `
            $VideoCpu.MaximumQueuePackets $VideoGl.MaximumQueuePackets "≤45"
        Add-CommonGates $gates $VideoCpu $VideoGl "16 路"
    }
    if ($null -ne $LatencyCpu -and $null -ne $LatencyGl) {
        $latencySamplesValid =
            @($LatencyCpu.Streams | Where-Object Samples -le 0).Count -eq 0 -and
            @($LatencyGl.Streams | Where-Object Samples -le 0).Count -eq 0
        $cpuWorstP95 = [double](($LatencyCpu.Streams | Measure-Object P95Ms -Maximum).Maximum)
        $glWorstP95 = [double](($LatencyGl.Streams | Measure-Object P95Ms -Maximum).Maximum)
        $glWorstMax = [double](($LatencyGl.Streams | Measure-Object MaximumMs -Maximum).Maximum)
        Add-Gate $gates "源到显示样本完整" $latencySamplesValid `
            "16/16" "16/16" "CPU/OpenGL 每路都必须有源到显示样本"
        Add-Gate $gates "源到显示 P95 相对 CPU" `
            ($latencySamplesValid -and $glWorstP95 -le $cpuWorstP95 * 1.10) `
            $cpuWorstP95 $glWorstP95 "OpenGL 不恶化超过 10%"
        Add-Gate $gates "源到显示绝对延迟" `
            ($latencySamplesValid -and $glWorstP95 -le 750 -and $glWorstMax -le 1500) `
            ([pscustomobject]@{P95=$cpuWorstP95}) `
            ([pscustomobject]@{P95=$glWorstP95;Maximum=$glWorstMax}) "P95≤750 ms，最大≤1500 ms"
        Add-Gate $gates "延迟场景 UI 调度" ($LatencyGl.MaximumUiGapMs -lt 500) `
            $LatencyCpu.MaximumUiGapMs $LatencyGl.MaximumUiGapMs "<500 ms"
        Add-CommonGates $gates $LatencyCpu $LatencyGl "延迟场景"
    }
    if (Test-IncludesSuite "Quality") {
        Add-Gate $gates "YUV framebuffer 质量" $QualityPassed "CPU reference" `
            "OpenGL framebuffer" "每例 PSNR≥35 dB、MAE≤3、P99≤8"
    }
    $passed = @($gates | Where-Object { -not $_.Passed }).Count -eq 0
    return [ordered]@{
        SchemaVersion = 1
        CompletedAtUtc = [DateTime]::UtcNow.ToString("O")
        Complete = $true
        Passed = $passed
        Configuration = [ordered]@{
            Order = @("cpu", "opengl")
            VideoDurationSeconds = $VideoDurationSeconds
            LiveLatencyDurationSeconds = $LiveLatencyDurationSeconds
            WarmupSeconds = $WarmupSeconds
            CooldownSeconds = $CooldownSeconds
            StreamPrefix = $StreamPrefix
            LogicalProcessors = [Environment]::ProcessorCount
        }
        Video = if ($null -eq $VideoCpu) { $null } else {
            [ordered]@{ CPU = $VideoCpu; OpenGL = $VideoGl }
        }
        LiveLatency = if ($null -eq $LatencyCpu) { $null } else {
            [ordered]@{ CPU = $LatencyCpu; OpenGL = $LatencyGl }
        }
        QualityPassed = $QualityPassed
        Gates = $gates
    }
}

function Write-ComparisonReport {
    param([object]$Report)
    $Report | ConvertTo-Json -Depth 12 |
        Set-Content -LiteralPath $ReportJsonPath -Encoding UTF8
    $rows = @($Report.Gates | ForEach-Object {
        "| $($_.Name) | $($_.CPU) | $($_.OpenGL) | $($_.Requirement -replace '\|','/') | $($_.Passed) |"
    })
    @(
        "# CPU/OpenGL 产品渲染对照报告", "",
        "- 完成时间（UTC）：$($Report.CompletedAtUtc)",
        "- 结果：$(if ($Report.Passed) {'通过'} else {'未通过'})",
        "- 顺序：CPU → 冷却 $CooldownSeconds 秒 → OpenGL",
        "- 注意：画质门禁证明等价与无可见退化，不表示 OpenGL 天然更清晰。", "",
        "| 门禁 | CPU | OpenGL | 要求 | 通过 |",
        "|---|---:|---:|---|---|"
    ) + $rows | Set-Content -LiteralPath $ReportMarkdownPath -Encoding UTF8
    $serialized = Get-Content -LiteralPath $ReportJsonPath -Raw -Encoding UTF8
    if (Test-SensitiveText $serialized) {
        throw "对照汇总包含绝对路径、RTMP URL 或疑似凭据，拒绝归档。"
    }
}

function Invoke-Stop {
    foreach ($scenario in @("video", "live-latency")) {
        foreach ($renderer in @("cpu", "opengl")) {
            $root = Join-Path $OutputRoot ("{0}\{1}" -f $scenario, $renderer)
            $script = if ($scenario -eq "video") { $VideoScript } else { $LatencyScript }
            try {
                Invoke-ChildScript $script @("-Action", "Stop", "-OutputRoot", $root) | Out-Null
            } catch {
                Write-Warning $_.Exception.Message
            }
        }
    }
}

function Invoke-Run {
    New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
    $videoCpu = $null
    $videoGl = $null
    $latencyCpu = $null
    $latencyGl = $null
    $qualityPassed = $false
    try {
        Invoke-Check
        if (Test-IncludesSuite "Quality") {
            Set-State "running" "quality" "正在运行生产 YUV framebuffer 质量门禁。"
            try { $qualityPassed = Invoke-Quality } catch {
                Write-Warning $_.Exception.Message
                $qualityPassed = $false
            }
        }
        if (Test-IncludesSuite "Video") {
            $assetOutput = Join-Path $OutputRoot "shared-assets"
            $sharedAssetDirectory = Join-Path $assetOutput "assets"
            Set-State "running" "video-prepare" `
                "正在准备 CPU/OpenGL 共用的 16 路预编码素材。"
            Invoke-ChildScript $VideoScript @(
                "-Action", "Prepare",
                "-OutputRoot", $assetOutput,
                "-AssetDirectory", $sharedAssetDirectory
            ) | Out-Null
            $videoCpu = Invoke-ScenarioRun "video" "cpu" $sharedAssetDirectory
            if ($CooldownSeconds -gt 0) { Start-Sleep -Seconds $CooldownSeconds }
            $videoGl = Invoke-ScenarioRun "video" "opengl" $sharedAssetDirectory
        }
        if (Test-IncludesSuite "LiveLatency") {
            $latencyCpu = Invoke-ScenarioRun "live-latency" "cpu"
            if ($CooldownSeconds -gt 0) { Start-Sleep -Seconds $CooldownSeconds }
            $latencyGl = Invoke-ScenarioRun "live-latency" "opengl"
        }
        $report = New-ComparisonReport $videoCpu $videoGl $latencyCpu $latencyGl $qualityPassed
        Write-ComparisonReport $report
        Set-State "completed" "summary" $(if ($report.Passed) {
            "全部请求门禁通过。"
        } else {
            "一个或多个门禁未通过，请查看 comparison.md。"
        })
        Write-Host "总报告：$ReportMarkdownPath"
        if (-not $report.Passed) { throw "CPU/OpenGL 对照门禁未全部通过。" }
    } catch {
        if (-not (Test-Path -LiteralPath $ReportJsonPath)) {
            Set-State "incomplete" "failed" $_.Exception.Message
        }
        throw
    } finally {
        Invoke-Stop
    }
}

function Invoke-Status {
    if ($RecalculateSummary) {
        $videoCpu = $null
        $videoGl = $null
        $latencyCpu = $null
        $latencyGl = $null
        if (Test-IncludesSuite "Video") {
            $videoCpu = Read-Json (Join-Path $OutputRoot "video\cpu\automated-report.json")
            $videoGl = Read-Json (Join-Path $OutputRoot "video\opengl\automated-report.json")
        }
        if (Test-IncludesSuite "LiveLatency") {
            $latencyCpu = Read-Json (Join-Path $OutputRoot "live-latency\cpu\latency-report.json")
            $latencyGl = Read-Json (Join-Path $OutputRoot "live-latency\opengl\latency-report.json")
        }
        $qualityPassed = $false
        if (Test-IncludesSuite "Quality") {
            $quality = Read-Json (Join-Path $OutputRoot "quality\windows-opengl-validation.json")
            $qualityPassed = [bool]$quality.Passed
        }
        $report = New-ComparisonReport $videoCpu $videoGl $latencyCpu $latencyGl $qualityPassed
        Write-ComparisonReport $report
        Set-State "completed" "summary" $(if ($report.Passed) {
            "基于已保存原始报告重新计算后，全部请求门禁通过。"
        } else {
            "基于已保存原始报告重新计算后，仍有门禁未通过。"
        })
    }
    if (Test-Path -LiteralPath $StatePath) {
        Get-Content -LiteralPath $StatePath -Raw -Encoding UTF8
    } else {
        Write-Host "尚无对照测试状态。"
    }
    if (Test-Path -LiteralPath $ReportMarkdownPath) {
        Write-Host "报告：$ReportMarkdownPath"
    }
}

function Invoke-SelfTest {
    if ([Math]::Abs((Get-Percentile @(1, 2, 3, 4, 100) 0.95) - 100) -gt 0.001) {
        throw "Percentile self-test failed."
    }
    if ([Math]::Abs((Get-ImprovementPercent 40 30) - 25) -gt 0.001) {
        throw "Improvement self-test failed."
    }
    if ([Math]::Abs((Get-LinearSlopePerMinute @(100, 101, 102, 103)) - 60) -gt 0.001) {
        throw "Slope self-test failed."
    }
    if (-not (Test-SensitiveText "rtmp://127.0.0.1/live/test") -or
        -not (Test-SensitiveText "E:\private\report.json") -or
        (Test-SensitiveText '{"renderer":"opengl","cpu":12.5}')) {
        throw "Sanitization self-test failed."
    }
    $normalizedCpu = 100.0 * 16.0 / (16 * 10.0)
    if ([Math]::Abs($normalizedCpu - 10.0) -gt 0.001) {
        throw "CPU normalization self-test failed."
    }
    $relativeOutput = [IO.Path]::GetFullPath(".\out\renderer-self-test")
    if (-not [IO.Path]::IsPathRooted($relativeOutput)) {
        throw "Output path normalization self-test failed."
    }
    $textureSamples = @(1..180 | ForEach-Object {
        [pscustomobject]@{ TextureBytes = $(if ($_ -eq 75) { 90 } else { 100 }) }
    })
    if (-not (Test-TextureStable $textureSamples)) {
        throw "Texture failure/recovery self-test failed."
    }
    $textureSamples[-1].TextureBytes = 110
    if (Test-TextureStable $textureSamples) {
        throw "Texture growth self-test failed."
    }
    Write-Host "Renderer comparison self-test passed." -ForegroundColor Green
}

switch ($Action) {
    "Check" { Invoke-Check }
    "Run" { Invoke-Run }
    "Status" { Invoke-Status }
    "Stop" { Invoke-Stop }
    "SelfTest" { Invoke-SelfTest }
}
