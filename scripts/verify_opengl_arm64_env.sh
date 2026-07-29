#!/usr/bin/env bash

set -euo pipefail

readonly project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly sysroot="${ARM64_SYSROOT:-/opt/rtmp-monitor/sysroots/jammy-arm64}"
readonly build_directory="${project_root}/out/build-linux-arm64/debug"

require_file()
{
    local path=$1
    [[ -f ${path} ]] || {
        echo "Required file is missing: ${path}" >&2
        exit 1
    }
}

require_library()
{
    local path=$1
    [[ -e ${path} ]] || {
        echo "Required library is missing: ${path}" >&2
        exit 1
    }
    file -L "${path}" | grep -q 'ARM aarch64'
}

require_aarch64_elf()
{
    local path=$1
    require_file "${path}"
    file "${path}" | grep -q 'ELF 64-bit.*ARM aarch64'
    aarch64-linux-gnu-readelf -h "${path}" |
        grep -q 'Machine:.*AArch64'
}

for tool in aarch64-linux-gnu-g++ aarch64-linux-gnu-readelf \
    cmake ninja file; do
    command -v "${tool}" >/dev/null || {
        echo "Required host tool is missing: ${tool}" >&2
        exit 1
    }
done

for header in \
    "${sysroot}/usr/include/GL/gl.h" \
    "${sysroot}/usr/include/EGL/egl.h" \
    "${sysroot}/usr/include/GLES2/gl2.h"; do
    require_file "${header}"
done

for qt_module in Qt6 Qt6OpenGL Qt6OpenGLWidgets; do
    require_file \
        "${sysroot}/usr/lib/aarch64-linux-gnu/cmake/${qt_module}/${qt_module}Config.cmake"
done

for library in \
    "${sysroot}/usr/lib/aarch64-linux-gnu/libGL.so" \
    "${sysroot}/usr/lib/aarch64-linux-gnu/libEGL.so" \
    "${sysroot}/usr/lib/aarch64-linux-gnu/libGLESv2.so" \
    "${sysroot}/usr/lib/aarch64-linux-gnu/libQt6OpenGL.so.6" \
    "${sysroot}/usr/lib/aarch64-linux-gnu/libQt6OpenGLWidgets.so.6"; do
    require_library "${library}"
done

cd "${project_root}"
cmake --preset Linux-ARM64-Debug \
    -DBUILD_TESTING=ON \
    -DRTMP_MONITOR_BUILD_OPENGL_PROTOTYPE=ON
cmake --build --preset Linux-ARM64-Debug --parallel "$(nproc)"

readonly main_binary="${build_directory}/rtmp_monitor"
readonly egl_smoke_binary="${build_directory}/rtmp_monitor_opengl_egl_smoke"
readonly qt_smoke_binary="${build_directory}/rtmp_monitor_qt_opengl_smoke"

readonly arm64_binaries=(
    "${main_binary}"
    "${build_directory}/rtmp_monitor_ui_smoke_test"
    "${build_directory}/rtmp_monitor_dynamic_grid_test"
    "${build_directory}/rtmp_monitor_ffmpeg_player_test"
    "${build_directory}/rtmp_monitor_multi_stream_test"
    "${build_directory}/rtmp_monitor_logging_test"
    "${build_directory}/rtmp_monitor_user_message_test"
    "${build_directory}/rtmp_monitor_connection_controller_test"
    "${build_directory}/rtmp_monitor_log_panel_test"
    "${egl_smoke_binary}"
    "${qt_smoke_binary}"
)

for binary in "${arm64_binaries[@]}"; do
    require_aarch64_elf "${binary}"
    if aarch64-linux-gnu-readelf -d "${binary}" |
        grep -Eqi 'mingw|windows|x86_64'; then
        echo "Host or Windows dependency leaked into ${binary}." >&2
        exit 1
    fi
done

egl_dependencies="$(aarch64-linux-gnu-readelf -d "${egl_smoke_binary}")"
grep -q 'libEGL.so' <<<"${egl_dependencies}"
grep -q 'libGLESv2.so' <<<"${egl_dependencies}"

qt_dependencies="$(aarch64-linux-gnu-readelf -d "${qt_smoke_binary}")"
grep -q 'libQt6OpenGLWidgets.so' <<<"${qt_dependencies}"
grep -q 'libQt6OpenGL.so' <<<"${qt_dependencies}"

echo "ARM64 OpenGL cross-build gate passed."
echo "The generated binaries were not executed in WSL2."
echo "Run these commands on the target ARM64 box:"
echo "  QT_QPA_PLATFORM=eglfs ./rtmp_monitor_qt_opengl_smoke"
echo "  ./rtmp_monitor_opengl_egl_smoke"
