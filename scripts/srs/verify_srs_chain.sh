#!/bin/bash
# RtmpMonitor - end-to-end SRS RTMP chain verification for Linux/ARM devices.
#
# Verifies, on the device itself:
#   FFmpeg publisher -> RTMP push -> SRS -> RTMP pull -> ffprobe
# plus the loopback HTTP API (/api/v1/versions, /api/v1/streams/).
#
# Lifecycle: start publisher -> active -> ffprobe -> [soak] -> stop ->
# inactive -> resume same URL -> active -> final stop.
# The publisher is owned by this script (tracked PID, identity-checked before
# any signal). Unknown processes are never signalled.
#
# Usage:
#   verify_srs_chain.sh [--srs-home /opt/rtmp-monitor/srs-6.0.184]
#                       [--srs-source <repo>] [--stream-key camera01]
#                       [--app live] [--soak-seconds N] [--source-file <path>]
set -uo pipefail

SRS_HOME="/opt/rtmp-monitor/srs-6.0.184"
SRS_SOURCE=""
STREAM_KEY="camera01"
APP="live"
SOAK_SECONDS=0
SOURCE_FILE=""
RTMP_PORT=1935
API_PORT=1985

while [ $# -gt 0 ]; do
    case "$1" in
        --srs-home)     SRS_HOME="$2"; shift 2 ;;
        --srs-source)   SRS_SOURCE="$2"; shift 2 ;;
        --stream-key)   STREAM_KEY="$2"; shift 2 ;;
        --app)          APP="$2"; shift 2 ;;
        --soak-seconds) SOAK_SECONDS="$2"; shift 2 ;;
        --source-file)  SOURCE_FILE="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

case "$APP" in (*[!A-Za-z0-9_-]*|'') echo "invalid --app identifier" >&2; exit 2 ;; esac
case "$STREAM_KEY" in (*[!A-Za-z0-9_-]*|'') echo "invalid --stream-key identifier" >&2; exit 2 ;; esac
case "$SOAK_SECONDS" in (*[!0-9]*|'') echo "invalid --soak-seconds value" >&2; exit 2 ;; esac

PULL_URL="rtmp://127.0.0.1:${RTMP_PORT}/${APP}/${STREAM_KEY}"
STREAM_NAME="${APP}/${STREAM_KEY}"
FAILURES=0

report() {  # report <pass 0|1> <step> <detail>
    if [ "$1" -eq 0 ]; then
        echo "[verify-srs] PASS $2 - $3"
    else
        echo "[verify-srs] FAIL $2 - $3"
        FAILURES=$((FAILURES + 1))
    fi
}

wait_stream_state() {  # <1=active|0=inactive> <timeout-sec>
    local want="$1" deadline=$(( $(date +%s) + $2 ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        local streams
        streams="$(curl --fail --silent --max-time 2 "http://127.0.0.1:${API_PORT}/api/v1/streams/" 2>/dev/null)"
        local found=0
        [ -n "$streams" ] && echo "$streams" | grep -q "$STREAM_NAME" && found=1
        [ "$found" = "$want" ] && return 0
        sleep 0.5
    done
    return 1
}

start_publisher() {  # echoes PID
    local log="$HOME/srs-run/pub-verify-$1.log"
    mkdir -p "$HOME/srs-run"
    if [ -z "$SOURCE_FILE" ]; then
        local input_args=(-re -stream_loop -1 -i "$SRS_SOURCE/trunk/doc/source.flv" -c copy)
    else
        local input_args=(-re -stream_loop -1 -i "$SOURCE_FILE" -map 0:v:0 -an
            -c:v libx264 -preset veryfast -tune zerolatency
            -pix_fmt yuv420p -g 50 -keyint_min 50 -sc_threshold 0)
    fi
    setsid nohup ffmpeg -hide_banner -loglevel warning "${input_args[@]}" \
        -f flv "$PULL_URL" > "$log" 2>&1 < /dev/null &
    local pid=$!
    disown || true
    sleep 1
    echo "$pid"
}

stop_publisher() {  # identity-checked stop
    local pid="$1"
    [ -d "/proc/$pid" ] || return 0
    local exe; exe="$(readlink "/proc/$pid/exe" 2>/dev/null)"
    case "$exe" in
        *ffmpeg*) ;;
        *) echo "[verify-srs] WARN: pid $pid exe=$exe is not ffmpeg; not signalling"; return 1 ;;
    esac
    local cmdline; cmdline="$(tr '\0' ' ' < "/proc/$pid/cmdline")"
    case "$cmdline" in
        *"$PULL_URL"*) ;;
        *) echo "[verify-srs] WARN: pid $pid cmdline does not reference our URL; not signalling"; return 1 ;;
    esac
    kill -TERM "$pid" 2>/dev/null
    local i
    for i in $(seq 1 16); do
        [ -d "/proc/$pid" ] || return 0
        sleep 0.5
    done
    # Re-read both identity fields immediately before escalation. The PID may
    # have been reused while waiting for graceful exit.
    exe="$(readlink "/proc/$pid/exe" 2>/dev/null)"
    case "$exe" in
        *ffmpeg*) ;;
        *) echo "[verify-srs] WARN: pid $pid identity changed; refusing KILL"; return 1 ;;
    esac
    cmdline="$(tr '\0' ' ' < "/proc/$pid/cmdline")"
    case "$cmdline" in
        *"$PULL_URL"*) kill -KILL "$pid" 2>/dev/null || true ;;
        *) echo "[verify-srs] WARN: pid $pid command changed; refusing KILL"; return 1 ;;
    esac
}

