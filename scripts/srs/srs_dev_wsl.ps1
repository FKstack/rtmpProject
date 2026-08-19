#requires -Version 5.1
<#
.SYNOPSIS
    RtmpMonitor Phase 1 - manage the development SRS 6.0.184 inside WSL2.

.DESCRIPTION
    Actions: Check | Start | Status | Test | Stop

    Ownership model: the script only manages SRS processes it started itself.
    Every Start writes a state file (out/srs/srs-dev-wsl.state.json) containing
    RunId, WSL PID, executable path, config path and start time. Stop verifies
    /proc/<pid>/cmdline against the recorded identity before sending a signal.

    If TCP 1935 is already held by a process this script cannot identify as its
    own, Start fails with a report and never kills anything.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\srs\srs_dev_wsl.ps1 -Action Check
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Check', 'Start', 'Status', 'Test', 'Stop')]
    [string]$Action,

    [string]$Distro = $env:RTMP_MONITOR_WSL_DISTRO,

    # WSL-side install prefix used by the Phase 1 build steps.
    [string]$SrsHome = '$HOME/opt/srs-6.0.184',

    # WSL-side source tree (provides doc/source.flv for Test).
    [string]$SrsSource = '$HOME/src/srs-6.0.184',

    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$StreamKey = 'camera01',
    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$Application = 'live'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Distro)) {
    throw 'A WSL distribution is required. Run "wsl.exe --list --quiet", then pass -Distro <name> or set RTMP_MONITOR_WSL_DISTRO.'
}

$ScriptRoot = Split-Path -Parent $PSScriptRoot          # scripts/
$ProjectRoot = Split-Path -Parent $ScriptRoot           # repo root
$RepoConfig = Join-Path $ProjectRoot 'deploy\srs\conf\srs-minimal.conf'
$StateDir = Join-Path $ProjectRoot 'out\srs'
$StateFile = Join-Path $StateDir 'srs-dev-wsl.state.json'
$RtmpPort = 1935
$ApiPort = 1985

function Write-Step([string]$Message) {
    Write-Host "[srs-dev-wsl] $Message"
}

function Invoke-Wsl {
    # Run through bash -lc. The script is base64 encoded so PowerShell, the
    # Windows command-line parser and bash cannot reinterpret $HOME, quotes or
    # here-documents on the way to the target shell.
    param(
        [Parameter(Mandatory = $true)][string]$Script,
        [int]$TimeoutSec = 120
    )
    $normalized = ($Script -replace "`r`n", "`n").TrimEnd() + "`n"
    $payload = [Convert]::ToBase64String(
        [System.Text.UTF8Encoding]::new($false).GetBytes($normalized)
    )
    if ($Distro -notmatch '^[A-Za-z0-9._-]+$') {
        throw "Unsupported WSL distro name: $Distro"
    }
    $bashCommand = "printf '%s' '$payload' | base64 --decode | bash"
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = 'wsl.exe'
    $psi.Arguments = "-d $Distro -- bash -lc `"$bashCommand`""
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
        return [pscustomobject]@{
            ExitCode = -1
            StdOut = ''
            StdErr = "wsl.exe timed out after ${TimeoutSec}s"
        }
    }
    $proc.WaitForExit()  # ensure async stream callbacks complete
    return [pscustomobject]@{
        ExitCode = $proc.ExitCode
        StdOut = [string]$stdoutTask.Result
        StdErr = [string]$stderrTask.Result
    }
}

function Get-ListenerReport {
    # Returns WSL-side listener info for 1935/1985 as text (may be empty).
    $r = Invoke-Wsl "ss -Hltnp '( sport = :$RtmpPort or sport = :$ApiPort )' 2>/dev/null || true"
    return ($r.StdOut -replace "`r", '').Trim()
}

function Get-SrsApiVersion {
    # Queries the loopback API inside WSL; returns raw JSON or $null.
    $r = Invoke-Wsl "curl --fail --silent --show-error --max-time 3 http://127.0.0.1:${ApiPort}/api/v1/versions 2>/dev/null || true"
    $json = ($r.StdOut -replace "`r", '').Trim()
    if ([string]::IsNullOrWhiteSpace($json)) { return $null }
    return $json
}

function Read-State {
    if (-not (Test-Path $StateFile)) { return $null }
    try {
        return (Get-Content $StateFile -Raw | ConvertFrom-Json)
    } catch {
        Write-Step "WARN: state file unreadable: $($_.Exception.Message)"
        return $null
    }
}

