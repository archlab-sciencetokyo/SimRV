#!/bin/sh
# Create a disposable Linux guest disk without modifying the golden image.
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 GOLDEN_IMAGE RUN_IMAGE" >&2
    exit 2
fi

golden=$1
run_image=$2

if [ ! -f "$golden" ]; then
    echo "golden image not found: $golden" >&2
    exit 2
fi
if [ -e "$run_image" ]; then
    echo "refusing to overwrite existing run image: $run_image" >&2
    exit 2
fi

mkdir -p "$(dirname "$run_image")"
cp --reflink=auto --sparse=always -- "$golden" "$run_image"

# Validate the disposable copy without changing it. This catches a damaged
# golden image before an emulator run can make the diagnosis ambiguous.
if command -v e2fsck >/dev/null 2>&1; then
    e2fsck -fn "$run_image" >/dev/null
fi

echo "Linux run image ready: $run_image"
