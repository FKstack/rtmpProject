#!/bin/bash
# RtmpMonitor Phase 2 - build SRS 6.0.184 for ARM Linux.
#
# Modes:
#   native : run ON the target ARM device (preferred, official recommendation)
#   cross  : run on an x86_64 host with an aarch64 cross toolchain. The result
#            only proves an AArch64 ELF was produced; it MUST be re-verified on
#            the real device (see docs/versions/rtmp-v1/architecture/srs_server_integration_plan.md 8.3).
#
# Usage:
#   build_srs_arm64.sh --source-dir <srs-repo> [--prefix /opt/rtmp-monitor/srs-6.0.184]
#                      [--config <repo>/deploy/srs/conf/srs-minimal.conf]
#                      [--mode native|cross] [--cross-prefix aarch64-linux-gnu-]
#                      [--jobs N] [--no-install]
set -euo pipefail

SOURCE_DIR=""
PREFIX="/opt/rtmp-monitor/srs-6.0.184"
CONFIG=""
MODE="native"
CROSS_PREFIX="aarch64-linux-gnu-"
JOBS="$(nproc)"
DO_INSTALL=1

while [ $# -gt 0 ]; do
    case "$1" in
        --source-dir)   SOURCE_DIR="$2"; shift 2 ;;
        --prefix)       PREFIX="$2"; shift 2 ;;
        --config)       CONFIG="$2"; shift 2 ;;
        --mode)         MODE="$2"; shift 2 ;;
        --cross-prefix) CROSS_PREFIX="$2"; shift 2 ;;
        --jobs)         JOBS="$2"; shift 2 ;;
        --no-install)   DO_INSTALL=0; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [ -z "$SOURCE_DIR" ] || [ ! -d "$SOURCE_DIR/trunk" ]; then
    echo "ERROR: --source-dir must point to the SRS repository (containing trunk/)" >&2
    exit 2
fi
case "$MODE" in native|cross) ;; *) echo "ERROR: --mode must be native or cross" >&2; exit 2 ;; esac

TRUNK="$SOURCE_DIR/trunk"

# Dependency check (official Dockerfile.builds list + tclsh for the vendored
# SRT configure). On a device use: sudo apt-get install -y gcc g++ make patch
# unzip perl git tclsh
missing=""
for tool in gcc g++ make patch unzip perl git tclsh; do
    command -v "$tool" >/dev/null || missing="$missing $tool"
done
if [ -n "$missing" ]; then
    echo "ERROR: missing build dependencies:$missing" >&2
    echo "Install on the device: sudo apt-get install -y gcc g++ make patch unzip perl git tclsh" >&2
    echo "No-sudo hosts may unpack debs user-side and fix the vendored tclsh" >&2
    echo "shebang; see docs/versions/rtmp-v1/weeks/week7 Phase 1 notes." >&2
    exit 1
fi

echo "=== build_srs_arm64: mode=$MODE"
echo "    source       : $SOURCE_DIR"
echo "    prefix       : $PREFIX"
echo "    jobs         : $JOBS"
uname -a
uname -m
(getconf GNU_LIBC_VERSION || true)

if [ "$MODE" = "cross" ]; then
    command -v "${CROSS_PREFIX}gcc" >/dev/null || {
        echo "ERROR: cross compiler ${CROSS_PREFIX}gcc not found in PATH" >&2
        exit 1
    }
    cd "$TRUNK"
    ./configure \
        --cross-build \
        --cross-prefix="$CROSS_PREFIX" \
        --prefix="$PREFIX"
    make -j"$JOBS"
    echo "=== cross build artifact checks (ELF only; NOT a device run) ==="
    file objs/srs
    "${CROSS_PREFIX}readelf" -h objs/srs | grep -E 'Class|Machine'
    "${CROSS_PREFIX}readelf" -d objs/srs | grep NEEDED || true
    echo "NOTE: cross build proves only that an AArch64 ELF links."
    echo "      Re-verify on the target device: file/ldd/srs -v (plan 8.3)."
    exit 0
fi

# native mode, on the device
cd "$TRUNK"
./configure --prefix="$PREFIX"
make -j"$JOBS"

if [ "$DO_INSTALL" -eq 1 ]; then
    if [ -w "$(dirname "$PREFIX")" ] || [ -w "$PREFIX" ]; then
        make install
    else
        echo "prefix $PREFIX not writable; using sudo make install"
        sudo make install
    fi
    if [ -n "$CONFIG" ]; then
        if [ -w "$PREFIX" ]; then
            install -m 0644 "$CONFIG" "$PREFIX/conf/rtmp-monitor.conf"
        else
            sudo install -m 0644 "$CONFIG" "$PREFIX/conf/rtmp-monitor.conf"
        fi
        echo "installed config: $PREFIX/conf/rtmp-monitor.conf"
    fi
fi

SRS_BIN="$PREFIX/objs/srs"
[ -x "$SRS_BIN" ] || SRS_BIN="$TRUNK/objs/srs"
echo "=== native build artifact checks ==="
"$SRS_BIN" -v 2>&1 | head -5
file "$SRS_BIN"
ldd "$SRS_BIN" || true
echo "BUILD_OK"
