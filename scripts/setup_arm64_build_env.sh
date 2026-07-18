#!/usr/bin/env bash

set -euo pipefail

readonly proxy_url="http://127.0.0.1:7890"
readonly arm64_source_file="/etc/apt/sources.list.d/rtmp-monitor-arm64.list"
readonly arm64_sysroot="/opt/rtmp-monitor/sysroots/jammy-arm64"
readonly apt_backup_directory="/etc/apt/rtmp-monitor-backups"

if [[ ${EUID} -ne 0 ]]; then
    echo "请使用 root 运行该脚本。" >&2
    exit 1
fi

if [[ ! -r /etc/os-release ]]; then
    echo "无法读取 /etc/os-release。" >&2
    exit 1
fi

# shellcheck disable=SC1091
source /etc/os-release
if [[ ${ID:-} != "ubuntu" || ${VERSION_CODENAME:-} != "jammy" ]]; then
    echo "当前脚本只支持 Ubuntu 22.04 Jammy。" >&2
    exit 1
fi

configure_network()
{
    if curl --proxy "${proxy_url}" --connect-timeout 5 --max-time 15 \
        --fail --silent --show-error --head \
        http://archive.ubuntu.com/ubuntu/dists/jammy/InRelease >/dev/null; then
        export http_proxy="${proxy_url}"
        export https_proxy="${proxy_url}"
        export HTTP_PROXY="${proxy_url}"
        export HTTPS_PROXY="${proxy_url}"
        echo "使用 Windows 本机代理 ${proxy_url}。"
        return
    fi

    unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY
    if curl --connect-timeout 5 --max-time 15 --fail --silent --show-error \
        --head http://archive.ubuntu.com/ubuntu/dists/jammy/InRelease >/dev/null; then
        echo "本机代理不可用，已回退为直连。"
        return
    fi

    echo "代理和直连均无法访问 Ubuntu 软件源。" >&2
    exit 1
}

disable_proxy()
{
    unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY
}

update_package_indexes()
{
    if apt-get update; then
        return 0
    fi

    if [[ ${http_proxy:-} == "${proxy_url}" ]]; then
        echo "代理刷新软件索引失败，改用直连重试。"
        disable_proxy
        apt-get update
        return
    fi

    return 1
}

restrict_binary_source_to_amd64()
{
    local backup_file
    local source_file=$1

    [[ -f ${source_file} ]] || return 0
    [[ ${source_file} != "${arm64_source_file}" ]] || return 0

    backup_file="${apt_backup_directory}/$(basename "${source_file}").pre-rtmp-monitor"
    if [[ ! -f ${backup_file} ]]; then
        cp -a "${source_file}" "${backup_file}"
    fi

    # 新增 arm64 后，普通 Ubuntu 和 PPA 源必须只查询 amd64，避免错误请求 ARM 包。
    sed -i -E \
        -e '/^[[:space:]]*deb[[:space:]]+\[/ { /arch=/! s/^([[:space:]]*deb[[:space:]]+\[)/\1arch=amd64 /; }' \
        -e '/^[[:space:]]*deb[[:space:]]+[^[]/ { s/^([[:space:]]*deb)[[:space:]]+/\1 [arch=amd64] /; }' \
        "${source_file}"
}

