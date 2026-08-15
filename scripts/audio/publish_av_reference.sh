#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 --url rtmp://host/app/stream [--video /dev/video0] [--audio hw:1,0] [--size 1280x720] [--fps 30]"
}

video_device=/dev/video0
audio_device=default
video_size=1280x720
frame_rate=30
rtmp_url=

while (($#)); do
    case "$1" in
        --url) rtmp_url=${2:-}; shift 2 ;;
        --video) video_device=${2:-}; shift 2 ;;
        --audio) audio_device=${2:-}; shift 2 ;;
        --size) video_size=${2:-}; shift 2 ;;
        --fps) frame_rate=${2:-}; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ ! ${rtmp_url} =~ ^rtmp://[^/]+/.+ ]] ||
   [[ ! -c ${video_device} ]] ||
   ! command -v ffmpeg >/dev/null 2>&1 ||
   ! command -v arecord >/dev/null 2>&1; then
    echo "Invalid RTMP URL, missing V4L2 device, ffmpeg, or ALSA tools." >&2
    exit 2
fi

if ! arecord -L | grep -Fqx "${audio_device}"; then
    echo "ALSA capture device is not listed: ${audio_device}" >&2
    exit 2
fi
if ! ffmpeg -hide_banner -encoders 2>/dev/null | grep -Eq '[[:space:]]libx264[[:space:]]'; then
    echo "FFmpeg libx264 encoder is unavailable." >&2
    exit 2
fi
if ! ffmpeg -hide_banner -encoders 2>/dev/null | grep -Eq '[[:space:]]aac[[:space:]]'; then
    echo "FFmpeg AAC encoder is unavailable." >&2
    exit 2
fi

exec ffmpeg -hide_banner -nostdin -loglevel warning \
    -fflags nobuffer \
    -f v4l2 -thread_queue_size 64 -video_size "${video_size}" \
    -framerate "${frame_rate}" -i "${video_device}" \
    -f alsa -thread_queue_size 32 -sample_rate 48000 -channels 1 \
    -i "${audio_device}" \
    -map 0:v:0 -map 1:a:0 \
    -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p \
    -g "${frame_rate}" -keyint_min "${frame_rate}" -bf 0 -sc_threshold 0 \
    -c:a aac -profile:a aac_low -ar 48000 -ac 1 -b:a 64k \
    -flags +global_header+low_delay -flush_packets 1 \
    -f flv "${rtmp_url}"
