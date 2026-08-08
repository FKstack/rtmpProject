#!/usr/bin/env bash
# qualify_embedded_device.sh — 嵌入式目标板渲染/路数资格测试
#
# 由用户在真实目标板上手动运行；不得在 CI、WSL2 或应用首次启动时自动执行。
# 每一档路数先预热再采样；任一门槛失败即停止升档，并保留已完成档位的报告。
# 输出设备档案只声明实测通过的内容（recommendedMaxStreams），不写成通用
# ARM 能力结论。
#
# 用法示例：
#   ./qualify_embedded_device.sh \
#       --app /opt/rtmpmonitor/rtmp_monitor \
#       --url-template 'rtmp://192.168.1.10:1935/live/cam%02d' \
#       --ladder "1 4 9 16" --warmup 20 --sample 120 \
#       --max-cpu-percent 85 --max-frame-age-p95-ms 200 \
#       --max-temp-millic 85000 --output ./qualification-report
#
set -euo pipefail

APP=""
URL_TEMPLATE=""
LADDER="1 4 9 16"
WARMUP_SECONDS=20
SAMPLE_SECONDS=120
MAX_CPU_PERCENT=85
MAX_FRAME_AGE_P95_MS=200
MAX_TEMP_MILLIC=85000
MAX_RSS_KIB=""
OUTPUT_DIR="./qualification-report"
RENDERER="auto"
EXTRA_ARGS=""

usage() {
    sed -n '2,20p' "$0"
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --app) APP="$2"; shift 2 ;;
        --url-template) URL_TEMPLATE="$2"; shift 2 ;;
        --ladder) LADDER="$2"; shift 2 ;;
        --warmup) WARMUP_SECONDS="$2"; shift 2 ;;
        --sample) SAMPLE_SECONDS="$2"; shift 2 ;;
        --max-cpu-percent) MAX_CPU_PERCENT="$2"; shift 2 ;;
        --max-frame-age-p95-ms) MAX_FRAME_AGE_P95_MS="$2"; shift 2 ;;
        --max-temp-millic) MAX_TEMP_MILLIC="$2"; shift 2 ;;
        --max-rss-kib) MAX_RSS_KIB="$2"; shift 2 ;;
        --renderer) RENDERER="$2"; shift 2 ;;
        --output) OUTPUT_DIR="$2"; shift 2 ;;
        --extra-args) EXTRA_ARGS="$2"; shift 2 ;;
        -h|--help) usage 0 ;;
        *) echo "未知参数: $1" >&2; usage 1 ;;
    esac
done

if [ -z "$APP" ] || [ ! -x "$APP" ]; then
    echo "错误：--app 必须指向可执行的 rtmp_monitor。" >&2
    exit 1
fi
if [ -z "$URL_TEMPLATE" ]; then
    echo "错误：--url-template 必须提供，%02d 会被替换为路数序号。" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
REPORT_ID="qualify-$(date +%Y%m%d-%H%M%S)"
REPORT_DIR="$OUTPUT_DIR/$REPORT_ID"
mkdir -p "$REPORT_DIR"

log() { echo "[qualify] $*"; }

read_temperature_millic() {
    local zone
    for zone in /sys/class/thermal/thermal_zone*/temp; do
        [ -r "$zone" ] || continue
        cat "$zone"
        return 0
    done
    echo ""
}

collect_board_facts() {
    {
        echo "boardId: $(cat /proc/device-tree/model 2>/dev/null | tr -d '\0' || echo unknown)"
        echo "hostname: $(hostname)"
        echo "kernel: $(uname -srvmo)"
        echo "imageVersion: $(cat /etc/os-release 2>/dev/null | grep '^PRETTY_NAME=' | cut -d= -f2- | tr -d '"')"
        echo "qpa: ${QT_QPA_PLATFORM:-default}"
        echo "cpuCores: $(nproc)"
        echo "memTotalKiB: $(awk '/MemTotal/ {print $2}' /proc/meminfo)"
        echo "outputResolution: $(cat /sys/class/graphics/fb0/virtual_size 2>/dev/null || echo unknown)"
        echo "testedAt: $(date -Is)"
        echo "reportId: $REPORT_ID"
    } > "$REPORT_DIR/device_facts.env"
}

stop_app() {
    if [ -n "${APP_PID:-}" ] && kill -0 "$APP_PID" 2>/dev/null; then
        kill "$APP_PID" 2>/dev/null || true
        for _ in 1 2 3 4 5 6 7 8 9 10; do
            kill -0 "$APP_PID" 2>/dev/null || break
            sleep 1
        done
        kill -9 "$APP_PID" 2>/dev/null || true
    fi
    APP_PID=""
}
trap stop_app EXIT

collect_board_facts
log "报告目录: $REPORT_DIR"

RECOMMENDED_MAX_STREAMS=0
FAILED_RUNG=""

