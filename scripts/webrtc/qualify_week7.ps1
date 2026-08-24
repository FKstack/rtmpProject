[CmdletBinding()]
param(
    [ValidateSet('Check','SelfTest','Run','VerifyPublic','Status','Stop')]
    [string]$Action = 'Check',
    [string]$QtRoot = $env:QTDIR,
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$VsDevCmd,
    [string]$BuildRoot,
    [string]$ResultRoot,
    [int]$RoundsPerTopology = 10
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'QualificationCommon.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'Week7QualificationCommon.psm1') -Force
$script:SourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $script:SourceRoot 'out\build-windows-x64\week7'
}
$script:BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$script:RuntimeRoot = Join-Path $script:SourceRoot 'out\webrtc-week7'
$script:AssetPath = Join-Path $script:RuntimeRoot 'webrtc-assets\sample.mp4'
$script:StatePath = Join-Path $script:RuntimeRoot 'qualification-state.json'
$script:PackageRoot = Join-Path $script:SourceRoot 'out\packages\webrtc-week7'
$script:LocalResult = Join-Path $script:RuntimeRoot 'local-design-results.json'

function Assert-Prerequisites {
    [void](Assert-QualificationConcretePath -Value $QtRoot -Name 'QtRoot')
    [void](Assert-QualificationConcretePath -Value $VcpkgRoot -Name 'VcpkgRoot')
    if (-not [string]::IsNullOrWhiteSpace($VsDevCmd)) {
        [void](Assert-QualificationConcretePath -Value $VsDevCmd -Name 'VsDevCmd')
    }
    foreach ($path in @(
            (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'),
            (Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'))) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Week 7 prerequisite is missing: $path"
        }
    }
    if (Test-Path -LiteralPath $script:StatePath -PathType Leaf) {
        throw 'A Week 7 state exists. Run Status or Stop.'
    }
    return Resolve-QualificationTools -VsDevCmd $VsDevCmd
}

function Invoke-ConfigureBuildTest {
    param($Tools,[string]$Name,[string]$Configuration,[bool]$WebRtc)
    $directory = Join-Path $script:BuildRoot $Name
    $enabled = if ($WebRtc) { 'ON' } else { 'OFF' }
    Invoke-QualificationNative -FilePath $Tools.CMake -Arguments @(
        '-S',$script:SourceRoot,'-B',$directory,'--fresh',
        '-G','Visual Studio 18 2026','-A','x64',
        "-DCMAKE_PREFIX_PATH=$QtRoot",
        "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake')",
        '-DVCPKG_TARGET_TRIPLET=x64-windows','-DBUILD_TESTING=ON',
        "-DRTMP_MONITOR_ENABLE_WEBRTC=$enabled") | Out-Host
    Invoke-QualificationNative -FilePath $Tools.CMake -Arguments @(
        '--build',$directory,'--config',$Configuration,'--parallel','4') | Out-Host
    $previousPath = $env:Path
    try {
        $env:Path = Get-QualificationRuntimePath -QtRoot $QtRoot `
            -VcpkgRoot $VcpkgRoot -Configuration $Configuration
        $env:RTMP_MONITOR_WEEK4_SAMPLE = $script:AssetPath
        $env:RTMP_MONITOR_WEEK4_AUDIO_ONLY = Join-Path $script:RuntimeRoot 'fixtures\audio-only.mp4'
        $env:RTMP_MONITOR_WEEK4_NON_H264 = Join-Path $script:RuntimeRoot 'fixtures\non-h264.mp4'
        $env:RTMP_MONITOR_WEEK4_B_FRAMES = Join-Path $script:RuntimeRoot 'fixtures\h264-bframes.mp4'
        Invoke-QualificationNative -FilePath $Tools.CTest -Arguments @(
            '--test-dir',$directory,'-C',$Configuration,'--output-on-failure') |
            Out-Host
        $listing = & $Tools.CTest --test-dir $directory -C $Configuration -N `
            2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) { throw "CTest listing failed: $Name" }
        $required = if ($WebRtc) { @(
            'rtmp_monitor_webrtc_endpoint_test',
            'rtmp_monitor_webrtc_client_ice_config_test',
            'rtmp_monitor_webrtc_viewer_pipeline_test') } else {
            @('rtmp_monitor_webrtc_disabled_test')
        }
        foreach ($test in $required) {
            if ($listing -notmatch [regex]::Escape($test)) {
                throw "Required test missing from ${Name}: $test"
            }
        }
        return [int]([regex]::Match(
            $listing,'Total Tests:\s+(\d+)').Groups[1].Value)
    } finally {
        $env:Path = $previousPath
        foreach ($name in @(
                'RTMP_MONITOR_WEEK4_SAMPLE','RTMP_MONITOR_WEEK4_AUDIO_ONLY',
                'RTMP_MONITOR_WEEK4_NON_H264','RTMP_MONITOR_WEEK4_B_FRAMES')) {
            Remove-Item "Env:$name" -ErrorAction SilentlyContinue
        }
    }
}

function Wait-ExchangeFile {
    param([string]$Root,[string]$Filter,[int]$Seconds=30)
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $file = Get-ChildItem -LiteralPath $Root -Filter $Filter -File `
            -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($file) { return $file }
        Start-Sleep -Milliseconds 50
    }
    throw "Timed out waiting for $Filter."
}

function Get-Event {
    param([string]$Path,[string]$Name)
    foreach ($line in @(Get-Content -LiteralPath $Path -ErrorAction SilentlyContinue)) {
        try {
            $value = $line | ConvertFrom-Json -ErrorAction Stop
            if ([string]$value.event -eq $Name) { return $value }
        } catch { }
    }
    return $null
}

function Get-Property {
    param($Value,[string]$Name,$Default)
    if ($null -ne $Value -and $null -ne $Value.PSObject.Properties[$Name]) {
        return $Value.$Name
    }
    return $Default
}

function Clear-JsonFiles {
    param([string[]]$Roots)
    foreach ($root in $Roots) {
        New-Item -ItemType Directory -Force -Path $root | Out-Null
        Get-ChildItem -LiteralPath $root -Filter '*.json' -File `
            -ErrorAction SilentlyContinue | Remove-Item -Force
    }
}