function Write-State($State) {
    if (-not (Test-Path $StateDir)) {
        New-Item -ItemType Directory -Path $StateDir -Force | Out-Null
    }
    ($State | ConvertTo-Json -Depth 5) | Set-Content -Path $StateFile -Encoding UTF8
}

function Remove-State {
    Remove-Item $StateFile -Force -ErrorAction SilentlyContinue
}

function Test-OwnedProcess($State) {
    # Identity check: /proc/<pid>/exe resolves to the recorded binary AND the
    # cmdline references the recorded config path. Robust against relative
    # cmdline spellings like ./objs/srs.
    if ($null -eq $State) { return $false }
    $pid_ = $State.WslPid
    $script = @"
if [ ! -d /proc/$pid_ ]; then exit 1; fi
exe=`$(readlink /proc/$pid_/exe)
if [ "`$exe" != "$($State.ExePath)" ]; then exit 2; fi
cmdline=`$(tr '\0' ' ' < /proc/$pid_/cmdline)
case "`$cmdline" in
  *"$($State.ConfigPath)"*) exit 0 ;;
  *) exit 3 ;;
esac
"@
    $r = Invoke-Wsl $script
    if ($env:RTMP_MONITOR_SRS_DEBUG -eq '1') {
        Write-Step "DEBUG identity script exit=$($r.ExitCode) stderr=$($r.StdErr)"
    }
    return ($r.ExitCode -eq 0)
}

function Send-OwnedSrsSignal($State, [ValidateSet('QUIT', 'KILL')][string]$Signal) {
    # Identity validation and signal delivery deliberately happen in one WSL
    # shell, eliminating the PID-reuse race between a check call and kill call.
    if ($null -eq $State) { return $false }
    $pid_ = [int]$State.WslPid
    $script = @"
if [ ! -d /proc/$pid_ ]; then exit 10; fi
exe=`$(readlink /proc/$pid_/exe 2>/dev/null) || exit 11
[ "`$exe" = "$($State.ExePath)" ] || exit 12
cmdline=`$(tr '\0' ' ' < /proc/$pid_/cmdline) || exit 13
case "`$cmdline" in
  *"$($State.ConfigPath)"*) kill -$Signal $pid_ ;;
  *) exit 14 ;;
esac
"@
    $r = Invoke-Wsl $script
    return ($r.ExitCode -eq 0 -or $r.ExitCode -eq 10)
}

function Test-WslProcessGone([int]$ProcId) {
    $r = Invoke-Wsl "test ! -d /proc/$ProcId"
    return ($r.ExitCode -eq 0)
}

function Stop-OwnedSrsProcess($State, [int]$GraceSeconds = 10) {
    if (Test-WslProcessGone ([int]$State.WslPid)) { return $true }
    if (-not (Send-OwnedSrsSignal $State 'QUIT')) { return $false }

    $deadline = (Get-Date).AddSeconds($GraceSeconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-WslProcessGone ([int]$State.WslPid)) { return $true }
        Start-Sleep -Milliseconds 500
    }

    Write-Step "SRS still alive after ${GraceSeconds}s; revalidating identity before SIGKILL."
    if (-not (Send-OwnedSrsSignal $State 'KILL')) { return $false }
    $deadline = (Get-Date).AddSeconds(3)
    while ((Get-Date) -lt $deadline) {
        if (Test-WslProcessGone ([int]$State.WslPid)) { return $true }
        Start-Sleep -Milliseconds 250
    }
    return $false
}

function Send-OwnedPublisherSignal(
    [int]$ProcId,
    [string]$Url,
    [ValidateSet('TERM', 'KILL')][string]$Signal
) {
    $script = @"
if [ ! -d /proc/$ProcId ]; then exit 10; fi
exe=`$(readlink /proc/$ProcId/exe 2>/dev/null) || exit 11
case "`$exe" in *ffmpeg*) ;; *) exit 12 ;; esac
cmdline=`$(tr '\0' ' ' < /proc/$ProcId/cmdline) || exit 13
case "`$cmdline" in *"$Url"*) kill -$Signal $ProcId ;; *) exit 14 ;; esac
"@
    $r = Invoke-Wsl $script
    return ($r.ExitCode -eq 0 -or $r.ExitCode -eq 10)
}

function Stop-OwnedPublisher([int]$ProcId, [string]$Url) {
    if ($ProcId -le 0 -or (Test-WslProcessGone $ProcId)) { return $true }
    if (-not (Send-OwnedPublisherSignal $ProcId $Url 'TERM')) { return $false }
    $deadline = (Get-Date).AddSeconds(8)
    while ((Get-Date) -lt $deadline) {
        if (Test-WslProcessGone $ProcId) { return $true }
        Start-Sleep -Milliseconds 300
    }
    if (-not (Send-OwnedPublisherSignal $ProcId $Url 'KILL')) { return $false }
    Start-Sleep -Milliseconds 500
    return (Test-WslProcessGone $ProcId)
}

