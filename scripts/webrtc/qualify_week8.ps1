[CmdletBinding()]
param(
    [ValidateSet('Check', 'SelfTest', 'Run')]
    [string]$Action = 'Check',
    [string]$QtRoot = $env:QTDIR,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$VsDevCmd,
    [string]$BuildRoot,
    [string]$ResultPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'QualificationCommon.psm1') -Force

$script:SourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $script:SourceRoot 'out\build-windows-x64\week8'
}
$script:BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$script:RuntimeRoot = Join-Path $script:SourceRoot 'out\webrtc-week8'
if ([string]::IsNullOrWhiteSpace($ResultPath)) {
    $ResultPath = Join-Path $script:RuntimeRoot 'qualification-result.json'
}
$script:ResultPath = [IO.Path]::GetFullPath($ResultPath)
$script:AssetPath = Join-Path $script:RuntimeRoot 'fixtures\sample.mp4'
$script:AudioOnlyPath = Join-Path $script:RuntimeRoot 'fixtures\audio-only.mp4'
$script:NonH264Path = Join-Path $script:RuntimeRoot 'fixtures\non-h264.mp4'
$script:BFramesPath = Join-Path $script:RuntimeRoot 'fixtures\h264-bframes.mp4'

function Assert-Prerequisites {
    [void](Assert-QualificationConcretePath -Value $QtRoot -Name 'QtRoot')
    [void](Assert-QualificationConcretePath -Value $VcpkgRoot -Name 'VcpkgRoot')
    if (-not [string]::IsNullOrWhiteSpace($VsDevCmd)) {
        [void](Assert-QualificationConcretePath -Value $VsDevCmd -Name 'VsDevCmd')
    }
    foreach ($path in @(
            (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'),
            (Join-Path $QtRoot 'plugins\platforms'),
            (Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'))) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Week 8 prerequisite is missing: $path"
        }
    }
    $tools = Resolve-QualificationTools -VsDevCmd $VsDevCmd
    Write-Host 'Week 8 prerequisites passed.'
    return $tools
}

function Set-TestEnvironment {
    param([ValidateSet('Debug', 'Release')][string]$Configuration)
    $env:Path = Get-QualificationRuntimePath -QtRoot $QtRoot `
        -VcpkgRoot $VcpkgRoot -Configuration $Configuration
    # Most existing UI/OpenGL tests require the real Windows backend.  The
    # Week 8 product test overrides this to offscreen in its CTest property.
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $QtRoot 'plugins\platforms'
    $env:RTMP_MONITOR_WEEK4_SAMPLE = $script:AssetPath
    $env:RTMP_MONITOR_WEEK4_AUDIO_ONLY = $script:AudioOnlyPath
    $env:RTMP_MONITOR_WEEK4_NON_H264 = $script:NonH264Path
    $env:RTMP_MONITOR_WEEK4_B_FRAMES = $script:BFramesPath
}

function Invoke-ConfigureBuildTest {
    param(
        [Parameter(Mandatory = $true)]$Tools,
        [Parameter(Mandatory = $true)][string]$Name,
        [ValidateSet('Debug', 'Release')][string]$Configuration,
        [bool]$WebRtc
    )
    $directory = Join-Path $script:BuildRoot $Name
    $enabled = if ($WebRtc) { 'ON' } else { 'OFF' }
    $toolchain = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
    Invoke-QualificationNative -FilePath $Tools.CMake -Arguments @(
        '-S', $script:SourceRoot, '-B', $directory, '--fresh',
        '-G', 'Visual Studio 18 2026', '-A', 'x64',
        "-DCMAKE_PREFIX_PATH=$QtRoot",
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
        '-DVCPKG_TARGET_TRIPLET=x64-windows',
        '-DBUILD_TESTING=ON',
        "-DRTMP_MONITOR_ENABLE_WEBRTC=$enabled"
    ) | Out-Host
    Invoke-QualificationNative -FilePath $Tools.CMake -Arguments @(
        '--build', $directory, '--config', $Configuration, '--parallel', '4'
    ) | Out-Host

    $saved = @{}
    foreach ($name in @(
            'Path', 'QT_QPA_PLATFORM', 'QT_PLUGIN_PATH',
            'QT_QPA_PLATFORM_PLUGIN_PATH', 'RTMP_MONITOR_WEEK4_SAMPLE',
            'RTMP_MONITOR_WEEK4_AUDIO_ONLY',
            'RTMP_MONITOR_WEEK4_NON_H264',
            'RTMP_MONITOR_WEEK4_B_FRAMES')) {
        $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }
    try {
        Set-TestEnvironment -Configuration $Configuration
        Invoke-QualificationNative -FilePath $Tools.CTest -Arguments @(
            '--test-dir', $directory, '-C', $Configuration,
            '--output-on-failure'
        ) | Out-Host
        $listing = & $Tools.CTest --test-dir $directory -C $Configuration `
            -N 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) { throw "CTest listing failed: $Name" }
        $countMatch = [regex]::Match($listing, 'Total Tests:\s+(\d+)')
        if (-not $countMatch.Success) { throw "CTest count missing: $Name" }
        $required = if ($WebRtc) {
            @(
                'rtmp_monitor_webrtc_endpoint_test',
                'rtmp_monitor_webrtc_viewer_pipeline_test',
                'rtmp_monitor_webrtc_product_test',
                'rtmp_monitor_layer_dependency_test'
            )
        } else {
            @('rtmp_monitor_webrtc_disabled_test',
              'rtmp_monitor_layer_dependency_test')
        }
        foreach ($test in $required) {
            if ($listing -notmatch [regex]::Escape($test)) {
                throw "Required test missing from ${Name}: $test"
            }
        }
        if (-not $WebRtc -and
            $listing -match 'rtmp_monitor_webrtc_product_test') {
            throw 'WebRTC=OFF unexpectedly exposes the Week 8 product test.'
        }
        $config = Get-Content -LiteralPath `
            (Join-Path $directory 'generated\RtmpMonitorBuildConfig.h') -Raw
        $expectedMacro = if ($WebRtc) { '1' } else { '0' }
        if ($config -notmatch "RTMP_MONITOR_HAS_WEBRTC $expectedMacro") {
            throw "Unexpected product feature macro in $Name."
        }
        if ($WebRtc) {
            $runtime = Join-Path $directory "$Configuration\rtmp_monitor.exe"
            $datachannel = Join-Path $directory "$Configuration\datachannel.dll"
            foreach ($path in @($runtime, $datachannel)) {
                if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                    throw "Week 8 runtime artifact is missing: $path"
                }
            }
        }
        return [int]$countMatch.Groups[1].Value
    } finally {
        foreach ($name in $saved.Keys) {
            if ($null -eq $saved[$name]) {
                Remove-Item "Env:$name" -ErrorAction SilentlyContinue
            } else {
                Set-Item "Env:$name" $saved[$name]
            }
        }
    }
}

function Assert-Week8Sources {
    $controller = Get-Content -LiteralPath `
        (Join-Path $script:SourceRoot `
            'include\common\webrtc_product\WebRtcProductSessionController.h') `
        -Raw -Encoding UTF8
    $request = Get-Content -LiteralPath `
        (Join-Path $script:SourceRoot `
            'include\common\webrtc_product\WebRtcProductTypes.h') `
        -Raw -Encoding UTF8
    $runtime = Get-Content -LiteralPath `
        (Join-Path $script:SourceRoot `
            'include\common\webrtc_runtime\WebRtcReceiveSession.h') `
        -Raw -Encoding UTF8
    foreach ($required in @(
            'WebRtcProductSessionController', 'WebRtcProductDiagnostics',
            'WebRtcReceiveSession', 'NeedsRelay', 'presentedFrameAgeMs',
            'controlAuthorized', 'rtmpFallbackStarted')) {
        if (($controller + $request + $runtime) -notmatch `
            [regex]::Escape($required)) {
            throw "Week 8 source contract is missing: $required"
        }
    }
    if (($controller + $request + $runtime) -match `
        '(?i)autoConnect|SavedStream|deviceId|peerId|rtmpUrl') {
        throw 'Runtime-only Week 8 contracts contain a forbidden durable identity.'
    }
    $cmake = Get-Content -LiteralPath `
        (Join-Path $script:SourceRoot 'CMakeLists.txt') -Raw -Encoding UTF8
    foreach ($required in @(
            'rtmp_monitor_webrtc_runtime', 'rtmp_monitor_webrtc_product',
            'rtmp_monitor_webrtc_product_test', 'RTMP_MONITOR_HAS_WEBRTC')) {
        if ($cmake -notmatch [regex]::Escape($required)) {
            throw "Week 8 CMake contract is missing: $required"
        }
    }
}

function Assert-Week8Documents {
    $week = Join-Path $script:SourceRoot `
        'docs\versions\webrtc-v2\weeks\week08'
    $summary = Get-Content -LiteralPath (Join-Path $week 'summary.md') `
        -Raw -Encoding UTF8
    $guide = Get-Content -LiteralPath (Join-Path $week 'testing_guide.md') `
        -Raw -Encoding UTF8
    if ($summary.Length -lt 16000 -or
        ([regex]::Matches($summary, '[\u4e00-\u9fff]')).Count -lt 5000) {
        throw 'Week 8 summary is not long enough for the 20-minute guide.'
    }
    if ($guide.Length -lt 11000 -or
        ([regex]::Matches($guide, '[\u4e00-\u9fff]')).Count -lt 4000) {
        throw 'Week 8 testing guide is not detailed enough.'
    }
    foreach ($required in @(
            'WebRtcProductSessionController', 'WebRtcReceiveSession',
            'WebRtcProductPolicy', 'NeedsRelay', 'Direct', '1,000 ms',
            'RTMP', 'controlAuthorized')) {
        if (($summary + $guide) -notmatch [regex]::Escape($required)) {
            throw "Week 8 documentation is missing: $required"
        }
    }
    $svgs = @(Get-ChildItem -LiteralPath (Join-Path $week 'assets') `
        -Filter '*.svg' -File)
    if ($svgs.Count -lt 7) {
        throw 'Week 8 documentation requires at least seven SVG diagrams.'
    }
    foreach ($svg in $svgs) {
        [xml](Get-Content -LiteralPath $svg.FullName -Raw -Encoding UTF8) |
            Out-Null
    }
    foreach ($document in @($summary, $guide)) {
        foreach ($match in [regex]::Matches(
                $document, '!\[[^\]]*\]\(([^)]+\.svg)\)')) {
            $linked = Join-Path $week $match.Groups[1].Value
            if (-not (Test-Path -LiteralPath $linked -PathType Leaf)) {
                throw "Broken Week 8 SVG link: $linked"
            }
        }
    }
}

function Invoke-SelfTest {
    [void][scriptblock]::Create((Get-Content -LiteralPath $PSCommandPath `
        -Raw -Encoding UTF8))
    Assert-Week8Sources
    Assert-Week8Documents
    Write-Host 'Week 8 qualification self-test passed.'
}

function Invoke-Run {
    $tools = Assert-Prerequisites
    New-QualificationH264Fixtures -Tools $tools `
        -AssetPath $script:AssetPath -AudioOnlyPath $script:AudioOnlyPath `
        -NonH264Path $script:NonH264Path -BFramesPath $script:BFramesPath
    $counts = [ordered]@{}
    $counts.debugOff = Invoke-ConfigureBuildTest `
        -Tools $tools -Name 'debug-off' -Configuration Debug -WebRtc $false
    $counts.debugOn = Invoke-ConfigureBuildTest `
        -Tools $tools -Name 'debug-on' -Configuration Debug -WebRtc $true
    $counts.releaseOn = Invoke-ConfigureBuildTest `
        -Tools $tools -Name 'release-on' -Configuration Release -WebRtc $true
    Assert-Week8Sources
    Assert-Week8Documents
    New-Item -ItemType Directory -Force `
        -Path (Split-Path -Parent $script:ResultPath) | Out-Null
    [void](Assert-QualificationPathUnderRoot -Path $script:ResultPath `
        -Root $script:SourceRoot -Label 'repository output')
    [ordered]@{
        schemaVersion = 1
        week = 8
        sourceCommit = (& git -C $script:SourceRoot rev-parse HEAD).Trim()
        generatedAtUtc = [DateTime]::UtcNow.ToString('o')
        ctest = $counts
        productEntryOnOnly = $true
        realH264ProductPath = $true
        signalingRoles = @('receiver-answerer', 'receiver-offerer')
        publicNetworkClaimed = $false
    } | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $script:ResultPath -Encoding UTF8
    Write-Host "Week 8 qualification passed: $script:ResultPath"
}

switch ($Action) {
    'Check' { [void](Assert-Prerequisites) }
    'SelfTest' { Invoke-SelfTest }
    'Run' { Invoke-Run }
}