configure_sources()
{
    local legacy_backup
    local source_file

    install -d -m 0755 "${apt_backup_directory}"
    shopt -s nullglob
    for legacy_backup in /etc/apt/sources.list.d/*.list.pre-rtmp-monitor; do
        mv -f "${legacy_backup}" "${apt_backup_directory}/$(basename "${legacy_backup}")"
    done
    shopt -u nullglob

    restrict_binary_source_to_amd64 /etc/apt/sources.list

    shopt -s nullglob
    for source_file in /etc/apt/sources.list.d/*.list; do
        restrict_binary_source_to_amd64 "${source_file}"
    done
    shopt -u nullglob

    install -d -m 0755 /etc/apt/sources.list.d
    printf '%s\n' \
        'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports jammy main restricted universe multiverse' \
        'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports jammy-updates main restricted universe multiverse' \
        'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports jammy-security main restricted universe multiverse' \
        'deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports jammy-backports main restricted universe multiverse' \
        >"${arm64_source_file}"

    dpkg --add-architecture arm64
}

multiarch_runtime_versions_match()
{
    local arm64_candidate
    local installed_version
    local package_name

    for package_name in libgcc-s1 libgomp1 libstdc++6; do
        installed_version=$(dpkg-query -W -f='${Version}' "${package_name}:amd64")
        arm64_candidate=$(apt-cache policy "${package_name}:arm64" |
            awk '/Candidate:/ { print $2; exit }')

        if [[ -z ${arm64_candidate} || ${arm64_candidate} == "(none)" || \
              ${installed_version} != "${arm64_candidate}" ]]; then
            echo "${package_name} 的 amd64/arm64 版本不一致，改用隔离 sysroot。"
            return 1
        fi
    done

    return 0
}

install_common_dependencies()
{
    apt-get install -y --no-install-recommends \
        gcc-aarch64-linux-gnu \
        g++-aarch64-linux-gnu \
        binutils-aarch64-linux-gnu \
        cmake \
        ninja-build \
        pkg-config \
        file \
        patchelf \
        qemu-user-static \
        binfmt-support \
        debootstrap
}

install_multiarch_qt()
{
    local libgcc_version
    local libgomp_version
    local libstdcxx_version

    libgcc_version=$(dpkg-query -W -f='${Version}' libgcc-s1:amd64)
    libgomp_version=$(dpkg-query -W -f='${Version}' libgomp1:amd64)
    libstdcxx_version=$(dpkg-query -W -f='${Version}' libstdc++6:amd64)

    apt-get install -y --no-install-recommends \
        "libgcc-s1:amd64=${libgcc_version}" \
        "libgcc-s1:arm64=${libgcc_version}" \
        "libgomp1:amd64=${libgomp_version}" \
        "libgomp1:arm64=${libgomp_version}" \
        "libstdc++6:amd64=${libstdcxx_version}" \
        "libstdc++6:arm64=${libstdcxx_version}" \
        gcc-aarch64-linux-gnu \
        g++-aarch64-linux-gnu \
        binutils-aarch64-linux-gnu \
        cmake \
        ninja-build \
        pkg-config \
        file \
        patchelf \
        qemu-user-static \
        binfmt-support \
        qt6-base-dev-tools:amd64 \
        qt6-base-dev:arm64 \
        libgl-dev:arm64 \
        libegl-dev:arm64 \
        libgles-dev:arm64
}

install_isolated_qt_sysroot()
{
    local -a network_environment=()

    apt-get install -y --no-install-recommends \
        qt6-base-dev:amd64 \
        qt6-base-dev-tools:amd64

    install -d -m 0755 "$(dirname "${arm64_sysroot}")"
    if [[ ! -r ${arm64_sysroot}/etc/os-release ]]; then
        if ! qemu-debootstrap --arch=arm64 --variant=minbase jammy \
            "${arm64_sysroot}" http://ports.ubuntu.com/ubuntu-ports; then
            echo "代理创建 sysroot 失败，改用直连续跑。"
            disable_proxy
            qemu-debootstrap --arch=arm64 --variant=minbase jammy \
                "${arm64_sysroot}" http://ports.ubuntu.com/ubuntu-ports
        fi
    fi

    printf '%s\n' \
        'deb http://ports.ubuntu.com/ubuntu-ports jammy main restricted universe multiverse' \
        'deb http://ports.ubuntu.com/ubuntu-ports jammy-updates main restricted universe multiverse' \
        'deb http://ports.ubuntu.com/ubuntu-ports jammy-security main restricted universe multiverse' \
        'deb http://ports.ubuntu.com/ubuntu-ports jammy-backports main restricted universe multiverse' \
        >"${arm64_sysroot}/etc/apt/sources.list"

    # 完整标记与关键头文件、链接入口同时存在时，避免每次运行都在 QEMU chroot
    # 中重新下载软件索引；任一文件缺失仍会进入安装流程并自动修复。
    if [[ -f ${arm64_sysroot}/.rtmp-monitor-ready && \
          -f ${arm64_sysroot}/usr/include/GL/gl.h && \
          -e ${arm64_sysroot}/usr/lib/aarch64-linux-gnu/libGL.so && \
          -f ${arm64_sysroot}/usr/lib/aarch64-linux-gnu/cmake/Qt6/Qt6Config.cmake ]]; then
        echo "隔离 ARM64 Qt sysroot 已就绪，跳过目标包重复安装。"
        return
    fi

    if [[ -n ${http_proxy:-} ]]; then
        network_environment+=("http_proxy=${http_proxy}")
        network_environment+=("https_proxy=${https_proxy}")
        network_environment+=("HTTP_PROXY=${HTTP_PROXY}")
        network_environment+=("HTTPS_PROXY=${HTTPS_PROXY}")
    fi

    if ! chroot "${arm64_sysroot}" /usr/bin/env "${network_environment[@]}" \
        DEBIAN_FRONTEND=noninteractive apt-get update; then
        echo "代理刷新 sysroot 软件索引失败，改用直连重试。"
        disable_proxy
        network_environment=()
        chroot "${arm64_sysroot}" /usr/bin/env \
            DEBIAN_FRONTEND=noninteractive apt-get update
    fi
    if ! chroot "${arm64_sysroot}" /usr/bin/env "${network_environment[@]}" \
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        qt6-base-dev libgl-dev libegl-dev libgles-dev; then
        echo "代理安装 ARM64 Qt 失败，改用直连续装。"
        disable_proxy
        network_environment=()
        chroot "${arm64_sysroot}" /usr/bin/env \
            DEBIAN_FRONTEND=noninteractive apt-get install -y --fix-missing \
            --no-install-recommends qt6-base-dev libgl-dev libegl-dev libgles-dev
    fi
    chroot "${arm64_sysroot}" apt-get clean
    rm -rf "${arm64_sysroot}/var/lib/apt/lists/"*
    touch "${arm64_sysroot}/.rtmp-monitor-ready"
}

install_dependencies()
{
    export DEBIAN_FRONTEND=noninteractive

    update_package_indexes
    install_common_dependencies

    if multiarch_runtime_versions_match && install_multiarch_qt; then
        echo "使用 Ubuntu multiarch Qt 6。"
    else
        install_isolated_qt_sysroot
        echo "使用隔离 ARM64 Qt 6 sysroot：${arm64_sysroot}。"
    fi

    apt-get clean
    rm -rf /var/lib/apt/lists/*
}

configure_network
configure_sources
install_dependencies

echo "ARM64 Qt 6 交叉编译环境安装完成。"
dpkg-query -W -f='${Package} ${Architecture} ${Version}\n' \
    g++-aarch64-linux-gnu qt6-base-dev-tools:amd64

if [[ -f ${arm64_sysroot}/.rtmp-monitor-ready ]]; then
    chroot "${arm64_sysroot}" dpkg-query -W \
        -f='${Package} ${Architecture} ${Version}\n' qt6-base-dev libgl-dev
    qt_target_library="${arm64_sysroot}/usr/lib/aarch64-linux-gnu/libQt6Widgets.so.6"
else
    dpkg-query -W -f='${Package} ${Architecture} ${Version}\n' qt6-base-dev:arm64
    qt_target_library="/usr/lib/aarch64-linux-gnu/libQt6Widgets.so.6"
fi

# 交叉构建只能执行宿主工具，链接阶段只能使用目标库，二者架构不得混淆。
for qt_host_tool in moc rcc uic; do
    file "/usr/lib/qt6/libexec/${qt_host_tool}" | grep -q 'x86-64'
done

file -L "${qt_target_library}" | grep -q 'ARM aarch64'
