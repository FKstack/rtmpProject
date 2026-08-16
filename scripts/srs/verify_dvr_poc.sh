#!/bin/bash
# End-to-end verifier for the opt-in SRS 6.0.184 DVR receipt PoC.
# It never starts the Qt application and never edits an installed SRS config.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SRS_HOME="$HOME/opt/srs-6.0.184"
SRS_SOURCE="$HOME/src/srs-6.0.184"
SOURCE_FILE=""
RTMP_PORT=1936
API_PORT=1986
CALLBACK_PORT=18085
APP=live
STREAM=camera_poc

while [ $# -gt 0 ]; do
    case "$1" in
        --srs-home) SRS_HOME="$2"; shift 2 ;;
        --srs-source) SRS_SOURCE="$2"; shift 2 ;;
        --source-file) SOURCE_FILE="$2"; shift 2 ;;
        --rtmp-port) RTMP_PORT="$2"; shift 2 ;;
        --api-port) API_PORT="$2"; shift 2 ;;
        --callback-port) CALLBACK_PORT="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

case "$RTMP_PORT:$API_PORT:$CALLBACK_PORT" in (*[!0-9:]*) echo "ports must be numeric" >&2; exit 2 ;; esac

SRS_BIN="$SRS_HOME/objs/srs"
ADAPTER="$REPOSITORY_ROOT/scripts/srs/dvr_receipt_adapter.py"
TEMPLATE="$REPOSITORY_ROOT/deploy/srs/conf/srs-dvr-poc.conf.in"
BASELINE_CONFIG="$REPOSITORY_ROOT/deploy/srs/conf/srs-minimal.conf"
if [ -z "$SOURCE_FILE" ]; then SOURCE_FILE="$SRS_SOURCE/trunk/doc/source.flv"; fi

FAILURES=0
TEMP_ROOT=""
DVR_ROOT=""
SPOOL_ROOT=""
CONFIG=""
SRS_PID=""
SRS_CONFIG_ACTIVE=""
ADAPTER_PID=""
PUBLISHER_PIDS=""
LAST_PUBLISHER_PID=""

report() {
    if [ "$1" -eq 0 ]; then
        echo "[dvr-poc] PASS $2 - $3"
    else
        echo "[dvr-poc] FAIL $2 - $3"
        FAILURES=$((FAILURES + 1))
    fi
}

pid_cmdline_contains() {
    [ -n "$1" ] && [ -r "/proc/$1/cmdline" ] || return 1
    tr '\0' ' ' < "/proc/$1/cmdline" | grep -Fq -- "$2"
}

wait_pid_exit() {
    local pid="$1" i
    for i in $(seq 1 40); do
        if [ ! -d "/proc/$pid" ]; then return 0; fi
        if [ "$(awk '{print $3}' "/proc/$pid/stat" 2>/dev/null)" = "Z" ]; then
            wait "$pid" 2>/dev/null || true
            return 0
        fi
        sleep 0.25
    done
    return 1
}

stop_publisher() {
    local pid="$1"
    [ -d "/proc/$pid" ] || return 0
    if ! pid_cmdline_contains "$pid" "rtmp://127.0.0.1:"; then
        echo "[dvr-poc] WARN publisher PID $pid identity mismatch; not signalling" >&2
        return 1
    fi
    kill -TERM "$pid" 2>/dev/null || true
    wait_pid_exit "$pid"
}

stop_srs() {
    [ -n "$SRS_PID" ] && [ -d "/proc/$SRS_PID" ] || { SRS_PID=""; return 0; }
    if ! pid_cmdline_contains "$SRS_PID" "$SRS_CONFIG_ACTIVE"; then
        echo "[dvr-poc] WARN SRS PID $SRS_PID identity mismatch; not signalling" >&2
        return 1
    fi
    kill -QUIT "$SRS_PID" 2>/dev/null || true
    if ! wait_pid_exit "$SRS_PID"; then
        echo "[dvr-poc] WARN SRS PID $SRS_PID did not exit after SIGQUIT" >&2
        return 1
    fi
    SRS_PID=""
}

