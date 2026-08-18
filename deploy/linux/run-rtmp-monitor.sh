#!/usr/bin/env bash

set -euo pipefail

package_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="${package_root}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export QT_PLUGIN_PATH="${package_root}/plugins"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-linuxfb}"

exec "${package_root}/bin/rtmp_monitor" "$@"
