#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source_root="$(cd "${script_dir}/.." && pwd)"
build_dir=""
output_dir=""
sysroot="/opt/rtmp-monitor/sysroots/jammy-arm64"
ffmpeg_source_dir="/opt/rtmp-monitor/ffmpeg-arm64/sources/ffmpeg-8.1.2"
version=""

usage()
{
    cat <<'EOF'
Usage: package_linux_arm64.sh --build-dir PATH --output-dir PATH --version VERSION
                              [--sysroot PATH] [--ffmpeg-source-dir PATH]

Creates a Linux ARM64 RASTER Engineering Preview directory and .tar.gz archive.
The build must be Release, Linux/aarch64, RASTER, and BUILD_TESTING=OFF.
Existing output, archive, or audit directories are never overwritten.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) build_dir=${2:?missing value for --build-dir}; shift 2 ;;
        --output-dir) output_dir=${2:?missing value for --output-dir}; shift 2 ;;
        --sysroot) sysroot=${2:?missing value for --sysroot}; shift 2 ;;
        --ffmpeg-source-dir) ffmpeg_source_dir=${2:?missing value for --ffmpeg-source-dir}; shift 2 ;;
        --version) version=${2:?missing value for --version}; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[[ -n ${build_dir} && -n ${output_dir} && -n ${version} ]] || {
    usage >&2
    exit 2
}
[[ ${version} =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$ ]] || {
    echo "Invalid version: ${version}" >&2
    exit 2
}

build_dir="$(realpath -m "${build_dir}")"
output_dir="$(realpath -m "${output_dir}")"
sysroot="$(realpath -m "${sysroot}")"
ffmpeg_source_dir="$(realpath -m "${ffmpeg_source_dir}")"
stage_name="RtmpMonitor-${version}-linux-arm64-raster"
package_root="$(dirname "${output_dir}")"
archive_path="${package_root}/${stage_name}.tar.gz"
audit_dir="${package_root}/audit-${stage_name}"

[[ $(basename "${output_dir}") == "${stage_name}" ]] || {
    echo "Output directory leaf must be ${stage_name}" >&2
    exit 2
}
for protected in "${output_dir}" "${archive_path}" "${audit_dir}"; do
    [[ ! -e ${protected} ]] || {
        echo "Packaging output already exists; refusing to overwrite: ${protected}" >&2
        exit 2
    }
done

cache_path="${build_dir}/CMakeCache.txt"
application="${build_dir}/rtmp_monitor"
for required in "${cache_path}" "${application}" "${sysroot}" \
    "${ffmpeg_source_dir}/LICENSE.md" "${ffmpeg_source_dir}/COPYING.LGPLv2.1"; do
    [[ -e ${required} ]] || { echo "Required input is missing: ${required}" >&2; exit 1; }
done
for tool in cmake file aarch64-linux-gnu-readelf tar realpath install sort; do
    command -v "${tool}" >/dev/null || { echo "Required tool is missing: ${tool}" >&2; exit 1; }
done

cache_value()
{
    local key=$1
    sed -n -E "s/^${key}(:[^=]+)?=(.*)$/\\2/p" "${cache_path}" | head -1
}

[[ $(cache_value CMAKE_BUILD_TYPE) == Release ]] || {
    echo "Build is not Release." >&2
    exit 1
}
[[ $(cache_value RTMP_MONITOR_LINUX_RENDER_MODE) == RASTER ]] || {
    echo "Build render mode is not RASTER." >&2
    exit 1
}
[[ $(cache_value BUILD_TESTING) == OFF ]] || {
    echo "Release packaging build must use BUILD_TESTING=OFF." >&2
    exit 1
}

file "${application}" | grep -q 'ELF 64-bit.*ARM aarch64'
aarch64-linux-gnu-readelf -h "${application}" | grep -q 'Machine:.*AArch64'