for STREAM_COUNT in $LADDER; do
    log "===== 档位: ${STREAM_COUNT} 路 (预热 ${WARMUP_SECONDS}s, 采样 ${SAMPLE_SECONDS}s) ====="
    METRICS_FILE="$REPORT_DIR/metrics-${STREAM_COUNT}-streams.json"
    SAMPLES_FILE="$REPORT_DIR/samples-${STREAM_COUNT}-streams.csv"
    echo "epoch_s,cpu_percent,rss_kib,temp_millic" > "$SAMPLES_FILE"

    URL_ARGS=()
    for index in $(seq 1 "$STREAM_COUNT"); do
        # shellcheck disable=SC2059
        URL_ARGS+=(--url "$(printf "$URL_TEMPLATE" "$index")")
    done

    "$APP" --renderer "$RENDERER" \
        --metrics-file "$METRICS_FILE" \
        "${URL_ARGS[@]}" $EXTRA_ARGS &
    APP_PID=$!

    log "预热中（PID $APP_PID）..."
    sleep "$WARMUP_SECONDS"
    if ! kill -0 "$APP_PID" 2>/dev/null; then
        log "档位失败：应用在预热期间退出。"
        FAILED_RUNG="$STREAM_COUNT"
        break
    fi

    log "采样 ${SAMPLE_SECONDS} 秒..."
    SAMPLE_END=$(( $(date +%s) + SAMPLE_SECONDS ))
    RUNG_FAILED=""
    while [ "$(date +%s)" -lt "$SAMPLE_END" ]; do
        if ! kill -0 "$APP_PID" 2>/dev/null; then
            log "档位失败：应用在采样期间退出。"
            RUNG_FAILED="app_exited"
            break
        fi
        CPU_STAT=$(ps -o %cpu=,rss= -p "$APP_PID" 2>/dev/null || echo "0 0")
        TEMP=$(read_temperature_millic)
        echo "$(date +%s),${CPU_STAT%% *},${CPU_STAT##* },${TEMP}" >> "$SAMPLES_FILE"

        if [ -n "$TEMP" ] && [ "$TEMP" -ge "$MAX_TEMP_MILLIC" ] 2>/dev/null; then
            log "档位失败：温度 ${TEMP} millic 超过门槛 ${MAX_TEMP_MILLIC}。"
            RUNG_FAILED="temperature"
            break
        fi
        RSS_KIB=${CPU_STAT##* }
        if [ -n "$MAX_RSS_KIB" ] && [ "$RSS_KIB" -gt "$MAX_RSS_KIB" ] 2>/dev/null; then
            log "档位失败：RSS ${RSS_KIB} KiB 超过门槛 ${MAX_RSS_KIB}。"
            RUNG_FAILED="memory"
            break
        fi
        sleep 1
    done

    stop_app

    if [ -n "$RUNG_FAILED" ]; then
        FAILED_RUNG="$STREAM_COUNT"
        echo "failureReason: $RUNG_FAILED" > "$REPORT_DIR/failure-${STREAM_COUNT}.env"
        break
    fi

    # 汇总：平均/最大 CPU、最大 RSS、最大温度、frame age（取指标文件末尾样本）。
    SUMMARY=$(awk -F, 'NR>1 {
        cpu+=$2; if ($2>maxcpu) maxcpu=$2;
        if ($3>maxrss) maxrss=$3;
        if ($4!="") { if ($4>maxtemp) maxtemp=$4; n++ }
    } END {
        printf "avgCpu=%.1f\nmaxCpu=%.1f\nmaxRssKiB=%s\nmaxTempMillic=%s\n", cpu/(NR-1), maxcpu, maxrss, maxtemp
    }' "$SAMPLES_FILE")
    echo "$SUMMARY"
    MAX_CPU=$(echo "$SUMMARY" | awk -F= '/^maxCpu/ {print int($2)}')
    if [ "$MAX_CPU" -gt "$MAX_CPU_PERCENT" ]; then
        log "档位失败：最大 CPU ${MAX_CPU}% 超过门槛 ${MAX_CPU_PERCENT}%。"
        FAILED_RUNG="$STREAM_COUNT"
        echo "failureReason: cpu" > "$REPORT_DIR/failure-${STREAM_COUNT}.env"
        break
    fi

    RECOMMENDED_MAX_STREAMS="$STREAM_COUNT"
    log "档位 ${STREAM_COUNT} 路通过。"
done

{
    echo "# 设备档案（仅声明本板实测通过的内容，不构成通用 ARM 能力结论）"
    cat "$REPORT_DIR/device_facts.env"
    echo "qualifiedBackend: $(grep -ho '"activeBackend": *"[^"]*"' "$REPORT_DIR"/metrics-*.json 2>/dev/null | tail -1 | sed 's/.*: *"//; s/"//' || echo unknown)"
    echo "testedStreamProfile: urlTemplate=$URL_TEMPLATE renderer=$RENDERER ladder=($LADDER)"
    echo "recommendedMaxStreams: $RECOMMENDED_MAX_STREAMS"
    if [ -n "$FAILED_RUNG" ]; then
        echo "firstFailedRung: $FAILED_RUNG"
    fi
} | tee "$REPORT_DIR/device_profile.txt"

log "完成。recommendedMaxStreams=$RECOMMENDED_MAX_STREAMS"
[ -n "$FAILED_RUNG" ] && exit 2 || exit 0