function Assert-PortFreeForStart {
    # Fails when either required port is held by anything not identified as ours.
    $listeners = Get-ListenerReport
    if ($listeners -notmatch ":$RtmpPort\b" -and
        $listeners -notmatch ":$ApiPort\b") { return }

    $state = Read-State
    if ($null -ne $state -and (Test-OwnedProcess $state)) {
        Write-Step "SRS already running as our own instance (RunId=$($state.RunId), PID=$($state.WslPid))."
        exit 0
    }

    $api = Get-SrsApiVersion
    Write-Host @"
[srs-dev-wsl] ERROR: required TCP port $RtmpPort or $ApiPort is already listening and is NOT owned by this script.
Listeners:
$listeners
API probe: $(if ($api) { $api } else { '<no response>' })
Refusing to start and refusing to kill unknown processes.
Resolve the conflict manually, then re-run -Action Start.
"@
    exit 3
}

function Invoke-Check {
    Write-Step "Distro: $Distro"
    $wslList = (& wsl.exe --list --quiet) -replace "`0", '' -replace "`r", ''
    if ((@($wslList | Where-Object { $_ -eq $Distro })).Count -eq 0) {
        throw "WSL distro '$Distro' not found. Available: $($wslList -join ', ')"
    }
    Write-Step "Distro present: OK"

    if (-not (Test-Path $RepoConfig)) {
        throw "Repo config missing: $RepoConfig"
    }
    Write-Step "Repo config: $RepoConfig"

    $r = Invoke-Wsl "test -x $SrsHome/objs/srs && $SrsHome/objs/srs -v 2>&1 | head -2 || echo SRS_BINARY_MISSING"
    $version = ($r.StdOut -replace "`r", '').Trim()
    if ($version -match 'SRS_BINARY_MISSING') {
        Write-Step "SRS binary: MISSING at $SrsHome/objs/srs (build it first, see docs/versions/rtmp-v1/architecture/srs_server_integration_plan.md section 6)"
    } else {
        Write-Step "SRS binary: $version"
    }

    $r = Invoke-Wsl "for t in ffmpeg ffprobe curl; do command -v `$t >/dev/null && echo `$t=OK || echo `$t=MISSING; done"
    Write-Step ("Tools: " + (($r.StdOut -replace "`r", '').Trim() -join ' '))

    $srcFlv = Invoke-Wsl "test -f $SrsSource/trunk/doc/source.flv && echo PRESENT || echo MISSING"
    Write-Step "source.flv: $(($srcFlv.StdOut -replace "`r", '').Trim())"

    $listeners = Get-ListenerReport
    if ([string]::IsNullOrWhiteSpace($listeners)) {
        Write-Step "Ports ${RtmpPort}/${ApiPort}: free"
    } else {
        Write-Step "Ports currently listening:`n$listeners"
        $api = Get-SrsApiVersion
        if ($api) { Write-Step "API versions: $api" }
    }

    $state = Read-State
    if ($null -ne $state) {
        $owned = Test-OwnedProcess $state
        Write-Step "State file: RunId=$($state.RunId) PID=$($state.WslPid) owned-alive=$owned"
    } else {
        Write-Step "State file: none"
    }
}

