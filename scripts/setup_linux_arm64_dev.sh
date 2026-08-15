#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_root="$(cd "${script_dir}/.." && pwd)"
action="check"
render_mode="both"
work_root="${RTMP_MONITOR_ARM64_WORK_ROOT:-/opt/rtmp-monitor}"
sysroot="${ARM64_SYSROOT:-}"
build_root=""
proxy_url="${RTMP_MONITOR_PROXY_URL:-}"

usage()
{
    cat <<'EOF'
Usage: setup_linux_arm64_dev.sh [options]

  --action check|install|build|test|all|self-test
  --render-mode raster|gles3|both
  --source-root PATH
  --work-root PATH
  --sysroot PATH
  --build-root PATH
  --proxy-url URL          Optional; used only by install.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --action)
            action=${2:?missing value for --action}
            shift 2
            ;;
        --render-mode)
            render_mode=${2:?missing value for --render-mode}
            shift 2
            ;;
        --source-root)
            source_root=$(realpath -m "${2:?missing value for --source-root}")
            shift 2
            ;;
        --work-root)
            work_root=$(realpath -m "${2:?missing value for --work-root}")
            shift 2
            ;;
        --sysroot)
            sysroot=$(realpath -m "${2:?missing value for --sysroot}")
            shift 2
            ;;
        --build-root)
            build_root=$(realpath -m "${2:?missing value for --build-root}")
            shift 2
            ;;
        --proxy-url)
            proxy_url=${2:?missing value for --proxy-url}
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

case "${action}" in
    check|install|build|test|all|self-test) ;;
    *) echo "Unsupported action: ${action}" >&2; exit 2 ;;
esac
case "${render_mode}" in
    raster|gles3|both) ;;
    *) echo "Unsupported render mode: ${render_mode}" >&2; exit 2 ;;
esac

[[ -n ${sysroot} ]] || sysroot="${work_root}/sysroots/jammy-arm64"
[[ -n ${build_root} ]] || build_root="${source_root}/out/build-linux-arm64"
ffmpeg_root="${work_root}/ffmpeg-arm64"
pkgconfig_dir="${sysroot}/usr/local/lib/aarch64-linux-gnu/pkgconfig:${sysroot}/usr/local/share/pkgconfig"

require_file()
{
    [[ -f $1 ]] || { echo "Required file is missing: $1" >&2; exit 1; }
}

require_tool()
{
    command -v "$1" >/dev/null || {
        echo "Required host tool is missing: $1" >&2
        exit 1
    }
}

environment_check()
{
    bash "${source_root}/scripts/setup_arm64_build_env.sh" \
        --action check --work-root "${work_root}" --sysroot "${sysroot}" \
        --ffmpeg-root "${ffmpeg_root}"
}

install_environment()
{
    if [[ ${EUID} -ne 0 ]]; then
        echo "Install requires root. Re-run this command with sudo and --action install." >&2
        exit 1
    fi
    local -a arguments=(
        --action install
        --work-root "${work_root}"
        --sysroot "${sysroot}"
        --ffmpeg-root "${ffmpeg_root}"
    )
    if [[ -n ${proxy_url} ]]; then
        arguments+=(--proxy-url "${proxy_url}")
    fi
    bash "${source_root}/scripts/setup_arm64_build_env.sh" "${arguments[@]}"
}

mode_list()
{
    case "${render_mode}" in
        raster) printf '%s\n' raster ;;
        gles3) printf '%s\n' gles3 ;;
        both) printf '%s\n' raster gles3 ;;
    esac
}

mode_preset()
{
    case "$1" in
        raster) printf '%s\n' Linux-ARM64-RASTER-Debug ;;
        gles3) printf '%s\n' Linux-ARM64-GLES3-Debug ;;
    esac
}

build_modes()
{
    local mode preset directory
    export PKG_CONFIG_SYSROOT_DIR="${sysroot}"
    export PKG_CONFIG_LIBDIR="${pkgconfig_dir}"
    while IFS= read -r mode; do
        preset=$(mode_preset "${mode}")
        directory="${build_root}/${mode}-debug"
        cmake --preset "${preset}" -S "${source_root}" -B "${directory}" \
            -DARM64_SYSROOT="${sysroot}" \
            -DQt6_DIR="${sysroot}/usr/lib/aarch64-linux-gnu/cmake/Qt6"
        cmake --build "${directory}" --parallel "$(nproc)"
    done < <(mode_list)
}

