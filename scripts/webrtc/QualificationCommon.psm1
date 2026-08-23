Set-StrictMode -Version Latest

function Assert-QualificationConcretePath {
    param(
        [Parameter(Mandatory = $true)][string]$Value,
        [Parameter(Mandatory = $true)][string]$Name
    )
    if ([string]::IsNullOrWhiteSpace($Value) -or
        $Value -match '[<>]') {
        throw "Please replace the $Name placeholder with a real local path or set its environment variable."
    }
    return $Value
}

function Get-QualificationRuntimePath {
    param(
        [Parameter(Mandatory = $true)][string]$QtRoot,
        [Parameter(Mandatory = $true)][string]$VcpkgRoot,
        [ValidateSet('Debug','Release')][string]$Configuration
    )
    $vcpkgBin = if ($Configuration -eq 'Debug') {
        Join-Path $VcpkgRoot 'installed\x64-windows\debug\bin'
    } else {
        Join-Path $VcpkgRoot 'installed\x64-windows\bin'
    }
    return @(
        (Join-Path $QtRoot 'bin'),
        $vcpkgBin,
        (Join-Path $env:SystemRoot 'System32'),
        $env:SystemRoot
    ) -join ';'
}

function Assert-QualificationPathUnderRoot {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [string]$Label = 'qualification'
    )
    $resolved = [System.IO.Path]::GetFullPath($Path)
    $resolvedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith(
            $resolvedRoot,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the managed $Label root: $resolved"
    }
    return $resolved
}

function Invoke-QualificationNative {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath"
    }
}