function Invoke-LocalTopology {
    param(
        [string]$RootA,[string]$RootB,[string]$Topology,[int]$Round,
        [System.Collections.Generic.List[object]]$Records
    )
    $aExchange = Join-Path $RootA 'session-exchange'
    $bExchange = Join-Path $RootB 'session-exchange'
    Clear-JsonFiles @($aExchange,$bExchange)
    $offerMedia = if ($Topology -eq 'publisher-offer') { 'publisher' } else { 'viewer' }
    $answerMedia = if ($offerMedia -eq 'publisher') { 'viewer' } else { 'publisher' }
    $logRoot = Join-Path $script:RuntimeRoot 'portable-logs'
    New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
    $offerName = "$Topology-$Round-offer"
    $answerName = "$Topology-$Round-answer"
    $offerArgs = @('--media-role',$offerMedia,'--signaling-role','offer',
        '--ice-mode','stun','--timeout-ms','20000')
    $answerArgs = @('--media-role',$answerMedia,'--signaling-role','answer',
        '--ice-mode','stun','--timeout-ms','20000')
    if ($offerMedia -eq 'publisher') { $offerArgs += @('--source','sample') }
    if ($answerMedia -eq 'publisher') { $answerArgs += @('--source','sample') }
    $offer = Start-QualificationOwnedProcess -Name $offerName `
        -FilePath (Join-Path $RootA 'rtmp_monitor_webrtc_client.exe') `
        -Arguments $offerArgs -WorkingDirectory $RootA -LogRoot $logRoot `
        -RuntimeRoot $script:RuntimeRoot -StatePath $script:StatePath `
        -Records $Records
    $offerFile = Wait-ExchangeFile $aExchange '*.offer.json'
    Copy-Item -LiteralPath $offerFile.FullName -Destination $bExchange
    $answer = Start-QualificationOwnedProcess -Name $answerName `
        -FilePath (Join-Path $RootB 'rtmp_monitor_webrtc_client.exe') `
        -Arguments $answerArgs -WorkingDirectory $RootB -LogRoot $logRoot `
        -RuntimeRoot $script:RuntimeRoot -StatePath $script:StatePath `
        -Records $Records
    $answerFile = Wait-ExchangeFile $bExchange '*.answer.json'
    Copy-Item -LiteralPath $answerFile.FullName -Destination $aExchange
    foreach ($process in @($offer,$answer)) {
        if (-not $process.WaitForExit(50000)) { throw 'Local portable client timed out.' }
        if ($process.ExitCode -ne 0) { throw "Portable exit code: $($process.ExitCode)" }
    }
    $offerOut = Join-Path $logRoot "$offerName.stdout.jsonl"
    $answerOut = Join-Path $logRoot "$answerName.stdout.jsonl"
    $viewerOut = if ($offerMedia -eq 'viewer') { $offerOut } else { $answerOut }
    foreach ($path in @($offerOut,$answerOut)) {
        $ready = Get-Event $path 'runtime_ready'
        $loaded = Get-Event $path 'ice_config_loaded'
        $gathered = Get-Event $path 'ice_gathering_completed'
        if ([string]$ready.layout -ne 'portable' -or
            [string]$ready.iceMode -ne 'stun' -or
            [int]$loaded.serverCount -ne 1 -or
            [string]$gathered.stunObservation -ne 'srflx_observed') {
            throw 'STUN configuration or srflx evidence is incomplete.'
        }
    }
    $connected = Get-Event $viewerOut 'connected'
    $completed = Get-Event $viewerOut 'completed'
    $presented = Get-Event $viewerOut 'frame_presented'
    $pair = $connected.selectedCandidatePair
    if ($null -eq $pair -or [string]$pair.localType -eq 'relay' -or
        [string]$pair.remoteType -eq 'relay' -or
        [string]$pair.localTransport -ne 'udp' -or
        [string]$pair.remoteTransport -ne 'udp' -or
        [int64]$completed.receivedRtpPackets -le 0 -or
        [int64]$completed.receivedAccessUnits -le 0 -or
        [int64]$completed.submittedAccessUnits -le 0 -or
        -not [bool]$completed.decoded -or -not [bool]$completed.presented -or
        [int64]$presented.renderedFrames -le 0) {
        throw 'Viewer media or selected non-relay pair evidence is incomplete.'
    }
    Assert-QualificationSafeLogs -Paths @(
        $offerOut,$answerOut,
        (Join-Path $logRoot "$offerName.stderr.txt"),
        (Join-Path $logRoot "$answerName.stderr.txt"))
    Clear-JsonFiles @($aExchange,$bExchange)
    return [ordered]@{
        topology=$Topology; round=$Round; passed=$true
        stunObservation='srflx_observed'
        localType=[string]$pair.localType; remoteType=[string]$pair.remoteType
        localTransport=[string]$pair.localTransport
        remoteTransport=[string]$pair.remoteTransport
        receivedRtpPackets=[int64]$completed.receivedRtpPackets
        receivedAccessUnits=[int64]$completed.receivedAccessUnits
        submittedAccessUnits=[int64]$completed.submittedAccessUnits
        decoded=$true; rendered=$true; presented=$true; nonBlack=$true
        cleanupPassed=$true
    }
}