stop_adapter() {
    [ -n "$ADAPTER_PID" ] && [ -d "/proc/$ADAPTER_PID" ] || { ADAPTER_PID=""; return 0; }
    if ! pid_cmdline_contains "$ADAPTER_PID" "$SPOOL_ROOT"; then
        echo "[dvr-poc] WARN adapter PID $ADAPTER_PID identity mismatch; not signalling" >&2
        return 1
    fi
    kill -TERM "$ADAPTER_PID" 2>/dev/null || true
    if ! wait_pid_exit "$ADAPTER_PID"; then
        echo "[dvr-poc] WARN adapter PID $ADAPTER_PID did not exit after SIGTERM" >&2
        return 1
    fi
    ADAPTER_PID=""
}

cleanup() {
    local pid
    for pid in $PUBLISHER_PIDS; do stop_publisher "$pid" || true; done
    stop_srs || true
    stop_adapter || true
    if [ -n "$TEMP_ROOT" ]; then
        case "$(realpath -m "$TEMP_ROOT")" in
            /tmp/rtmp-monitor-dvr-poc.*) rm -rf -- "$TEMP_ROOT" ;;
            *) echo "[dvr-poc] WARN refusing to remove unexpected temp path: $TEMP_ROOT" >&2 ;;
        esac
    fi
}
trap cleanup EXIT INT TERM

port_is_free() {
    ! ss -ltn "sport = :$1" 2>/dev/null | tail -n +2 | grep -q .
}