function Resolve-QualificationTools {
    param([string]$VsDevCmd)
    $batch = $VsDevCmd
    if ([string]::IsNullOrWhiteSpace($batch)) {
        $vswhere = Get-Command vswhere.exe -ErrorAction SilentlyContinue
        if (-not $vswhere) {
            $programFilesX86 =
                [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
            if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
                $candidate = Join-Path $programFilesX86 `
                    'Microsoft Visual Studio\Installer\vswhere.exe'
                if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                    $vswhere = Get-Item -LiteralPath $candidate
                }
            }
        }
        if (-not $vswhere) { throw 'vswhere.exe was not found.' }
        $vswherePath = if (
            $vswhere -is [System.Management.Automation.ApplicationInfo]
        ) { $vswhere.Source } else { $vswhere.FullName }
        $installation = (& $vswherePath -latest -products '*' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath).Trim()
        if ($LASTEXITCODE -ne 0 -or
            [string]::IsNullOrWhiteSpace($installation)) {
            throw 'Visual Studio C++ installation was not found.'
        }
        $batch = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
    }
    if (-not (Test-Path -LiteralPath $batch -PathType Leaf)) {
        throw "VsDevCmd does not exist: $batch"
    }
    $batch = [System.IO.Path]::GetFullPath($batch)
    $tools = Split-Path -Parent $batch
    $common7 = Split-Path -Parent $tools
    $installationRoot = Split-Path -Parent $common7
    $cmake = Join-Path $installationRoot `
        'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    $ctest = Join-Path $installationRoot `
        'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
    foreach ($path in @($cmake, $ctest)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required Visual Studio tool is missing: $path"
        }
    }
    return [pscustomobject]@{
        VsDevCmd = $batch
        CMake = $cmake
        CTest = $ctest
        Ffmpeg = (Get-Command ffmpeg.exe -ErrorAction Stop).Source
        Ffprobe = (Get-Command ffprobe.exe -ErrorAction Stop).Source
    }
}

function Write-QualificationState {
    param(
        [Parameter(Mandatory = $true)][string]$StatePath,
        [Parameter(Mandatory = $true)][string]$RuntimeRoot,
        [Parameter(Mandatory = $true)]$Processes
    )
    New-Item -ItemType Directory -Force -Path $RuntimeRoot | Out-Null
    $temporary = $StatePath + '.tmp'
    [void](Assert-QualificationPathUnderRoot `
        -Path $temporary -Root $RuntimeRoot -Label 'runtime')
    [pscustomobject]@{
        schemaVersion = 1
        processes = @($Processes)
    } | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $StatePath -Force
}

function Read-QualificationState {
    param([Parameter(Mandatory = $true)][string]$StatePath)
    if (-not (Test-Path -LiteralPath $StatePath -PathType Leaf)) {
        return $null
    }
    return Get-Content -LiteralPath $StatePath -Raw -Encoding UTF8 |
        ConvertFrom-Json
}

function Start-QualificationOwnedProcess {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string]$LogRoot,
        [Parameter(Mandatory = $true)][string]$RuntimeRoot,
        [Parameter(Mandatory = $true)][string]$StatePath,
        [Parameter(Mandatory = $true)]$Records
    )
    $stdout = Join-Path $LogRoot ($Name + '.stdout.jsonl')
    $stderr = Join-Path $LogRoot ($Name + '.stderr.txt')
    foreach ($path in @($stdout, $stderr)) {
        [void](Assert-QualificationPathUnderRoot `
            -Path $path -Root $RuntimeRoot -Label 'runtime')
    }
    $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments `
        -WorkingDirectory $WorkingDirectory -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $process.Refresh()
    $record = [pscustomobject]@{
        name = $Name
        pid = $process.Id
        path = [System.IO.Path]::GetFullPath($FilePath)
        startTimeUtc = $process.StartTime.ToUniversalTime().ToString('o')
        stdout = $stdout
        stderr = $stderr
    }
    [void]$Records.Add($record)
    Write-QualificationState -StatePath $StatePath `
        -RuntimeRoot $RuntimeRoot -Processes $Records
    return $process
}

function Wait-QualificationJsonEvent {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Event,
        [int]$Seconds = 30
    )
    $deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $Path -PathType Leaf) {
            foreach ($line in Get-Content -LiteralPath $Path `
                         -ErrorAction SilentlyContinue) {
                try {
                    $value = $line | ConvertFrom-Json -ErrorAction Stop
                    if ([string]$value.event -eq $Event) { return $value }
                } catch {
                    continue
                }
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for safe event '$Event'."
}

function Stop-QualificationOwnedProcesses {
    param(
        [Parameter(Mandatory = $true)][string]$StatePath,
        [Parameter(Mandatory = $true)][string]$RuntimeRoot
    )
    $state = Read-QualificationState -StatePath $StatePath
    if (-not $state) { return }
    foreach ($record in @($state.processes)) {
        $process = Get-Process -Id ([int]$record.pid) `
            -ErrorAction SilentlyContinue
        if (-not $process) { continue }
        $process.Refresh()
        $expectedStart = [DateTime]::Parse(
            [string]$record.startTimeUtc,
            [Globalization.CultureInfo]::InvariantCulture,
            [Globalization.DateTimeStyles]::RoundtripKind
        ).ToUniversalTime()
        $actualStart = $process.StartTime.ToUniversalTime()
        if ($process.Path -ine [string]$record.path -or
            [Math]::Abs(($actualStart - $expectedStart).TotalSeconds) -gt 1) {
            throw "Owned process identity mismatch for PID $($record.pid); state preserved."
        }
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(3000)) {
            Stop-Process -Id $process.Id -Force
            [void]$process.WaitForExit(5000)
        }
    }
    [void](Assert-QualificationPathUnderRoot `
        -Path $StatePath -Root $RuntimeRoot -Label 'runtime')
    Remove-Item -LiteralPath $StatePath -Force -ErrorAction SilentlyContinue
}

function Assert-QualificationSafeLogs {
    param([Parameter(Mandatory = $true)][string[]]$Paths)
    $forbidden = '(?i)(candidate:|a=candidate|fingerprint|ice-ufrag|ice-pwd|stun:|turn:|token|rtmps?://|[A-Z]:\\)'
    foreach ($path in $Paths) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
        $text = Get-Content -LiteralPath $path -Raw
        if ($text -match $forbidden) {
            throw "Sensitive output pattern found in managed log: $([IO.Path]::GetFileName($path))"
        }
    }
}

function Assert-QualificationSample {
    param([Parameter(Mandatory = $true)]$Stream)
    if ($Stream.codec_name -ne 'h264' -or
        $Stream.profile -ne 'Constrained Baseline' -or
        [int]$Stream.level -ne 31 -or
        [int]$Stream.width -ne 1280 -or [int]$Stream.height -ne 720 -or
        [int]$Stream.has_b_frames -ne 0 -or
        $Stream.r_frame_rate -ne '30/1') {
        throw 'Generated sample does not match H.264 42e01f constraints.'
    }
}

function New-QualificationH264Fixtures {
    param(
        [Parameter(Mandatory = $true)]$Tools,
        [Parameter(Mandatory = $true)][string]$AssetPath,
        [Parameter(Mandatory = $true)][string]$AudioOnlyPath,
        [Parameter(Mandatory = $true)][string]$NonH264Path,
        [Parameter(Mandatory = $true)][string]$BFramesPath
    )
    New-Item -ItemType Directory -Force `
        -Path (Split-Path -Parent $AssetPath) | Out-Null
    Invoke-QualificationNative -FilePath $Tools.Ffmpeg -Arguments @(
        '-hide_banner', '-loglevel', 'error', '-y',
        '-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=30',
        '-t', '6', '-an', '-c:v', 'libx264', '-preset', 'ultrafast',
        '-profile:v', 'baseline', '-level:v', '3.1', '-pix_fmt', 'yuv420p',
        '-g', '30', '-keyint_min', '30', '-sc_threshold', '0', '-bf', '0',
        '-movflags', '+faststart', $AssetPath
    )
    $json = & $Tools.Ffprobe -v error -select_streams v:0 `
        -show_entries stream=codec_name,profile,level,width,height,r_frame_rate,has_b_frames `
        -of json $AssetPath | Out-String | ConvertFrom-Json
    if ($LASTEXITCODE -ne 0 -or @($json.streams).Count -ne 1) {
        throw 'ffprobe could not validate the generated sample.'
    }
    Assert-QualificationSample -Stream $json.streams[0]

    New-Item -ItemType Directory -Force `
        -Path (Split-Path -Parent $AudioOnlyPath) | Out-Null
    Invoke-QualificationNative -FilePath $Tools.Ffmpeg -Arguments @(
        '-hide_banner', '-loglevel', 'error', '-y',
        '-f', 'lavfi', '-i', 'sine=frequency=1000:sample_rate=48000',
        '-t', '0.25', '-vn', '-c:a', 'aac', $AudioOnlyPath
    )
    Invoke-QualificationNative -FilePath $Tools.Ffmpeg -Arguments @(
        '-hide_banner', '-loglevel', 'error', '-y',
        '-f', 'lavfi', '-i', 'testsrc2=size=64x64:rate=10',
        '-t', '0.25', '-an', '-c:v', 'mpeg4', $NonH264Path
    )
    Invoke-QualificationNative -FilePath $Tools.Ffmpeg -Arguments @(
        '-hide_banner', '-loglevel', 'error', '-y',
        '-f', 'lavfi', '-i', 'testsrc2=size=64x64:rate=30',
        '-t', '0.5', '-an', '-c:v', 'libx264', '-profile:v', 'main',
        '-g', '15', '-bf', '2', $BFramesPath
    )
}

Export-ModuleMember -Function @(
    'Assert-QualificationConcretePath',
    'Get-QualificationRuntimePath',
    'Assert-QualificationPathUnderRoot',
    'Invoke-QualificationNative',
    'Resolve-QualificationTools',
    'Write-QualificationState',
    'Read-QualificationState',
    'Start-QualificationOwnedProcess',
    'Wait-QualificationJsonEvent',
    'Stop-QualificationOwnedProcesses',
    'Assert-QualificationSafeLogs',
    'Assert-QualificationSample',
    'New-QualificationH264Fixtures'
)