function Invoke-Start {
    $r = Invoke-Wsl "test -x $SrsHome/objs/srs && echo OK || echo MISSING"
    if (($r.StdOut -replace "`r", '').Trim() -ne 'OK') {
        throw "SRS binary missing at $SrsHome/objs/srs; build it first (plan section 6.2/6.3)."
    }

    Assert-PortFreeForStart

    $repoWsl = ((& wsl.exe -d $Distro -- wslpath -a ($ProjectRoot -replace '\\', '/')) -replace "`r", '').Trim()
    if ([string]::IsNullOrWhiteSpace($repoWsl)) { throw "wslpath failed for $ProjectRoot" }

    Write-Step "Installing config from $repoWsl/deploy/srs/conf/srs-minimal.conf"
    $r = Invoke-Wsl "install -m 0644 '$repoWsl/deploy/srs/conf/srs-minimal.conf' $SrsHome/conf/rtmp-monitor.conf && echo INSTALLED"
    if (($r.StdOut -replace "`r", '').Trim() -ne 'INSTALLED') {
        throw "Config install failed: $($r.StdErr)"
    }

    $runId = ([guid]::NewGuid().ToString('N')).Substring(0, 8)
    $logPath = "`$HOME/srs-run/srs-$runId.log"
    Write-Step "Starting SRS (RunId=$runId) ..."
    $startScript = @"
mkdir -p `$HOME/srs-run
cd $SrsHome || exit 1
setsid nohup ./objs/srs -c conf/rtmp-monitor.conf > $logPath 2>&1 < /dev/null &
echo `$!
disown || true
sleep 1
"@
    $r = Invoke-Wsl $startScript
    $wslPid = [int](($r.StdOut -replace "`r", '').Trim().Split("`n")[-1].Trim())
    if ($wslPid -le 0) { throw "Failed to obtain SRS PID: $($r.StdOut) $($r.StdErr)" }

    $state = [pscustomobject]@{
        RunId = $runId
        Status = 'starting'
        Distro = $Distro
        WslPid = $wslPid
        ExePath = "$SrsHome/objs/srs"
        ConfigPath = 'conf/rtmp-monitor.conf'
        LogPathWsl = $logPath
        StartedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        Ports = @{ Rtmp = $RtmpPort; Api = $ApiPort }
    }
    try {
        # Persist ownership immediately. A failed readiness probe can now clean
        # up the exact process it started instead of leaving an orphan.
        Write-State $state
        $deadline = (Get-Date).AddSeconds(20)
        $ready = $false
        while ((Get-Date) -lt $deadline) {
            $probe = Invoke-Wsl "ss -Hltn '( sport = :$RtmpPort )' | grep -q ':$RtmpPort' && curl --fail --silent --max-time 2 http://127.0.0.1:${ApiPort}/api/v1/versions | tr -d '[:space:]' | grep -q '`"code`":0' && echo READY || true"
            if (($probe.StdOut -replace "`r", '').Trim() -eq 'READY') { $ready = $true; break }
            Start-Sleep -Milliseconds 500
        }
        if (-not $ready) {
            $tail = Invoke-Wsl "tail -30 $logPath 2>/dev/null || true"
            throw "SRS did not become ready within 20s. Log tail:`n$(($tail.StdOut -replace "`r", ''))"
        }

        $state.Status = 'ready'
        Write-State $state
        $api = Get-SrsApiVersion
        Write-Step "SRS ready. PID=$wslPid API=$api"
        Write-Step "Log (WSL): $logPath"
    } catch {
        Write-Step "Start failed; cleaning up the identity-checked SRS process."
        if (Stop-OwnedSrsProcess $state 5) {
            Remove-State
        } else {
            Write-Step "ERROR: cleanup could not prove process ownership/exit; state retained for inspection."
        }
        throw
    }
}

function Invoke-Status {
    $state = Read-State
    if ($null -eq $state) {
        Write-Step "No state file: this script owns no running SRS."
    } else {
        $owned = Test-OwnedProcess $state
        Write-Step "RunId=$($state.RunId) PID=$($state.WslPid) started=$($state.StartedAtUtc) owned-alive=$owned"
        if (-not $owned) { Write-Step "WARN: recorded process no longer matches identity (stale state)." }
    }
    $listeners = Get-ListenerReport
    Write-Step ("Listeners: " + $(if ([string]::IsNullOrWhiteSpace($listeners)) { 'none' } else { "`n$listeners" }))
    $api = Get-SrsApiVersion
    Write-Step ("API versions: " + $(if ($api) { $api } else { '<unreachable>' }))
    if (-not [string]::IsNullOrWhiteSpace($api)) {
        $streams = Invoke-Wsl "curl --fail --silent --max-time 3 http://127.0.0.1:${ApiPort}/api/v1/streams/ 2>/dev/null || true"
        Write-Step ("API streams: " + (($streams.StdOut -replace "`r", '').Trim()))
    }
}

function Invoke-Test {
    # Quick automated push/pull chain against a running SRS.
    $api = Get-SrsApiVersion
    if (-not $api) { throw "SRS API unreachable; run -Action Start first." }
    Write-Step "API: $api"

    $url = "rtmp://127.0.0.1:${RtmpPort}/${Application}/${StreamKey}"
    $srcCheck = Invoke-Wsl "test -f $SrsSource/trunk/doc/source.flv && echo PRESENT || echo MISSING"
    if (($srcCheck.StdOut -replace "`r", '').Trim() -ne 'PRESENT') {
        throw "source.flv missing under $SrsSource; cannot run copy-codec publisher."
    }

    Write-Step "Starting owned publisher -> $url"
    $pubScript = @"
cd `$HOME
setsid nohup ffmpeg -hide_banner -loglevel warning -re -stream_loop -1 \
  -i $SrsSource/trunk/doc/source.flv -c copy -f flv $url \
  > `$HOME/srs-run/pub-test.log 2>&1 < /dev/null &
echo `$!
disown || true
sleep 1
"@
    $r = Invoke-Wsl $pubScript
    $pubPid = [int](($r.StdOut -replace "`r", '').Trim().Split("`n")[-1].Trim())
    Write-Step "Publisher PID=$pubPid"

    $stopped = $false
    try {
        $deadline = (Get-Date).AddSeconds(20)
        $active = $false
        while ((Get-Date) -lt $deadline) {
            $probe = Invoke-Wsl "curl --fail --silent --max-time 2 http://127.0.0.1:${ApiPort}/api/v1/streams/ | grep -q '$Application/$StreamKey' && echo ACTIVE || true"
            if (($probe.StdOut -replace "`r", '').Trim() -eq 'ACTIVE') { $active = $true; break }
            Start-Sleep -Milliseconds 500
        }
        if (-not $active) { throw "Stream $Application/$StreamKey did not become active within 20s." }
        Write-Step "Stream active in API: OK"

        $probe = Invoke-Wsl "ffprobe -v error -show_entries stream=index,codec_type,codec_name,width,height -of json '$url' 2>&1"
        Write-Step "ffprobe (WSL pull): $(($probe.StdOut -replace "`r", '').Trim())"
        if (($probe.StdOut -replace "`r", '') -notmatch 'h264') {
            throw "ffprobe did not report an h264 stream."
        }

        Write-Step "Stopping publisher and waiting for stream to disappear..."
        $stopped = Stop-OwnedPublisher $pubPid $url
        if (-not $stopped) {
            throw "Publisher identity changed; refusing to signal PID $pubPid."
        }
        $deadline = (Get-Date).AddSeconds(15)
        $gone = $false
        while ((Get-Date) -lt $deadline) {
            $probe = Invoke-Wsl "curl --fail --silent --max-time 2 http://127.0.0.1:${ApiPort}/api/v1/streams/ | grep -q '$Application/$StreamKey' && echo STILL || true"
            if (($probe.StdOut -replace "`r", '').Trim() -ne 'STILL') { $gone = $true; break }
            Start-Sleep -Milliseconds 500
        }
        if (-not $gone) { throw "Stream did not disappear after publisher stop." }
        Write-Step "Stream inactive after stop: OK"
        Write-Step "TEST PASS"
    } finally {
        if (-not $stopped) {
            if (-not (Stop-OwnedPublisher $pubPid $url)) {
                Write-Step "WARN: publisher cleanup refused because identity changed."
            }
        }
    }
}

