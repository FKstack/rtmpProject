[CmdletBinding()]
param(
    [ValidateSet('Check','Scan','SelfTest','LegacyObserve','Capability','Graph')]
    [string]$Action = 'Check',
    [string]$BuildDirectory,
    [ValidateSet('Debug','Release')]
    [string]$Configuration = 'Debug',
    [string]$FixturePath,
    [string]$ConfigPath,
    [string]$ResultPath,
    [string]$ForbiddenEndpointPattern
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))

function Get-TrackedTextFiles {
    $paths = & git -C $sourceRoot ls-files
    if ($LASTEXITCODE -ne 0) { throw 'git_ls_files_failed' }
    foreach ($path in $paths) {
        if ([IO.Path]::GetExtension($path) -in @(
                '.cpp','.h','.cmake','.txt','.md','.ps1','.json','.in')) {
            Join-Path $sourceRoot $path
        }
    }
}

function Assert-NoTrackedPublicEndpoint {
    $publicLiteral = '(?i)\b(?:mqtt|mqtts|tcp|ssl|tls|http|https)://' +
        '(?!(?:127|10)\.|192\.168\.|172\.(?:1[6-9]|2[0-9]|3[01])\.|0\.0\.0\.0|<)' +
        '(?:[0-9]{1,3}\.){3}[0-9]{1,3}(?::[0-9]+)?'
    foreach ($file in Get-TrackedTextFiles) {
        $text = Get-Content -LiteralPath $file -Raw
        if ($text -match $publicLiteral) {
            throw "tracked_public_endpoint:$([IO.Path]::GetRelativePath($sourceRoot,$file))"
        }
        if (-not [string]::IsNullOrWhiteSpace($ForbiddenEndpointPattern) -and
            $text -match $ForbiddenEndpointPattern) {
            throw "tracked_forbidden_endpoint:$([IO.Path]::GetRelativePath($sourceRoot,$file))"
        }
    }
}

function Resolve-FixturePath {
    if (-not [string]::IsNullOrWhiteSpace($FixturePath)) {
        return [IO.Path]::GetFullPath($FixturePath)
    }
    if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
        throw 'fixture_or_build_directory_required'
    }
    return Join-Path ([IO.Path]::GetFullPath($BuildDirectory)) `
        "broker-fixture\$Configuration\rtmp_monitor_p2p_direct_broker_fixture.exe"
}

function Invoke-Fixture([string]$Mode) {
    $executable = Resolve-FixturePath
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw 'fixture_missing'
    }
    $arguments = @('--mode',$Mode)
    if ($Mode -ne 'self-test') {
        if ([string]::IsNullOrWhiteSpace($ConfigPath) -or
            -not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
            throw 'ignored_runtime_config_required'
        }
        $arguments += @('--config',[IO.Path]::GetFullPath($ConfigPath))
    }
    if (-not [string]::IsNullOrWhiteSpace($ResultPath)) {
        $resultFull = [IO.Path]::GetFullPath($ResultPath)
        $resultDirectory = Split-Path -Parent $resultFull
        if (-not (Test-Path -LiteralPath $resultDirectory)) {
            [void](New-Item -ItemType Directory -Path $resultDirectory)
        }
        $arguments += @('--result',$resultFull)
    }
    & $executable @arguments
    if ($LASTEXITCODE -ne 0) { throw "fixture_failed:$Mode" }
}

function Export-TargetGraph {
    if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
        throw 'build_directory_required'
    }
    $buildFull = [IO.Path]::GetFullPath($BuildDirectory)
    $cachePath = Join-Path $buildFull 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath)) { throw 'cmake_cache_missing' }
    $cmakeLine = Get-Content -LiteralPath $cachePath |
        Where-Object { $_ -match '^CMAKE_COMMAND:INTERNAL=(.+)$' } |
        Select-Object -First 1
    if (-not $cmakeLine -or $cmakeLine -notmatch '^CMAKE_COMMAND:INTERNAL=(.+)$') {
        throw 'cmake_command_unavailable'
    }
    $cmake = $Matches[1]
    $outRoot = Join-Path $sourceRoot 'out\p2p-direct-00\dag'
    [void](New-Item -ItemType Directory -Force -Path $outRoot)
    $name = Split-Path -Leaf $buildFull
    $dotPath = Join-Path $outRoot "$name.dot"
    & $cmake "--graphviz=$dotPath" $buildFull
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $dotPath)) {
        throw 'cmake_graphviz_failed'
    }

    $labels = @{}
    $edges = New-Object System.Collections.Generic.List[string]
    foreach ($line in Get-Content -LiteralPath $dotPath) {
        if ($line -match '^\s*"([^"]+)"\s+\[\s*label\s*=\s*"([^"]+)"') {
            $labels[$Matches[1]] = $Matches[2]
        }
    }
    foreach ($line in Get-Content -LiteralPath $dotPath) {
        if ($line -match '^\s*"([^"]+)"\s*->\s*"([^"]+)"') {
            $from = $labels[$Matches[1]]
            $to = $labels[$Matches[2]]
            if ($from -and $to -and
                ($from -like 'rtmp_monitor_*' -or $to -like 'rtmp_monitor_*')) {
                [void]$edges.Add("$from -> $to")
            }
        }
    }
    $normalizedPath = Join-Path $outRoot "$name-targets.txt"
    $edges | Sort-Object -Unique | Set-Content -LiteralPath $normalizedPath -Encoding utf8
    Write-Output "normalized_target_graph=$normalizedPath"
}

switch ($Action) {
    'Check' {
        foreach ($required in @(
                (Join-Path $sourceRoot 'tests\P2PDirectBrokerFixtureMain.cpp'),
                (Join-Path $sourceRoot 'cmake\CheckLayerDependencies.cmake'),
                (Join-Path $sourceRoot 'docs\versions\webrtc-v2\p2p-direct-00\README.md'))) {
            if (-not (Test-Path -LiteralPath $required)) {
                throw 'p2p_direct_00_prerequisite_missing'
            }
        }
        Assert-NoTrackedPublicEndpoint
        Write-Output 'P2P-DIRECT-00 prerequisites passed.'
    }
    'Scan' {
        Assert-NoTrackedPublicEndpoint
        Write-Output 'Tracked endpoint scan passed.'
    }
    'SelfTest' {
        Assert-NoTrackedPublicEndpoint
        Invoke-Fixture 'self-test'
    }
    'LegacyObserve' {
        Assert-NoTrackedPublicEndpoint
        Invoke-Fixture 'legacy-observe'
    }
    'Capability' {
        Assert-NoTrackedPublicEndpoint
        Invoke-Fixture 'capability'
    }
    'Graph' {
        Export-TargetGraph
    }
}
