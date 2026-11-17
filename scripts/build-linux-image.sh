#!/usr/bin/env bash
# Compatibility entry point. Linux image orchestration lives in native C++.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname -- "$SCRIPT_DIR")"

if [[ -n "${SIMRV_LINUX_BUILDER:-}" ]]; then
    BUILDER="$SIMRV_LINUX_BUILDER"
else
    BUILDER=""
    for candidate in \
        "$ROOT_DIR/build/rv64-dev/simrv-linux-builder" \
        "$ROOT_DIR/build/rv64-release/simrv-linux-builder" \
        "$ROOT_DIR/build/rv32-dev/simrv-linux-builder" \
        "$ROOT_DIR/build/rv32-release/simrv-linux-builder"; do
        if [[ -x "$candidate" ]]; then
            BUILDER="$candidate"
            break
        fi
    done
fi

if [[ -z "$BUILDER" || ! -x "$BUILDER" ]]; then
    echo "simrv-linux-builder is not built. Configure with -DSIMRV_ENABLE_LINUX_TOOLS=ON." >&2
    exit 127
fi

exec "$BUILDER" "$@"