mkdir -p "${package_root}" "${audit_dir}"
cmake --install "${build_dir}" --prefix "${output_dir}"
mkdir -p \
    "${output_dir}/lib" \
    "${output_dir}/plugins/platforms" \
    "${output_dir}/plugins/generic" \
    "${output_dir}/plugins/imageformats" \
    "${output_dir}/plugins/platforminputcontexts" \
    "${output_dir}/plugins/tls" \
    "${output_dir}/config" \
    "${output_dir}/licenses/qt" \
    "${output_dir}/licenses/ffmpeg" \
    "${output_dir}/licenses/paho"

qt_plugins="${sysroot}/usr/lib/aarch64-linux-gnu/qt6/plugins"
declare -a plugin_relatives=(
    platforms/libqlinuxfb.so
    platforms/libqminimal.so
    platforms/libqoffscreen.so
    generic/libqevdevkeyboardplugin.so
    generic/libqevdevmouseplugin.so
    generic/libqevdevtouchplugin.so
    imageformats/libqgif.so
    imageformats/libqico.so
    imageformats/libqjpeg.so
    platforminputcontexts/libcomposeplatforminputcontextplugin.so
    tls/libqcertonlybackend.so
)
declare -a dependency_queue=("${output_dir}/bin/rtmp_monitor")
for relative in "${plugin_relatives[@]}"; do
    source="${qt_plugins}/${relative}"
    destination="${output_dir}/plugins/${relative}"
    [[ -f ${source} ]] || { echo "Required Qt plugin is missing: ${source}" >&2; exit 1; }
    install -Dm755 "${source}" "${destination}"
    dependency_queue+=("${destination}")
done

install -Dm644 "${source_root}/deploy/srs/media-server.example.ini" \
    "${output_dir}/config/media-server.example.ini"
install -Dm644 "${source_root}/deploy/linux/qt.conf" "${output_dir}/bin/qt.conf"
install -Dm755 "${source_root}/deploy/linux/run-rtmp-monitor.sh" \
    "${output_dir}/run-rtmp-monitor.sh"
install -Dm644 "${source_root}/LICENSE" "${output_dir}/LICENSE"
install -Dm644 "${source_root}/README_LINUX_ARM64_TEST_PACKAGE.txt" \
    "${output_dir}/README_LINUX_ARM64_TEST_PACKAGE.txt"
install -Dm644 "${source_root}/THIRD_PARTY_NOTICES_LINUX_ARM64" \
    "${output_dir}/THIRD_PARTY_NOTICES_LINUX_ARM64"

install -Dm644 "${sysroot}/usr/share/doc/libqt6core6/copyright" \
    "${output_dir}/licenses/qt/ubuntu-copyright"
install -Dm644 "${sysroot}/usr/share/common-licenses/LGPL-3" \
    "${output_dir}/licenses/qt/LGPL-3"
install -Dm644 "${ffmpeg_source_dir}/LICENSE.md" \
    "${output_dir}/licenses/ffmpeg/LICENSE.md"
install -Dm644 "${ffmpeg_source_dir}/COPYING.LGPLv2.1" \
    "${output_dir}/licenses/ffmpeg/COPYING.LGPLv2.1"
install -Dm644 "${sysroot}/usr/local/share/doc/Eclipse Paho C/epl-v20" \
    "${output_dir}/licenses/paho/epl-v20"
install -Dm644 "${sysroot}/usr/local/share/doc/Eclipse Paho C/edl-v10" \
    "${output_dir}/licenses/paho/edl-v10"
install -Dm644 "${sysroot}/usr/local/share/doc/Eclipse Paho C/notice.html" \
    "${output_dir}/licenses/paho/notice.html"

resolve_soname()
{
    local soname=$1 directory
    for directory in \
        "${sysroot}/usr/local/lib/aarch64-linux-gnu" \
        "${sysroot}/usr/lib/aarch64-linux-gnu" \
        "${sysroot}/lib/aarch64-linux-gnu" \
        "${sysroot}/usr/lib" \
        "${sysroot}/lib"; do
        if [[ -e ${directory}/${soname} ]]; then
            realpath "${directory}/${soname}"
            return 0
        fi
    done
    return 1
}

