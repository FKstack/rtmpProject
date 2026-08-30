[CmdletBinding()]
param(
    [ValidateSet('Check','SelfTest','Run','VerifyLan','Status','Stop')]
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
Import-Module (Join-Path $PSScriptRoot 'Week6LanCommon.psm1') -Force
$script:SourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($BuildRoot)) {
    $BuildRoot = Join-Path $script:SourceRoot 'out\build-windows-x64\week6'
}
$script:BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$script:RuntimeRoot = Join-Path $script:SourceRoot 'out\webrtc-week6'
$script:AssetPath = Join-Path $script:RuntimeRoot 'webrtc-assets\sample.mp4'
$script:StatePath = Join-Path $script:RuntimeRoot 'qualification-state.json'
$script:PackageRoot = Join-Path $script:SourceRoot 'out\packages\webrtc-week6'
$script:LocalResult = Join-Path $script:RuntimeRoot 'local-portable-results.json'

function Assert-Prerequisites {
    [void](Assert-QualificationConcretePath -Value $QtRoot -Name 'QtRoot')
    [void](Assert-QualificationConcretePath -Value $VcpkgRoot -Name 'VcpkgRoot')
    if (-not [string]::IsNullOrWhiteSpace($VsDevCmd)) {
        [void](Assert-QualificationConcretePath -Value $VsDevCmd `
            -Name 'VsDevCmd')
    }
    foreach ($path in @(
            (Join-Path $QtRoot 'lib\cmake\Qt6\Qt6Config.cmake'),
            (Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'))) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Week 6 prerequisite is missing: $path"
        }
    }
    if (Test-Path -LiteralPath $script:StatePath -PathType Leaf) {
        throw 'A Week 6 state exists. Run Status or Stop.'
    }
    return Resolve-QualificationTools -VsDevCmd $VsDevCmd
}

function Invoke-Week6ConfigureBuildTest {
    param($Tools, [string]$Name, [string]$Configuration, [bool]$WebRtc)
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
    $previous = @{}
    $previousPath = $env:Path
    foreach ($name in @('RTMP_MONITOR_WEEK4_SAMPLE','RTMP_MONITOR_WEEK4_AUDIO_ONLY',
            'RTMP_MONITOR_WEEK4_NON_H264','RTMP_MONITOR_WEEK4_B_FRAMES')) {
        $previous[$name] = [Environment]::GetEnvironmentVariable($name,'Process')
    }
    try {
        $env:Path = Get-QualificationRuntimePath -QtRoot $QtRoot `
            -VcpkgRoot $VcpkgRoot -Configuration $Configuration
        if ($WebRtc) {
            $env:RTMP_MONITOR_WEEK4_SAMPLE = $script:AssetPath
            $env:RTMP_MONITOR_WEEK4_AUDIO_ONLY = Join-Path $script:RuntimeRoot 'fixtures\audio-only.mp4'
            $env:RTMP_MONITOR_WEEK4_NON_H264 = Join-Path $script:RuntimeRoot 'fixtures\non-h264.mp4'
            $env:RTMP_MONITOR_WEEK4_B_FRAMES = Join-Path $script:RuntimeRoot 'fixtures\h264-bframes.mp4'
        }
        Invoke-QualificationNative -FilePath $Tools.CTest -Arguments @(
            '--test-dir',$directory,'-C',$Configuration,'--output-on-failure') |
            Out-Host
        $listing = & $Tools.CTest --test-dir $directory -C $Configuration -N `
            2>&1 | Out-String
        if ($LASTEXITCODE -ne 0) { throw "CTest listing failed: $Name" }
        $required = if ($WebRtc) { @(
            'rtmp_monitor_webrtc_endpoint_test',
            'rtmp_monitor_webrtc_runtime_paths_test',
            'rtmp_monitor_webrtc_viewer_pipeline_test') } else {
            @('rtmp_monitor_optional_transport_disabled_test')
        }
        foreach ($test in $required) {
            if ($listing -notmatch [regex]::Escape($test)) {
                throw "Required test missing from ${Name}: $test"
            }
        }
        $count = [regex]::Match($listing,'Total Tests:\s+(\d+)').Groups[1].Value
        return [int]$count
    } finally {
        $env:Path = $previousPath
        foreach ($name in $previous.Keys) {
            if ($null -eq $previous[$name]) {
                Remove-Item "Env:$name" -ErrorAction SilentlyContinue
            } else { Set-Item "Env:$name" $previous[$name] }
        }
    }
}

function Wait-File {
    param([string]$Root, [string]$Filter, [int]$Seconds = 30)
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    do {
        $file = Get-ChildItem -LiteralPath $Root -Filter $Filter -File `
            -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($file) { return $file }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Timed out waiting for $Filter."
}

function Read-JsonEvent {
    param([string]$Path,[string]$Event)
    foreach ($line in Get-Content -LiteralPath $Path -ErrorAction SilentlyContinue) {
        try {
            $value = $line | ConvertFrom-Json -ErrorAction Stop
            if ([string]$value.event -eq $Event) { return $value }
        } catch { }
    }
    return $null
}

function Invoke-PortableTopology {
    param([string]$RootA,[string]$RootB,[string]$Topology,[int]$Round)
    $aExchange = Join-Path $RootA 'session-exchange'
    $bExchange = Join-Path $RootB 'session-exchange'
    foreach ($root in @($aExchange,$bExchange)) {
        New-Item -ItemType Directory -Force -Path $root | Out-Null
        Get-ChildItem -LiteralPath $root -Filter '*.json' -File `
            -ErrorAction SilentlyContinue | Remove-Item -Force
    }
    $logRoot = Join-Path $script:RuntimeRoot 'portable-logs'
    New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
    $offerMedia = if ($Topology -eq 'publisher-offer') { 'publisher' } else { 'viewer' }
    $answerMedia = if ($offerMedia -eq 'publisher') { 'viewer' } else { 'publisher' }
    $offerOut = Join-Path $logRoot "$Topology-$Round-offer.stdout.jsonl"
    $offerErr = Join-Path $logRoot "$Topology-$Round-offer.stderr.txt"
    $answerOut = Join-Path $logRoot "$Topology-$Round-answer.stdout.jsonl"
    $answerErr = Join-Path $logRoot "$Topology-$Round-answer.stderr.txt"
    $argsOffer = @('--media-role',$offerMedia,'--signaling-role','offer',
        '--timeout-ms','15000')
    $argsAnswer = @('--media-role',$answerMedia,'--signaling-role','answer',
        '--timeout-ms','15000')
    if ($offerMedia -eq 'publisher') { $argsOffer += @('--source','sample') }
    if ($answerMedia -eq 'publisher') { $argsAnswer += @('--source','sample') }
    $previous = $env:QT_QPA_PLATFORM
    $env:QT_QPA_PLATFORM = 'offscreen'
    $offerProcess = $null; $answerProcess = $null
    $records = [System.Collections.Generic.List[object]]::new()
    try {
        $offerProcess = Start-QualificationOwnedProcess `
            -Name "$Topology-$Round-offer" `
            -FilePath (Join-Path $RootA 'rtmp_monitor_webrtc_client.exe') `
            -Arguments $argsOffer -WorkingDirectory $RootA `
            -LogRoot $logRoot -RuntimeRoot $script:RuntimeRoot `
            -StatePath $script:StatePath -Records $records
        $offerFile = Wait-File -Root $aExchange -Filter '*.offer.json'
        Copy-Item -LiteralPath $offerFile.FullName -Destination $bExchange
        $answerProcess = Start-QualificationOwnedProcess `
            -Name "$Topology-$Round-answer" `
            -FilePath (Join-Path $RootB 'rtmp_monitor_webrtc_client.exe') `
            -Arguments $argsAnswer -WorkingDirectory $RootB `
            -LogRoot $logRoot -RuntimeRoot $script:RuntimeRoot `
            -StatePath $script:StatePath -Records $records
        $answerFile = Wait-File -Root $bExchange -Filter '*.answer.json'
        Copy-Item -LiteralPath $answerFile.FullName -Destination $aExchange
        foreach ($process in @($offerProcess,$answerProcess)) {
            if (-not $process.WaitForExit(45000)) { throw 'Portable client timed out.' }
            if ($process.ExitCode -ne 0) { throw "Portable exit code: $($process.ExitCode)" }
        }
        $events = @(
            (Read-JsonEvent $offerOut 'runtime_ready'),
            (Read-JsonEvent $answerOut 'runtime_ready'))
        foreach ($event in $events) {
            if ([string]$event.layout -ne 'portable') { throw 'Portable layout evidence missing.' }
        }
        $viewerOut = if ($offerMedia -eq 'viewer') { $offerOut } else { $answerOut }
        $viewer = Read-JsonEvent $viewerOut 'completed'
        $connected = Read-JsonEvent $viewerOut 'connected'
        if (-not $viewer -or -not $connected -or
            [int64]$viewer.receivedRtpPackets -le 0 -or
            [int64]$viewer.receivedAccessUnits -le 0 -or
            -not [bool]$viewer.decoded -or -not [bool]$viewer.presented) {
            throw 'Portable viewer evidence is incomplete.'
        }
        $pair = $connected.selectedCandidatePair
        if ($null -eq $pair -or
            [string]$pair.localTransport -ne 'udp' -or
            [string]$pair.remoteTransport -ne 'udp' -or
            [string]$pair.localType -notin @('host','srflx','relay') -or
            [string]$pair.remoteType -notin @('host','srflx','relay')) {
            throw 'Selected-pair evidence is absent or not sanitized UDP.'
        }
        Assert-QualificationSafeLogs -Paths @($offerOut,$offerErr,$answerOut,$answerErr)
        foreach ($root in @($aExchange,$bExchange)) {
            if (@(Get-ChildItem -LiteralPath $root -Filter '*.json' -File `
                    -ErrorAction SilentlyContinue).Count -ne 0) {
                throw 'Portable session-exchange was not cleaned.'
            }
        }
        return [ordered]@{ topology=$Topology; round=$Round; passed=$true;
            localType=[string]$pair.localType; remoteType=[string]$pair.remoteType }
    } finally {
        if (Test-Path -LiteralPath $script:StatePath -PathType Leaf) {
            Stop-QualificationOwnedProcesses -StatePath $script:StatePath `
                -RuntimeRoot $script:RuntimeRoot
        }
        if ($null -eq $previous) { Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
        else { $env:QT_QPA_PLATFORM = $previous }
    }
}

function Invoke-PortableCliChecks {
    param([Parameter(Mandatory = $true)][string]$PackageRoot)
    $client = Join-Path $PackageRoot 'rtmp_monitor_webrtc_client.exe'
    $previous = $env:QT_QPA_PLATFORM
    $env:QT_QPA_PLATFORM = 'offscreen'
    try {
        $help = & $client --help 2>&1 | Out-String
        if ($LASTEXITCODE -ne 0 -or
            $help -notmatch 'WebRTC V2 publisher/viewer test client') {
            throw 'Portable client help check failed.'
        }
        $invalid = & $client --media-role viewer --signaling-role offer `
            --source sample --timeout-ms 1000 2>&1 | Out-String
        if ($LASTEXITCODE -eq 0 -or $invalid -notmatch 'invalid_arguments') {
            throw 'Portable client invalid-argument check failed.'
        }
    } finally {
        if ($null -eq $previous) {
            Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
        } else { $env:QT_QPA_PLATFORM = $previous }
    }
}