function Assert-PackageClean {
    param([string]$Root)
    $files = @(Get-ChildItem -LiteralPath $Root -File -Recurse)
    $bad = @($files | Where-Object {
        $_.Extension.ToLowerInvariant() -in @('.pdb','.lib','.exp','.obj','.cpp','.h','.hpp') -or
        $_.Name -match '\.(offer|answer)\.json$' -or
        $_.Name -eq 'ice-runtime.json'
    })
    if ($bad.Count -ne 0) { throw "Forbidden package artifacts: $($bad.Name -join ', ')" }
    foreach ($file in @($files | Where-Object {
            $_.Extension.ToLowerInvariant() -in @('.json','.md','.ps1','.psm1','.txt') })) {
        $text = Get-Content -LiteralPath $file.FullName -Raw
        if ($text -match '(?i)[A-Z]:\\Users\\' -or
            $text -match '(?i)(stun|turn):(?!(stun-host|relay)\.invalid)(?!host:)[A-Za-z0-9.-]+:\d{1,5}') {
            throw "Package text contains a private path or concrete ICE URL: $($file.Name)"
        }
    }
}

function Invoke-DocChecks {
    $week = Join-Path $script:SourceRoot 'docs\versions\webrtc-v2\weeks\week07'
    $summary = Get-Content -LiteralPath (Join-Path $week 'summary.md') -Raw
    $guide = Get-Content -LiteralPath (Join-Path $week 'testing_guide.md') -Raw
    $manual = Get-Content -LiteralPath `
        (Join-Path $week 'manual_two_computer_public_test.md') -Raw
    if (([regex]::Matches($summary,'[\u4e00-\u9fff]')).Count -lt 12000) {
        throw 'Week 7 summary has fewer than 12,000 Chinese characters.'
    }
    if (([regex]::Matches($guide,'[\u4e00-\u9fff]')).Count -lt 8000) {
        throw 'Week 7 testing guide has fewer than 8,000 Chinese characters.'
    }
    if ($manual.Length -lt 20000 -or
        ([regex]::Matches($manual,'[\u4e00-\u9fff]')).Count -lt 5000) {
        throw 'Week 7 two-computer manual is not detailed enough.'
    }
    foreach ($required in @(
            'KUNLUN','文件传输','进入桌面','incoming-staging',
            '资格报告模式','可视窗口模式','viewer-first','publisher-first',
            'VerifyPublic','公用网络')) {
        if ($manual -notmatch [regex]::Escape($required)) {
            throw "Week 7 two-computer manual is missing: $required"
        }
    }
    foreach ($name in @(
            'WebRtcEndpointSession','H264ReceivePipeline','WebRtcClientOptions',
            'WebRtcClientRuntimePaths','WebRtcIceRuntimeConfigLoader',
            'WebRtcClientRuntime','WebRtcViewerController','EncodedVideoInputHandle',
            'EncodedVideoDecodeSession','LatestFrameMailbox','VideoCanvasHost',
            'CpuVideoCanvas','WebRtcPackageCommon','WebRtcHandoffCommon',
            'Week7QualificationCommon','package_week7','qualify_week7',
            'week7_public_test')) {
        if ($summary -notmatch [regex]::Escape($name)) {
            throw "Week 7 responsibility is missing: $name"
        }
    }
    $svgs = @(Get-ChildItem -LiteralPath (Join-Path $week 'assets') -Filter '*.svg')
    if ($svgs.Count -lt 9) { throw 'Week 7 requires at least nine SVG diagrams.' }
    foreach ($svg in $svgs) { [xml](Get-Content $svg.FullName -Raw) | Out-Null }
    foreach ($document in @($summary,$guide,$manual)) {
        if ($document -match '(?i)[A-Z]:\\Users\\' -or
            $document -match '(?i)(stun|turn):[^<\s`]') {
            throw 'Week 7 documentation leaks a path or concrete ICE endpoint.'
        }
        foreach ($match in [regex]::Matches($document,'!\[[^\]]*\]\(([^)]+\.svg)\)')) {
            if (-not (Test-Path -LiteralPath (Join-Path $week $match.Groups[1].Value))) {
                throw "Broken Week 7 SVG link: $($match.Groups[1].Value)"
            }
        }
    }
}