is_glibc_runtime()
{
    case "$1" in
        ld-linux-aarch64.so.1|libc.so.6|libm.so.6|libpthread.so.0|libdl.so.2|\
        librt.so.1|libresolv.so.2|libutil.so.1|libanl.so.1|libBrokenLocale.so.1)
            return 0 ;;
        *) return 1 ;;
    esac
}

is_bundled_family()
{
    case "$1" in
        libQt6*.so.6|libav*.so.*|libsw*.so.*|libpaho-mqtt3a.so.1)
            return 0 ;;
        *) return 1 ;;
    esac
}

declare -A visited=()
declare -A bundled=()
declare -A system_dependencies=()
queue_index=0
while (( queue_index < ${#dependency_queue[@]} )); do
    current=${dependency_queue[queue_index++]}
    while IFS= read -r soname; do
        [[ -n ${soname} ]] || continue
        if is_glibc_runtime "${soname}"; then
            system_dependencies["${soname}"]=1
            continue
        fi
        [[ -z ${visited[${soname}]+x} ]] || continue
        visited["${soname}"]=1
        resolved="$(resolve_soname "${soname}")" || {
            echo "Unable to resolve ARM64 runtime dependency: ${soname}" >&2
            exit 1
        }
        file "${resolved}" | grep -q 'ELF 64-bit.*ARM aarch64' || {
            echo "Resolved dependency is not AArch64: ${resolved}" >&2
            exit 1
        }
        if is_bundled_family "${soname}"; then
            install -m755 "${resolved}" "${output_dir}/lib/${soname}"
            bundled["${soname}"]=1
            dependency_queue+=("${output_dir}/lib/${soname}")
        else
            system_dependencies["${soname}"]=1
            dependency_queue+=("${resolved}")
        fi
    done < <(aarch64-linux-gnu-readelf -d "${current}" 2>/dev/null |
        sed -n -E 's/.*Shared library: \[([^]]+)\].*/\1/p')
done

{
    echo "RtmpMonitor ${version}"
    echo "gitCommit=$(git -C "${source_root}" rev-parse HEAD)"
    echo "target=Linux/aarch64"
    echo "buildType=Release"
    echo "renderMode=RASTER"
    echo "baseline=Ubuntu 22.04 Jammy ARM64 sysroot"
    echo "contentIntegrityVerification=not_performed"
} > "${output_dir}/VERSION"

printf '%s\n' "${!bundled[@]}" | sort > "${output_dir}/BUNDLED_RUNTIME_LIBRARIES.txt"
printf '%s\n' "${!system_dependencies[@]}" | sort > \
    "${output_dir}/SYSTEM_RUNTIME_DEPENDENCIES.txt"

while IFS= read -r elf; do
    description="$(file "${elf}")"
    if grep -q 'ELF ' <<<"${description}"; then
        grep -q 'ELF 64-bit.*ARM aarch64' <<<"${description}" || {
            echo "Package contains a non-AArch64 ELF: ${elf}" >&2
            exit 1
        }
    fi
done < <(find "${output_dir}/bin" "${output_dir}/lib" "${output_dir}/plugins" \
    -type f -print | sort)

aarch64-linux-gnu-readelf -h "${output_dir}/bin/rtmp_monitor" > \
    "${audit_dir}/application-elf-header.txt"
aarch64-linux-gnu-readelf -d "${output_dir}/bin/rtmp_monitor" > \
    "${audit_dir}/application-dynamic-section.txt"
find "${output_dir}" -type f -printf '%P\n' | sort > "${audit_dir}/package-files.txt"

if find "${output_dir}" -type f \( -name '*.pdb' -o -name '*.debug' -o -name '*.py' \
    -o -name '*.cpp' -o -name '*.h' -o -name '*.cmake' \) -print -quit | grep -q .; then
    echo "Package contains forbidden development files." >&2
    exit 1
fi

tar --sort=name --owner=0 --group=0 --numeric-owner \
    -C "${package_root}" -czf "${archive_path}" "${stage_name}"
tar -tzf "${archive_path}" > "${audit_dir}/archive-files.txt"

echo "Linux ARM64 package created: ${archive_path}"
echo "Stage directory: ${output_dir}"
echo "Audit directory: ${audit_dir}"
echo "No content digest was generated or compared."