function Invoke-Stop {
    $state = Read-State
    if ($null -eq $state) {
        Write-Step "No state file: nothing owned to stop."
        exit 0
    }

    $pid_ = $state.WslPid
    $alive = Invoke-Wsl "test -d /proc/$pid_ && echo ALIVE || echo GONE"
    if (($alive.StdOut -replace "`r", '').Trim() -ne 'ALIVE') {
        Write-Step "Recorded PID $pid_ already gone; removing stale state."
        Remove-State
        exit 0
    }

    if (-not (Test-OwnedProcess $state)) {
        Write-Step @"
ERROR: PID $pid_ exists but its cmdline no longer matches the recorded identity
(exe=$($state.ExePath), config=$($state.ConfigPath)). Refusing to signal it.
"@
        exit 4
    }

    Write-Step "Stopping owned SRS (RunId=$($state.RunId), PID=$pid_) with SIGQUIT..."
    if (-not (Stop-OwnedSrsProcess $state 10)) {
        Write-Step "ERROR: SRS did not exit or identity changed; retaining state and refusing further signals."
        exit 4
    }

    $listeners = Get-ListenerReport
    if ($listeners -match ":$RtmpPort\b" -or $listeners -match ":$ApiPort\b") {
        Write-Step "WARN: ports still listening after stop:`n$listeners"
    } else {
        Write-Step "Ports ${RtmpPort}/${ApiPort} released."
    }
    if (-not (Test-WslProcessGone ([int]$pid_))) {
        Write-Step "ERROR: SRS PID still exists; retaining state."
        exit 4
    }
    Remove-State
    Write-Step "STOP OK"
}

switch ($Action) {
    'Check'  { Invoke-Check }
    'Start'  { Invoke-Start }
    'Status' { Invoke-Status }
    'Test'   { Invoke-Test }
    'Stop'   { Invoke-Stop }
}