wait_http() {
    local url="$1" deadline=$(( $(date +%s) + ${2:-15} ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        curl --fail --silent --max-time 1 "$url" >/dev/null 2>&1 && return 0
        sleep 0.25
    done
    return 1
}

receipt_count() {
    find "$SPOOL_ROOT/receipts" -maxdepth 1 -type f -name '*.json' 2>/dev/null | wc -l
}

segment_count() {
    find "$DVR_ROOT" -type f -name '*.flv' 2>/dev/null | wc -l
}

wait_receipts() {
    local wanted="$1" deadline=$(( $(date +%s) + ${2:-20} ))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        [ "$(receipt_count)" -ge "$wanted" ] && return 0
        sleep 0.5
    done
    return 1
}

start_adapter() {
    local minimum_free="${1:-0}"
    python3 "$ADAPTER" serve --listen 127.0.0.1 --port "$CALLBACK_PORT" \
        --dvr-root "$DVR_ROOT" --spool-root "$SPOOL_ROOT" --ffprobe "$(command -v ffprobe)" \
        --minimum-free-bytes "$minimum_free" >"$TEMP_ROOT/adapter.log" 2>&1 &
    ADAPTER_PID=$!
    if wait_http "http://127.0.0.1:$CALLBACK_PORT/healthz" 10; then return 0; fi
    echo "[dvr-poc] adapter log:" >&2
    tail -40 "$TEMP_ROOT/adapter.log" >&2 || true
    return 1
}

start_srs() {
    local config="$1" label="$2"
    SRS_CONFIG_ACTIVE="$config"
    (cd "$SRS_HOME" && exec "$SRS_BIN" -c "$config") >"$TEMP_ROOT/srs-$label.log" 2>&1 &
    SRS_PID=$!
    if wait_http "http://127.0.0.1:$API_PORT/api/v1/versions" 15; then return 0; fi
    echo "[dvr-poc] SRS log:" >&2
    tail -60 "$TEMP_ROOT/srs-$label.log" >&2 || true
    return 1
}

start_publisher() {
    local stream="$1" label="$2"
    local url="rtmp://127.0.0.1:$RTMP_PORT/$APP/$stream"
    ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -i "$SOURCE_FILE" -c copy -f flv "$url" \
        >"$TEMP_ROOT/publisher-$label.log" 2>&1 &
    LAST_PUBLISHER_PID=$!
    PUBLISHER_PIDS="$PUBLISHER_PIDS $LAST_PUBLISHER_PID"
}

make_callback_json() {
    python3 - "$1" "$2" "$3" <<'PY'
import json, sys
print(json.dumps({
    "action": "on_dvr", "server_id": "replay-server", "service_id": "replay-service",
    "stream_id": "replay-stream", "vhost": "__defaultVhost__", "app": sys.argv[2],
    "stream": sys.argv[3], "file": sys.argv[1], "tcUrl": "redacted-by-adapter",
    "param": "must-not-be-stored", "ip": "192.0.2.1", "cwd": "/not-stored"
}))
PY
}

post_callback() {
    local file="$1" stream="$2" expected="${3:-200}" payload status
    payload="$(make_callback_json "$file" "$APP" "$stream")"
    status="$(curl --silent --output /dev/null --write-out '%{http_code}' --max-time 5 \
        -H 'Content-Type: application/json' --data-binary "$payload" \
        "http://127.0.0.1:$CALLBACK_PORT/api/v1/dvrs" || true)"
    [ "$status" = "$expected" ]
}

validate_segments() {
    local file duration first_key failures=0
    while IFS= read -r file; do
        duration="$(ffprobe -v error -show_entries format=duration -of default=nw=1:nk=1 "$file" 2>/dev/null)" || failures=$((failures + 1))
        awk -v d="$duration" 'BEGIN { exit !(d > 0 && d <= 20.5) }' || failures=$((failures + 1))
        first_key="$(ffprobe -v error -select_streams v:0 -show_entries frame=key_frame \
            -of default=nw=1:nk=1 -read_intervals '%+#1' "$file" 2>/dev/null | head -1)"
        [ "$first_key" = "1" ] || failures=$((failures + 1))
    done < <(find "$DVR_ROOT/$APP/$STREAM" -type f -name '*.flv' | sort)
    [ "$failures" -eq 0 ]
}

echo "=== SRS DVR receipt PoC verifier ==="
for tool in python3 ffmpeg ffprobe curl ss realpath timeout; do
    command -v "$tool" >/dev/null 2>&1 || { echo "missing required tool: $tool" >&2; exit 2; }
done
[ -x "$SRS_BIN" ] || { echo "missing SRS binary: $SRS_BIN" >&2; exit 2; }
[ -f "$SOURCE_FILE" ] || { echo "missing media source: $SOURCE_FILE" >&2; exit 2; }
[ -f "$TEMPLATE" ] && [ -f "$ADAPTER" ] && [ -f "$BASELINE_CONFIG" ] || { echo "PoC source files missing" >&2; exit 2; }
version="$($SRS_BIN -v 2>&1 | head -1)"
echo "$version" | grep -Fq '6.0.184' || { echo "expected SRS 6.0.184, got: $version" >&2; exit 2; }
report 0 version "$version"

for port in "$RTMP_PORT" "$API_PORT" "$CALLBACK_PORT" 1935 1985; do
    port_is_free "$port" || { echo "port $port is already in use; refusing to disturb existing service" >&2; exit 2; }
done

TEMP_ROOT="$(mktemp -d /tmp/rtmp-monitor-dvr-poc.XXXXXX)"
DVR_ROOT="$TEMP_ROOT/dvr"
SPOOL_ROOT="$TEMP_ROOT/spool"
CONFIG="$TEMP_ROOT/srs-dvr-poc.conf"
mkdir -p "$DVR_ROOT" "$SPOOL_ROOT"
python3 - "$TEMPLATE" "$CONFIG" "$DVR_ROOT" "$RTMP_PORT" "$API_PORT" "$CALLBACK_PORT" <<'PY'
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text(encoding="utf-8")
values = {
    "__DVR_ROOT__": str(Path(sys.argv[3]).resolve()), "__RTMP_PORT__": sys.argv[4],
    "__API_PORT__": sys.argv[5], "__CALLBACK_PORT__": sys.argv[6], "__DVR_APPLY__": "all"
}
for key, value in values.items(): source = source.replace(key, value)
remaining = [key for key in values if key in source]
if remaining: raise SystemExit("unrendered placeholder: " + ",".join(remaining))
Path(sys.argv[2]).write_text(source, encoding="utf-8")
PY

start_adapter 0 || exit 1
report 0 adapter "loopback adapter ready"
start_srs "$CONFIG" poc1 || exit 1
report 0 config "rendered config accepted by pinned SRS"
report 0 srs "PoC SRS ready on independent ports"

start_publisher "$STREAM" long
publisher="$LAST_PUBLISHER_PID"
sleep 27
stop_publisher "$publisher" || true
if wait_receipts 2 20; then report 0 segmented "at least two segment receipts"; else report 1 segmented "receipts=$(receipt_count), segments=$(segment_count)"; fi
if validate_segments; then report 0 segment-media "readable, bounded duration, keyframe first"; else report 1 segment-media "one or more segment probes failed"; fi

before_tail="$(receipt_count)"
start_publisher camera_tail tail
tail_publisher="$LAST_PUBLISHER_PID"
sleep 4
stop_publisher "$tail_publisher" || true
if wait_receipts $((before_tail + 1)) 15; then report 0 tail-segment "early disconnect produced a receipt"; else report 1 tail-segment "no tail receipt"; fi

first_segment="$(find "$DVR_ROOT/$APP/$STREAM" -type f -name '*.flv' | sort | head -1)"
last_segment="$(find "$DVR_ROOT/$APP/$STREAM" -type f -name '*.flv' | sort | tail -1)"
before_replay="$(receipt_count)"
post_callback "$last_segment" "$STREAM" 200 && post_callback "$first_segment" "$STREAM" 200
[ "$(receipt_count)" -eq "$before_replay" ] && report 0 replay "duplicate and out-of-order callbacks were idempotent" || report 1 replay "duplicate receipt created"

stop_adapter || true
start_adapter 0 || exit 1
before_restart_replay="$(receipt_count)"
post_callback "$first_segment" "$STREAM" 200
[ "$(receipt_count)" -eq "$before_restart_replay" ] && report 0 adapter-restart "dedup index rebuilt" || report 1 adapter-restart "restart lost dedup index"

before_srs_restart="$(receipt_count)"
stop_srs || true
start_srs "$CONFIG" poc2 || exit 1
start_publisher "$STREAM" restart
restart_publisher="$LAST_PUBLISHER_PID"
sleep 12
stop_publisher "$restart_publisher" || true
if wait_receipts $((before_srs_restart + 1)) 15; then report 0 srs-restart "new segment receipt after restart"; else report 1 srs-restart "no new receipt"; fi

stop_adapter || true
offline_before="$(receipt_count)"
start_publisher camera_offline offline
offline_publisher="$LAST_PUBLISHER_PID"
sleep 2
if timeout 15s ffprobe -v error -show_entries stream=codec_type -of csv=p=0 \
    "rtmp://127.0.0.1:$RTMP_PORT/$APP/camera_offline" 2>/dev/null | grep -q video; then
    report 0 adapter-offline "RTMP pull continued without callback adapter"
else
    report 1 adapter-offline "RTMP pull failed"
fi
sleep 3
stop_publisher "$offline_publisher" || true
[ "$(receipt_count)" -eq "$offline_before" ] && report 0 adapter-offline-receipt "no fabricated receipt while adapter was down" || report 1 adapter-offline-receipt "unexpected receipt"

start_adapter 999999999999999 || exit 1
low_disk_before="$(receipt_count)"
offline_segment="$(find "$DVR_ROOT/$APP/camera_offline" -type f -name '*.flv' | sort | head -1)"
post_callback "$offline_segment" camera_offline 507
[ "$(receipt_count)" -eq "$low_disk_before" ] && report 0 low-disk "refused without deleting or registering" || report 1 low-disk "receipt count changed"
stop_adapter || true

# The unchanged normal configuration is exercised separately. Its fixed ports
# are checked above so an existing service is never stopped or reconfigured.
stop_srs || true
baseline_dvr_before="$(segment_count)"
baseline_spool_before="$(receipt_count)"
RTMP_PORT=1935
API_PORT=1985
start_srs "$BASELINE_CONFIG" baseline || exit 1
start_publisher camera_baseline baseline
baseline_publisher="$LAST_PUBLISHER_PID"
sleep 4
if timeout 15s ffprobe -v error -show_entries stream=codec_type -of csv=p=0 \
    "rtmp://127.0.0.1:1935/$APP/camera_baseline" 2>/dev/null | grep -q video; then
    report 0 baseline-stream "original srs-minimal.conf still pushes and pulls"
else
    report 1 baseline-stream "original configuration stream failed"
fi
stop_publisher "$baseline_publisher" || true
sleep 1
if [ "$(segment_count)" -eq "$baseline_dvr_before" ] && [ "$(receipt_count)" -eq "$baseline_spool_before" ]; then
    report 0 default-off "original configuration produced no DVR or receipt side effect"
else
    report 1 default-off "DVR or spool changed under original configuration"
fi

echo "=== failures: $FAILURES ==="
if [ "$FAILURES" -eq 0 ]; then
    echo "[dvr-poc] VERIFY PASS"
    exit 0
fi
echo "[dvr-poc] VERIFY FAIL"
exit 1