require_aarch64_elf()
{
    require_file "$1"
    file "$1" | grep -q 'ELF 64-bit.*ARM aarch64'
    aarch64-linux-gnu-readelf -h "$1" | grep -q 'Machine:.*AArch64'
}

verify_mode()
{
    local mode=$1
    local directory="${build_root}/${mode}-debug"
    local main_binary="${directory}/rtmp_monitor"
    local dependencies
    require_aarch64_elf "${main_binary}"
    dependencies=$(aarch64-linux-gnu-readelf -d "${main_binary}")
    if grep -Eqi 'mingw|windows|x86_64' <<<"${dependencies}"; then
        echo "Host or Windows dependency leaked into ${main_binary}." >&2
        exit 1
    fi
    if [[ ${mode} == raster ]]; then
        if grep -Eq 'libQt6OpenGL|libQt6OpenGLWidgets|libEGL|libGLES' \
            <<<"${dependencies}"; then
            echo "RASTER target links a forbidden OpenGL dependency." >&2
            exit 1
        fi
    else
        grep -q 'libQt6OpenGLWidgets.so' <<<"${dependencies}"
        grep -q 'libQt6OpenGL.so' <<<"${dependencies}"
        require_aarch64_elf "${directory}/rtmp_monitor_opengl_egl_smoke"
        require_aarch64_elf "${directory}/rtmp_monitor_qt_opengl_smoke"
    fi
}

run_qemu_logic_tests()
{
    local preferred_mode=gles3
    local directory binary
    if [[ ${render_mode} == raster ]]; then
        preferred_mode=raster
    else
        preferred_mode=gles3
    fi
    directory="${build_root}/${preferred_mode}-debug"
    for binary in \
        rtmp_monitor_embedded_gl_capabilities_test \
        rtmp_monitor_linux_rendering_policy_test \
        rtmp_monitor_video_render_core_test \
        rtmp_monitor_user_message_test; do
        require_aarch64_elf "${directory}/${binary}"
        qemu-aarch64-static -L "${sysroot}" \
            -E LD_LIBRARY_PATH=/usr/local/lib/aarch64-linux-gnu:/usr/lib/aarch64-linux-gnu \
            -E QT_QPA_PLATFORM=offscreen \
            "${directory}/${binary}" -o -,txt
    done
}

test_modes()
{
    local mode
    ARM64_SYSROOT="${sysroot}" \
    ARM64_BUILD_ROOT="${build_root}/ffmpeg-smoke" \
        bash "${source_root}/scripts/verify_ffmpeg_arm64_env.sh"
    while IFS= read -r mode; do
        verify_mode "${mode}"
    done < <(mode_list)
    run_qemu_logic_tests
    echo "Linux ARM64 ${render_mode} cross-development gate passed."
    echo "Real QPA/GPU/video/device qualification remains Phase 5."
}

self_test()
{
    for required in \
        CMakeLists.txt CMakePresets.json \
        scripts/setup_arm64_build_env.sh \
        scripts/verify_ffmpeg_arm64_env.sh; do
        require_file "${source_root}/${required}"
    done
    grep -q 'Linux-ARM64-RASTER-Debug' "${source_root}/CMakePresets.json"
    grep -q 'Linux-ARM64-GLES3-Debug' "${source_root}/CMakePresets.json"
    if grep -q '127.0.0.1:7890' "${source_root}/scripts/setup_arm64_build_env.sh"; then
        echo "ARM64 installer still contains a machine-local proxy default." >&2
        exit 1
    fi
    echo "Linux ARM64 development setup self-test passed."
}

if [[ ${action} == self-test ]]; then
    self_test
    exit 0
fi

for tool in cmake ninja aarch64-linux-gnu-g++ \
    aarch64-linux-gnu-readelf file pkg-config qemu-aarch64-static; do
    require_tool "${tool}"
done
require_file "${source_root}/CMakePresets.json"

case "${action}" in
    check)
        environment_check
        ;;
    install)
        install_environment
        ;;
    build)
        environment_check
        build_modes
        ;;
    test)
        environment_check
        test_modes
        ;;
    all)
        environment_check
        build_modes
        test_modes
        ;;
esac