echo "=== verify_srs_chain.sh: stream=$STREAM_NAME rtmp-port=$RTMP_PORT"
uname -a; uname -m

# 1. environment
if [ -x "$SRS_HOME/objs/srs" ]; then
    report 0 srs-binary "$("$SRS_HOME/objs/srs" -v 2>&1 | head -1) ($SRS_HOME)"
else
    report 1 srs-binary "missing at $SRS_HOME/objs/srs"
fi
tools_missing=""
for t in ffmpeg ffprobe curl timeout; do
    command -v "$t" >/dev/null || tools_missing="$tools_missing $t"
done
[ -z "$tools_missing" ] && report 0 tools "ffmpeg/ffprobe/curl/timeout OK" || report 1 tools "missing:$tools_missing"

api="$(curl --fail --silent --max-time 3 "http://127.0.0.1:${API_PORT}/api/v1/versions" 2>/dev/null)"
[ -n "$api" ] && report 0 api-versions "$api" || { report 1 api-versions "unreachable"; echo "API unreachable; aborting."; exit 1; }

# 2. publisher lifecycle
if [ -z "$SOURCE_FILE" ] && [ ! -f "$SRS_SOURCE/trunk/doc/source.flv" ]; then
    report 1 publisher-source "no --source-file and source.flv missing under $SRS_SOURCE"
    exit 1
fi

pubpid="$(start_publisher run1)"
report 0 publisher-start "pid=$pubpid"

if wait_stream_state 1 20; then
    report 0 stream-active "$STREAM_NAME active in API"
else
    report 1 stream-active "not active within 20s"
    stop_publisher "$pubpid" || true
    exit 1
fi

probe="$(timeout 30s ffprobe -v error -show_entries stream=index,codec_type,codec_name,width,height -of json "$PULL_URL" 2>&1)"
echo "$probe" | grep -q h264 && report 0 ffprobe "$(echo "$probe" | tr -d '\n ')" || report 1 ffprobe "$probe"

if [ "$SOAK_SECONDS" -gt 0 ]; then
    echo "[verify-srs] soak: ${SOAK_SECONDS}s ..."
    deadline=$(( $(date +%s) + SOAK_SECONDS ))
    soak_ok=1
    while [ "$(date +%s)" -lt "$deadline" ]; do
        sleep 30
        [ -d "/proc/$pubpid" ] || { soak_ok=0; break; }
        wait_stream_state 1 5 || { soak_ok=0; break; }
    done
    [ "$soak_ok" = 1 ] && report 0 soak "publisher held stream ${SOAK_SECONDS}s" || report 1 soak "publisher/stream lost during soak"
fi

stop_publisher "$pubpid" || true
if wait_stream_state 0 15; then
    report 0 stream-inactive "stream disappeared after publisher stop"
else
    report 1 stream-inactive "stream still present after stop"
fi

pubpid2="$(start_publisher run2)"
if wait_stream_state 1 20; then
    report 0 stream-resume "same URL publishable again"
else
    report 1 stream-resume "resume failed"
fi
stop_publisher "$pubpid2" || true

echo "=== failures: $FAILURES"
[ "$FAILURES" -eq 0 ] && { echo "[verify-srs] VERIFY PASS"; exit 0; } || { echo "[verify-srs] VERIFY FAIL"; exit 1; }