function Assert-PortablePackageClean {
    param([Parameter(Mandatory = $true)][string]$PackageRoot)
    $files = @(Get-ChildItem -LiteralPath $PackageRoot -Recurse -File)
    $forbiddenExtensions = @('.pdb','.lib','.exp','.obj','.cpp','.c','.h','.hpp')
    $forbidden = @($files | Where-Object {
        $_.Extension.ToLowerInvariant() -in $forbiddenExtensions -or
        $_.Name -in @('CMakeCache.txt','qualification-state.json') -or
        $_.Name -match '\.(offer|answer)\.json$'
    })
    if ($forbidden.Count -ne 0) {
        throw "Portable package contains forbidden artifacts: $($forbidden.Name -join ', ')"
    }
    foreach ($name in @('logs','results','session-exchange')) {
        $runtimeDirectories = @(Get-ChildItem -LiteralPath $PackageRoot -Recurse `
            -Directory -ErrorAction SilentlyContinue | Where-Object {
                $_.Name -eq $name -and
                @(Get-ChildItem -LiteralPath $_.FullName -Force `
                    -ErrorAction SilentlyContinue).Count -ne 0
            })
        if ($runtimeDirectories.Count -ne 0) {
            throw "Portable package contains non-empty runtime directory: $name"
        }
    }
    $textFiles = @($files | Where-Object {
        $_.Extension.ToLowerInvariant() -in @('.json','.md','.ps1','.psm1','.txt')
    })
    foreach ($file in $textFiles) {
        $text = Get-Content -LiteralPath $file.FullName -Raw
        if ($text -match '(?i)[A-Z]:\\Users\\[^\\\s]+' -or
            $text -match '(?i)[A-Z]:\\rtmpProject') {
            throw "Portable package leaks a development-machine path: $($file.Name)"
        }
    }
}