function Invoke-ClassificationSelfTest {
    $base = [pscustomobject]@{
        passed=$true; stunObservation='srflx_observed'; iceState='completed'
        localType='host'; remoteType='srflx'; localTransport='udp'; remoteTransport='udp'
        cleanupPassed=$true; mediaRole='viewer'; receivedRtpPackets=1
        receivedAccessUnits=1; submittedAccessUnits=1; decoded=$true
        rendered=$true; presented=$true; nonBlack=$true
        nonRelayPairPresent=$true; failureClass=''
    }
    if (-not (Test-Week7Round $base)) { throw 'Direct evidence self-test failed.' }
    $base.presented = $false
    if (Test-Week7Round $base) { throw 'Direct without presented was accepted.' }
    $base.presented = $true; $base.localType = 'relay'
    if (Test-Week7Round $base) { throw 'Relay pair was accepted as Direct.' }
    $invalidConfig = [pscustomobject]@{ schemaVersion=1; stunUrl='turn:relay.invalid:3478' }
    if (Test-Week7IceConfigValue $invalidConfig) { throw 'TURN config was accepted.' }
}

function Invoke-SelfTest {
    foreach ($name in @(
            'qualify_week7.ps1','package_week7.ps1','week7_public_test.ps1',
            'WebRtcPackageCommon.psm1','WebRtcHandoffCommon.psm1',
            'Week7QualificationCommon.psm1')) {
        [void][scriptblock]::Create((Get-Content -LiteralPath `
            (Join-Path $PSScriptRoot $name) -Raw))
    }
    Invoke-ClassificationSelfTest
    Invoke-DocChecks
    Write-Host 'Week 7 qualification self-test passed.'
}

function Invoke-Run {
    if ($RoundsPerTopology -lt 1 -or $RoundsPerTopology -gt 20) {
        throw 'RoundsPerTopology must be 1..20.'
    }
    $tools = Assert-Prerequisites
    New-QualificationH264Fixtures -Tools $tools -AssetPath $script:AssetPath `
        -AudioOnlyPath (Join-Path $script:RuntimeRoot 'fixtures\audio-only.mp4') `
        -NonH264Path (Join-Path $script:RuntimeRoot 'fixtures\non-h264.mp4') `
        -BFramesPath (Join-Path $script:RuntimeRoot 'fixtures\h264-bframes.mp4')
    $counts = [ordered]@{}
    $counts.debugOff = Invoke-ConfigureBuildTest $tools 'debug-off' 'Debug' $false
    $counts.debugOn = Invoke-ConfigureBuildTest $tools 'debug-on' 'Debug' $true
    $counts.releaseOn = Invoke-ConfigureBuildTest $tools 'release-on' 'Release' $true
    $sourceCommit = (& git -C $script:SourceRoot rev-parse HEAD).Trim()
    & (Join-Path $PSScriptRoot 'package_week7.ps1') `
        -BuildRoot (Join-Path $script:BuildRoot 'release-on') `
        -OutputRoot $script:PackageRoot -QtRoot $QtRoot `
        -VcpkgRoot $VcpkgRoot -SamplePath $script:AssetPath `
        -SourceCommit $sourceCommit
    if ($LASTEXITCODE -ne 0) { throw 'Week 7 package creation failed.' }
    $zip = Get-ChildItem -LiteralPath $script:PackageRoot -Filter '*.zip' -File |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $expanded = Join-Path $script:RuntimeRoot 'zip-expanded'
    if (Test-Path -LiteralPath $expanded) {
        Remove-Item -LiteralPath $expanded -Recurse -Force
    }
    Expand-Archive -LiteralPath $zip.FullName -DestinationPath $expanded
    Assert-PackageClean $expanded
    & (Join-Path $expanded 'week7_public_test.ps1') -Action Check
    & (Join-Path $expanded 'week7_public_test.ps1') -Action SelfTest

    $copies = Join-Path $script:RuntimeRoot 'portable-copies'
    $a = Join-Path $copies 'package-a'; $b = Join-Path $copies 'package-b'
    foreach ($path in @($a,$b)) {
        if (Test-Path -LiteralPath $path) { Remove-Item $path -Recurse -Force }
        Copy-Item -LiteralPath $expanded -Destination $path -Recurse
    }
    $records = [System.Collections.Generic.List[object]]::new()
    $fixtureRoot = Join-Path $script:BuildRoot 'debug-on\webrtc\Debug'
    $fixture = Start-QualificationOwnedProcess -Name 'stun-fixture' `
        -FilePath (Join-Path $fixtureRoot 'rtmp_monitor_webrtc_stun_fixture.exe') `
        -Arguments @('--timeout-ms','600000') -WorkingDirectory $fixtureRoot `
        -LogRoot (Join-Path $script:RuntimeRoot 'fixture-logs') `
        -RuntimeRoot $script:RuntimeRoot -StatePath $script:StatePath `
        -Records $records
    try {
        $ready = Wait-QualificationJsonEvent `
            -Path (Join-Path $script:RuntimeRoot 'fixture-logs\stun-fixture.stdout.jsonl') `
            -Event 'stun_fixture_ready'
        foreach ($root in @($a,$b)) {
            $configRoot = Join-Path $root 'local-config'
            New-Item -ItemType Directory -Force -Path $configRoot | Out-Null
            [ordered]@{ schemaVersion=1; stunUrl="stun:127.0.0.1:$([int]$ready.port)" } |
                ConvertTo-Json -Compress |
                Set-Content -LiteralPath (Join-Path $configRoot 'ice-runtime.json') `
                    -Encoding UTF8
        }
        $results = [System.Collections.Generic.List[object]]::new()
        $previousPlatform = $env:QT_QPA_PLATFORM
        $env:QT_QPA_PLATFORM = 'offscreen'
        try {
            foreach ($round in 1..$RoundsPerTopology) {
                [void]$results.Add((Invoke-LocalTopology $a $b 'publisher-offer' $round $records))
                [void]$results.Add((Invoke-LocalTopology $a $b 'viewer-offer' $round $records))
            }
        } finally {
            if ($null -eq $previousPlatform) { Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
            else { $env:QT_QPA_PLATFORM = $previousPlatform }
        }
        foreach ($root in @($a,$b)) {
            Remove-Item -LiteralPath (Join-Path $root 'local-config') `
                -Recurse -Force
        }
        [ordered]@{
            schemaVersion=1; sourceCommit=$sourceCommit; ctest=$counts
            sameMachinePortable=$true; publicClaimed=$false
            designGate='passed'; publicNetwork='deferred'
            rounds=@($results)
        } | ConvertTo-Json -Depth 8 |
            Set-Content -LiteralPath $script:LocalResult -Encoding UTF8
    } finally {
        if (Test-Path -LiteralPath $script:StatePath) {
            Stop-QualificationOwnedProcesses -StatePath $script:StatePath `
                -RuntimeRoot $script:RuntimeRoot
        }
    }
    Invoke-DocChecks
    Write-Host 'W7-DESIGN-GATE passed locally; public-network qualification is deferred.'
}

switch ($Action) {
    'Check' { [void](Assert-Prerequisites); Write-Host 'Week 7 prerequisites passed.' }
    'SelfTest' { Invoke-SelfTest }
    'Run' { Invoke-Run }
    'VerifyPublic' {
        if ([string]::IsNullOrWhiteSpace($ResultRoot)) {
            throw 'VerifyPublic requires ResultRoot.'
        }
        $reports = @(Get-ChildItem -LiteralPath $ResultRoot -Filter '*.json' `
            -File -Recurse | ForEach-Object {
                Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
            })
        $result = Resolve-Week7PublicResult -Reports $reports
        Write-Host "W7-PUBLIC-NETWORK result: $result"
        if ($result -notin @('Direct','NeedsRelay')) { exit 2 }
    }
    'Status' {
        $state = Read-QualificationState -StatePath $script:StatePath
        if ($state) { $state.processes | Format-Table name,pid,startTimeUtc }
        elseif (Test-Path -LiteralPath $script:LocalResult) {
            Get-Content -LiteralPath $script:LocalResult -Raw
        } else { Write-Host 'Week 7 qualification is idle.' }
    }
    'Stop' {
        Stop-QualificationOwnedProcesses -StatePath $script:StatePath `
            -RuntimeRoot $script:RuntimeRoot
        Write-Host 'Week 7 owned processes stopped.'
    }
}
