#requires -Version 5.1
<#
.SYNOPSIS
    RtmpMonitor - end-to-end SRS RTMP chain verification (Windows + WSL2).

.DESCRIPTION
    Verifies the full chain against a running SRS (see srs_dev_wsl.ps1):

        FFmpeg publisher (WSL) -> RTMP push -> SRS -> RTMP pull -> ffprobe

    Checks run from BOTH the WSL side (loopback inside the distro) and the
    Windows side (loopback through WSL2 mirrored networking), plus the SRS
    HTTP API (/api/v1/versions, /api/v1/streams).

    Publisher lifecycle covered: start -> active -> ffprobe -> [soak] ->
    stop -> inactive -> resume same URL -> active again -> final stop.

    Writes a structured JSON report to out/srs/verify-srs-chain-<timestamp>.json.
    The publisher is always owned by this script (tracked PID) and is stopped
    even on failure. Unknown processes are never signalled.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\srs\verify_srs_chain.ps1 -Action Verify
.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\srs\verify_srs_chain.ps1 -Action Verify -SoakSeconds 600
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Check', 'Verify')]
    [string]$Action,

    [string]$Distro = $env:RTMP_MONITOR_WSL_DISTRO,
    [string]$SrsHome = '$HOME/opt/srs-6.0.184',
    [string]$SrsSource = '$HOME/src/srs-6.0.184',

    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$StreamKey = 'camera01',
    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Application = 'live',

    # Seconds to keep the first publisher running before the stop/resume cycle.
    [int]$SoakSeconds = 0,

    # Optional extra input (WSL-side path). Defaults to SRS doc/source.flv with
    # copy codecs. When set, the publisher re-encodes with libx264.
    [string]$SourceFile = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Distro)) {
    throw 'A WSL distribution is required. Run "wsl.exe --list --quiet", then pass -Distro <name> or set RTMP_MONITOR_WSL_DISTRO.'
}

$ScriptRoot = Split-Path -Parent $PSScriptRoot
$ProjectRoot = Split-Path -Parent $ScriptRoot
$ReportDir = Join-Path $ProjectRoot 'out\srs'
$RtmpPort = 1935
$ApiPort = 1985
$PullUrl = "rtmp://127.0.0.1:${RtmpPort}/${Application}/${StreamKey}"
$StreamName = "${Application}/${StreamKey}"

$script:Results = [System.Collections.Generic.List[object]]::new()

function Add-Result([string]$Step, [bool]$Pass, [string]$Detail) {
    $script:Results.Add([pscustomobject]@{
        step = $Step; pass = $Pass; detail = $Detail
        atUtc = (Get-Date).ToUniversalTime().ToString('o')
    })
    $mark = if ($Pass) { 'PASS' } else { 'FAIL' }
    Write-Host "[verify-srs] $mark $Step - $Detail"
}

function Invoke-Wsl {
    # bash script via stdin (bash -s); see srs_dev_wsl.ps1 for rationale.
    # Uses ProcessStartInfo directly: Start-Process loses ExitCode when stdin
    # is redirected. Streams are drained async to avoid pipe-buffer deadlock.
    param(
        [Parameter(Mandatory = $true)][string]$Script,
        [int]$TimeoutSec = 120
    )
    $normalized = $Script -replace "`r`n", "`n"
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'wsl.exe'
    $psi.Arguments = "-d $Distro -- bash -s"
    $psi.UseShellExecute = $false
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    [void]$proc.Start()
    $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
    $stderrTask = $proc.StandardError.ReadToEndAsync()
    $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($normalized)
    $proc.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
    $proc.StandardInput.Close()
    if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
        try { $proc.Kill() } catch { }
        return [pscustomobject]@{ ExitCode = -1
            StdOut = ''
            StdErr = "wsl.exe timed out after ${TimeoutSec}s" }
    }
    $proc.WaitForExit()  # ensure async stream callbacks complete
    return [pscustomobject]@{ ExitCode = $proc.ExitCode
        StdOut = [string]$stdoutTask.Result
        StdErr = [string]$stderrTask.Result }
}

function Get-ApiJson([string]$Path, [int]$TimeoutSec = 5) {
    $r = Invoke-Wsl "curl --fail --silent --show-error --max-time 3 http://127.0.0.1:${ApiPort}${Path} 2>/dev/null || true" $TimeoutSec
    $json = ($r.StdOut -replace "`r", '').Trim()
    if ([string]::IsNullOrWhiteSpace($json)) { return $null }
    return $json
}