function Invoke-DocChecks {
    $week = Join-Path $script:SourceRoot 'docs\versions\webrtc-v2\weeks\week06'
    foreach ($file in @('summary.md','testing_guide.md','test_results.md')) {
        if (-not (Test-Path -LiteralPath (Join-Path $week $file) -PathType Leaf)) {
            throw "Week 6 document missing: $file"
        }
    }
    $summary = Get-Content -LiteralPath (Join-Path $week 'summary.md') -Raw
    $guide = Get-Content -LiteralPath (Join-Path $week 'testing_guide.md') -Raw
    if (([regex]::Matches($summary,'[\u4e00-\u9fff]')).Count -lt 12000) {
        throw 'Week 6 summary has fewer than 12,000 Chinese characters.'
    }
    if (([regex]::Matches($guide,'[\u4e00-\u9fff]')).Count -lt 8000) {
        throw 'Week 6 testing guide has fewer than 8,000 Chinese characters.'
    }
    foreach ($name in @(
            'WebRtcEndpointSession','H264ReceivePipeline',
            'WebRtcClientOptions','WebRtcClientRuntimePaths',
            'WebRtcClientRuntime','WebRtcViewerController',
            'WebRtcViewerEvidence','EncodedVideoInputHandle',
            'EncodedVideoDecodeSession','LatestFrameMailbox',
            'VideoCanvasHost','CpuVideoCanvas','QualificationCommon',
            'Week6LanCommon','package_week6','qualify_week6',
            'week6_lan_test')) {
        if ($summary -notmatch [regex]::Escape($name)) {
            throw "Week 6 class/tool responsibility is missing: $name"
        }
    }
    foreach ($document in @($summary,$guide)) {
        if ($document -match '(?i)[A-Z]:\\Users\\') {
            throw 'Week 6 documentation contains a personal absolute path.'
        }
        foreach ($match in [regex]::Matches($document,'!\[[^\]]*\]\(([^)]+\.svg)\)')) {
            $imagePath = Join-Path $week $match.Groups[1].Value
            if (-not (Test-Path -LiteralPath $imagePath -PathType Leaf)) {
                throw "Week 6 image link is broken: $($match.Groups[1].Value)"
            }
        }
    }
    $svgs = @(Get-ChildItem -LiteralPath (Join-Path $week 'assets') -Filter '*.svg')
    if ($svgs.Count -lt 6) { throw 'Week 6 requires at least six SVG diagrams.' }
    foreach ($svg in $svgs) {
        [xml](Get-Content -LiteralPath $svg.FullName -Raw) | Out-Null
    }
}

