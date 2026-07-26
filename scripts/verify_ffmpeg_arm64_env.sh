#!/usr/bin/env bash

set -euo pipefail

readonly project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly sysroot="${ARM64_SYSROOT:-/opt/rtmp-monitor/sysroots/jammy-arm64}"
readonly pkgconfig_dir="${sysroot}/usr/local/lib/aarch64-linux-gnu/pkgconfig:${sysroot}/usr/local/share/pkgconfig"
readonly smoke_source="${project_root}/scripts/ffmpeg_smoke/ffmpeg_environment_smoke.c"
readonly smoke_directory="${project_root}/out/ffmpeg-smoke-arm64"
readonly smoke_binary="${smoke_directory}/ffmpeg_environment_smoke"

export PKG_CONFIG_SYSROOT_DIR="${sysroot}"
export PKG_CONFIG_LIBDIR="${pkgconfig_dir}"

for package_name in libavformat libavcodec libavutil libswscale; do
    pkg-config --exists "${package_name}"
    printf '%s ABI version: %s\n' \
        "${package_name}" "$(pkg-config --modversion "${package_name}")"
done

for disabled_library in libavdevice libavfilter libswresample; do
    if pkg-config --exists "${disabled_library}"; then
        echo "Unexpected FFmpeg library is installed: ${disabled_library}" >&2
        exit 1
    fi
done

install -d -m 0755 "${smoke_directory}"
aarch64-linux-gnu-gcc "${smoke_source}" -o "${smoke_binary}" \
    $(pkg-config --cflags --libs libavformat libavcodec libavutil libswscale)

file "${smoke_binary}" | grep -q 'ARM aarch64'
readelf -h "${smoke_binary}" | grep -q 'Machine:.*AArch64'
readelf -d "${smoke_binary}" | grep 'NEEDED'

qemu-aarch64-static -L "${sysroot}" \
    -E LD_LIBRARY_PATH=/usr/local/lib/aarch64-linux-gnu \
    "${smoke_binary}"