function Wait-StreamState([bool]$WantActive, [int]$TimeoutSec = 20) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $streams = Get-ApiJson '/api/v1/streams/'
        $found = ($null -ne $streams -and $streams.Contains($StreamName))
        if ($found -eq $WantActive) { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Start-OwnedPublisher([string]$Tag) {
    if ([string]::IsNullOrWhiteSpace($SourceFile)) {
        $inputArgs = "-re -stream_loop -1 -i $SrsSource/trunk/doc/source.flv -c copy"
    } else {
        $inputArgs = "-re -stream_loop -1 -i '$SourceFile' -map 0:v:0 -an -c:v libx264 -preset veryfast -tune zerolatency -pix_fmt yuv420p -g 50 -keyint_min 50 -sc_threshold 0"
    }
    $pubScript = @"
mkdir -p `$HOME/srs-run
setsid nohup ffmpeg -hide_banner -loglevel warning $inputArgs -f flv $PullUrl \
  > `$HOME/srs-run/pub-$Tag.log 2>&1 < /dev/null &
echo `$!
disown || true
sleep 1
"@
    $r = Invoke-Wsl $pubScript
    $procId = [int](($r.StdOut -replace "`r", '').Trim().Split("`n")[-1].Trim())
    if ($procId -le 0) { throw "publisher start failed: $($r.StdOut) $($r.StdErr)" }
    return $procId
}

function Stop-OwnedPublisher([int]$ProcId) {
    if ($ProcId -le 0) { return }
    # Identity check: /proc cmdline must be our ffmpeg publishing to our URL.
    $check = @"
if [ ! -d /proc/$ProcId ]; then exit 0; fi
cmdline=`$(tr '\0' ' ' < /proc/$ProcId/cmdline)
case "`$cmdline" in
  *ffmpeg*"$PullUrl"*) kill -TERM $ProcId ;;
  *) exit 2 ;;
esac
"@
    $termResult = Invoke-Wsl $check
    if ($termResult.ExitCode -notin @(0, 10)) {
        Write-Host "[verify-srs] WARN publisher identity mismatch; refusing TERM for PID $ProcId"
        return $false
    }
    $deadline = (Get-Date).AddSeconds(8)
    while ((Get-Date) -lt $deadline) {
        $alive = Invoke-Wsl "test -d /proc/$ProcId && echo ALIVE || echo GONE"
        if (($alive.StdOut -replace "`r", '').Trim() -eq 'GONE') { return $true }
        Start-Sleep -Milliseconds 300
    }
    $killCheck = @"
if [ ! -d /proc/$ProcId ]; then exit 10; fi
exe=`$(readlink /proc/$ProcId/exe 2>/dev/null) || exit 11
case "`$exe" in *ffmpeg*) ;; *) exit 12 ;; esac
cmdline=`$(tr '\0' ' ' < /proc/$ProcId/cmdline) || exit 13
case "`$cmdline" in *"$PullUrl"*) kill -KILL $ProcId ;; *) exit 14 ;; esac
"@
    $killResult = Invoke-Wsl $killCheck
    if ($killResult.ExitCode -notin @(0, 10)) {
        Write-Host "[verify-srs] WARN publisher identity mismatch; refusing KILL for PID $ProcId"
        return $false
    }
    return $true
}

function Invoke-WindowsFfprobe([int]$TimeoutSec = 30) {
    $command = Get-Command 'ffprobe.exe' -ErrorAction Stop
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $command.Source
    $psi.Arguments = "-v error -show_entries stream=index,codec_type,codec_name,width,height -of json `"$PullUrl`""
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    [void]$proc.Start()
    $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
    $stderrTask = $proc.StandardError.ReadToEndAsync()
    if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
        try { $proc.Kill() } catch { }
        return [pscustomobject]@{ ExitCode = -1; Text = "ffprobe timed out after ${TimeoutSec}s" }
    }
    $proc.WaitForExit()
    return [pscustomobject]@{
        ExitCode = $proc.ExitCode
        Text = (([string]$stdoutTask.Result + [string]$stderrTask.Result).Trim())
    }
}

function Write-Report([bool]$OverallPass) {
    if (-not (Test-Path $ReportDir)) {
        New-Item -ItemType Directory -Path $ReportDir -Force | Out-Null
    }
    $stamp = (Get-Date).ToString('yyyyMMdd-HHmmss')
    $path = Join-Path $ReportDir "verify-srs-chain-$stamp.json"
    $report = [pscustomobject]@{
        tool = 'verify_srs_chain.ps1'
        action = $Action
        distro = $Distro
        stream = $StreamName
        soakSeconds = $SoakSeconds
        overallPass = $OverallPass
        results = $script:Results
    }
    ($report | ConvertTo-Json -Depth 6) | Set-Content -Path $path -Encoding UTF8
    Write-Host "[verify-srs] report: $path"
}

function Invoke-Check {
    $wslList = (& wsl.exe --list --quiet) -replace "`0", '' -replace "`r", ''
    $ok = ((@($wslList | Where-Object { $_ -eq $Distro })).Count -gt 0)
    Add-Result 'wsl-distro' $ok "distro=$Distro"

    $r = Invoke-Wsl "test -x $SrsHome/objs/srs && $SrsHome/objs/srs -v 2>&1 | head -1 || echo MISSING"
    $ver = ($r.StdOut -replace "`r", '').Trim()
    Add-Result 'srs-binary' ($ver -notmatch 'MISSING') $ver

    $r = Invoke-Wsl "for t in ffmpeg ffprobe curl; do command -v `$t >/dev/null && echo `$t=OK || echo `$t=MISSING; done"
    $tools = ($r.StdOut -replace "`r", '').Trim()
    Add-Result 'wsl-tools' ($tools -notmatch 'MISSING') ($tools -join ' ')

    foreach ($tool in 'ffmpeg', 'ffprobe', 'ffplay') {
        $found = (Get-Command "$tool.exe" -ErrorAction SilentlyContinue) -ne $null
        Add-Result "windows-$tool" $found $(if ($found) { 'on PATH' } else { 'not found' })
    }

    $api = Get-ApiJson '/api/v1/versions'
    Add-Result 'api-versions-wsl' ($null -ne $api) $(if ($api) { $api } else { 'unreachable' })

    $winApiOk = $false; $winApiText = ''
    try {
        $resp = Invoke-RestMethod -Uri "http://127.0.0.1:${ApiPort}/api/v1/versions" -TimeoutSec 3
        $winApiOk = ($resp.code -eq 0)
        $winApiText = ($resp | ConvertTo-Json -Compress)
    } catch { $winApiText = $_.Exception.Message }
    Add-Result 'api-versions-windows' $winApiOk $winApiText
}