function Invoke-SelfTest {
    [void][scriptblock]::Create((Get-Content -LiteralPath $PSCommandPath -Raw))
    foreach ($path in @('package_week6.ps1','week6_lan_test.ps1','Week6LanCommon.psm1')) {
        [void][scriptblock]::Create((Get-Content -LiteralPath `
            (Join-Path $PSScriptRoot $path) -Raw))
    }
    $placeholderRejected = $false
    try { Assert-QualificationConcretePath '<qt-root>' 'QtRoot' } catch { $placeholderRejected = $true }
    if (-not $placeholderRejected) { throw 'Placeholder rejection self-test failed.' }
    Invoke-DocChecks
    Write-Host 'Week 6 qualification self-test passed.'
}

function Invoke-Run {
    $tools = Assert-Prerequisites
    New-QualificationH264Fixtures -Tools $tools -AssetPath $script:AssetPath `
        -AudioOnlyPath (Join-Path $script:RuntimeRoot 'fixtures\audio-only.mp4') `
        -NonH264Path (Join-Path $script:RuntimeRoot 'fixtures\non-h264.mp4') `
        -BFramesPath (Join-Path $script:RuntimeRoot 'fixtures\h264-bframes.mp4')
    $counts = [ordered]@{}
    $counts.debugOff = Invoke-Week6ConfigureBuildTest $tools 'debug-off' 'Debug' $false
    $counts.debugOn = Invoke-Week6ConfigureBuildTest $tools 'debug-on' 'Debug' $true
    $counts.releaseOn = Invoke-Week6ConfigureBuildTest $tools 'release-on' 'Release' $true
    $sourceCommit = (& git -C $script:SourceRoot rev-parse HEAD).Trim()
    & (Join-Path $PSScriptRoot 'package_week6.ps1') `
        -BuildRoot (Join-Path $script:BuildRoot 'release-on') `
        -OutputRoot $script:PackageRoot -QtRoot $QtRoot `
        -VcpkgRoot $VcpkgRoot -SamplePath $script:AssetPath `
        -SourceCommit $sourceCommit
    if ($LASTEXITCODE -ne 0) { throw 'Week 6 package creation failed.' }
    $stage = Get-ChildItem -LiteralPath $script:PackageRoot -Directory |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $stage) { throw 'Week 6 staged package was not found.' }
    $zip = Get-ChildItem -LiteralPath $script:PackageRoot -Filter '*.zip' -File |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $zip) { throw 'Week 6 ZIP package was not found.' }
    $expanded = Join-Path $script:RuntimeRoot 'zip-expanded'
    if (Test-Path -LiteralPath $expanded) {
        Remove-Item -LiteralPath $expanded -Recurse -Force
    }
    Expand-Archive -LiteralPath $zip.FullName -DestinationPath $expanded
    Assert-PortablePackageClean -PackageRoot $expanded
    Invoke-PortableCliChecks -PackageRoot $expanded
    & (Join-Path $expanded 'week6_lan_test.ps1') -Action Check
    & (Join-Path $expanded 'week6_lan_test.ps1') -Action SelfTest
    $copyRoot = Join-Path $script:RuntimeRoot 'portable-copies'
    New-Item -ItemType Directory -Force -Path $copyRoot | Out-Null
    $a = Join-Path $copyRoot 'package-a'; $b = Join-Path $copyRoot 'package-b'
    foreach ($path in @($a,$b)) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
        Copy-Item -LiteralPath $expanded -Destination $path -Recurse
    }
    $results = [System.Collections.Generic.List[object]]::new()
    foreach ($round in 1..$RoundsPerTopology) {
        [void]$results.Add((Invoke-PortableTopology $a $b 'publisher-offer' $round))
        [void]$results.Add((Invoke-PortableTopology $a $b 'viewer-offer' $round))
    }
    [ordered]@{ schemaVersion=1; sourceCommit=$sourceCommit;
        ctest=$counts; sameMachinePortable=$true; lanClaimed=$false;
        rounds=@($results) } | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath $script:LocalResult -Encoding UTF8
    Invoke-DocChecks
    Write-Host 'Week 6 technical and same-machine portable qualification passed.'
    Write-Host 'True dual-PC LAN gate remains pending VerifyLan.'
}

