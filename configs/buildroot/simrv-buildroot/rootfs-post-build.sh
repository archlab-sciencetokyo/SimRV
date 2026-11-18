#!/bin/sh
# Buildroot requires a host-side script hook; perform the work in the C++ helper.
set -eu

helper=${SIMRV_BUILDROOT_POST_BUILD:-}
if [ -z "$helper" ]; then
    helper=$(command -v simrv-linux-builder-post-build || true)
fi

if [ -z "$helper" ]; then
    script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
    project_root=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
    for candidate in \
        "$project_root/build/rv64-release/simrv-linux-builder-post-build" \
        "$project_root/build/rv64-dev/simrv-linux-builder-post-build" \
        "$project_root/build/rv32-release/simrv-linux-builder-post-build" \
        "$project_root/build/rv32-dev/simrv-linux-builder-post-build"; do
        if [ -x "$candidate" ]; then
            helper=$candidate
            break
        fi
    done
fi

if [ -z "$helper" ] || [ ! -x "$helper" ]; then
    echo "simrv-linux-builder-post-build is not available; install simrv-tools, set SIMRV_BUILDROOT_POST_BUILD, or build with SIMRV_ENABLE_LINUX_TOOLS=ON." >&2
    exit 127
fi

exec "$helper" "$@"