function Invoke-Verify {
    Invoke-Check
    if ((@($script:Results | Where-Object { -not $_.pass -and $_.step -in @('wsl-distro', 'srs-binary', 'wsl-tools', 'api-versions-wsl') })).Count -gt 0) {
        Write-Report $false
        throw "Preconditions failed; see report."
    }

    $pubPid = 0
    try {
        $pubPid = Start-OwnedPublisher 'verify'
        Add-Result 'publisher-start' ($pubPid -gt 0) "pid=$pubPid stream=$StreamName"

        $active = Wait-StreamState $true 20
        Add-Result 'stream-active' $active "stream=$StreamName visible in /api/v1/streams"
        if (-not $active) { throw "stream never became active" }

        $probe = Invoke-Wsl "ffprobe -v error -show_entries stream=index,codec_type,codec_name,width,height -of json '$PullUrl' 2>&1" 30
        $probeText = ($probe.StdOut -replace "`r", '').Trim()
        Add-Result 'ffprobe-wsl' ($probeText -match 'h264') $probeText

        $winProbeText = ''
        $winProbeOk = $false
        try {
            $winProbe = Invoke-WindowsFfprobe 30
            $winProbeText = $winProbe.Text
            $winProbeOk = ($winProbe.ExitCode -eq 0 -and $winProbeText -match 'h264')
        } catch { $winProbeText = $_.Exception.Message }
        Add-Result 'ffprobe-windows' $winProbeOk $winProbeText

        if ($SoakSeconds -gt 0) {
            Write-Host "[verify-srs] soak: keeping publisher for ${SoakSeconds}s ..."
            $soakDeadline = (Get-Date).AddSeconds($SoakSeconds)
            $sampleFail = $false
            while ((Get-Date) -lt $soakDeadline) {
                Start-Sleep -Seconds ([Math]::Min(30, [Math]::Max(1, [int](($soakDeadline - (Get-Date)).TotalSeconds))))
                $streams = Get-ApiJson '/api/v1/streams/'
                if ($null -eq $streams -or -not $streams.Contains($StreamName)) { $sampleFail = $true; break }
                $alive = Invoke-Wsl "test -d /proc/$pubPid && echo ALIVE || echo GONE"
                if (($alive.StdOut -replace "`r", '').Trim() -ne 'ALIVE') { $sampleFail = $true; break }
            }
            Add-Result 'soak' (-not $sampleFail) "publisher held stream for ${SoakSeconds}s"
        }

        if (-not (Stop-OwnedPublisher $pubPid)) {
            throw "publisher identity changed during stop; refusing further signals"
        }
        $pubPid = 0
        $inactive = Wait-StreamState $false 15
        Add-Result 'stream-inactive-after-stop' $inactive 'stream disappeared from API after publisher stop'

        $pubPid = Start-OwnedPublisher 'verify-resume'
        $resumed = Wait-StreamState $true 20
        Add-Result 'stream-resume-same-url' $resumed 'same URL publishable again after stop'
    } finally {
        if ($pubPid -gt 0) {
            [void](Stop-OwnedPublisher $pubPid)
        }
    }

    $overall = (@($script:Results | Where-Object { -not $_.pass })).Count -eq 0
    Write-Report $overall
    if (-not $overall) { exit 1 }
    Write-Host "[verify-srs] VERIFY PASS"
}

switch ($Action) {
    'Check'  { Invoke-Check;  Write-Report ((@($script:Results | Where-Object { -not $_.pass })).Count -eq 0) }
    'Verify' { Invoke-Verify }
}