switch ($Action) {
    'Check' { [void](Assert-Prerequisites); Write-Host 'Week 6 prerequisites passed.' }
    'SelfTest' { Invoke-SelfTest }
    'Run' { Invoke-Run }
    'VerifyLan' {
        if ([string]::IsNullOrWhiteSpace($ResultRoot)) {
            throw 'VerifyLan requires -ResultRoot.'
        }
        $reports = @(Get-ChildItem -LiteralPath $ResultRoot -Filter '*.json' `
            -File -Recurse | ForEach-Object {
                Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json
            })
        if ($reports.Count -eq 0 -or
            -not (Test-Week6LanReportSet -Reports $reports)) {
            throw 'W6-GATE is blocked: four valid dual-PC reports were not found.'
        }
        Write-Host 'W6-GATE passed from four dual-PC host/host UDP reports.'
    }
    'Status' {
        if (Test-Path -LiteralPath $script:LocalResult) {
            Get-Content -LiteralPath $script:LocalResult -Raw
        } else { Write-Host 'Week 6 qualification has no completed local result.' }
    }
    'Stop' {
        Stop-QualificationOwnedProcesses -StatePath $script:StatePath `
            -RuntimeRoot $script:RuntimeRoot
        Write-Host 'Week 6 owned qualification processes stopped.'
    }
}
