#!/usr/bin/env bash
# Build an RV98-compatible Doom rootfs and compose it into a derived full image.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
VERSIONS_FILE="${SIMRV_LINUX_VERSION_FILE:-$SCRIPT_DIR/linux-versions.conf}"
BUILDROOT_VER="$(awk -F= '$1 == "buildroot" { print $2; exit }' "$VERSIONS_FILE")"
BUILDROOT_SOURCE="${SIMRV_BUILDROOT_SOURCE_DIR:-$ROOT_DIR/linux-build/buildroot-$BUILDROOT_VER}"
BUILDROOT_OUTPUT="${SIMRV_RV98_BUILDROOT_OUTPUT:-$ROOT_DIR/linux-build/buildroot-output-rv98-rv32}"
IMAGES_DIR="${SIMRV_RV98_DOOM_OUTPUT:-$ROOT_DIR/linux-images/rv98/rv32/doom}"
SOURCE_IMAGE="${SIMRV_RV98_SOURCE_IMAGE:-$ROOT_DIR/linux-images/rv98/sys_full.img}"
SOURCE_DTB="${SIMRV_RV98_DTB:-$ROOT_DIR/linux-images/rv98/rv98.dtb}"
JOBS="${SIMRV_LINUX_JOBS:-4}"
EXTERNAL="$ROOT_DIR/configs/buildroot/simrv-rv98"
ROOTFS_OFFSET=$((32 * 1024 * 1024))

if [[ ! -f "$BUILDROOT_SOURCE/Makefile" ]]; then
    echo "Buildroot $BUILDROOT_VER source not found: $BUILDROOT_SOURCE" >&2
    exit 1
fi
if [[ ! -f "$SOURCE_IMAGE" || ! -f "$SOURCE_DTB" ]]; then
    echo "RV98 source image or DTB is missing: $SOURCE_IMAGE / $SOURCE_DTB" >&2
    exit 1
fi
if ! command -v e2fsck >/dev/null || ! command -v dumpe2fs >/dev/null || ! command -v dd >/dev/null; then
    echo "Required host tools are missing (e2fsck, dumpe2fs, dd)." >&2
    exit 1
fi

mkdir -p "$BUILDROOT_OUTPUT" "$IMAGES_DIR"
cp "$EXTERNAL/rv32_defconfig" "$BUILDROOT_OUTPUT/.config"
make -C "$BUILDROOT_SOURCE" O="$BUILDROOT_OUTPUT" BR2_EXTERNAL="$EXTERNAL" olddefconfig
# Re-extract kbd so the RV98-specific openvt compatibility patch is applied.
make -C "$BUILDROOT_SOURCE" O="$BUILDROOT_OUTPUT" BR2_EXTERNAL="$EXTERNAL" kbd-dirclean
make -C "$BUILDROOT_SOURCE" O="$BUILDROOT_OUTPUT" BR2_EXTERNAL="$EXTERNAL" -j"$JOBS"
# Buildroot does not re-copy changed overlay files on every incremental build.
# Refresh them after package installation and regenerate the filesystem image.
mkdir -p "$BUILDROOT_OUTPUT/target"
cp -a "$EXTERNAL/rootfs-overlay/." "$BUILDROOT_OUTPUT/target/"
"$EXTERNAL/rootfs-post-build.sh" "$BUILDROOT_OUTPUT/target"
# Force ext4 regeneration from the refreshed target tree on both clean and
# incremental builds.
rm -f "$BUILDROOT_OUTPUT/images/rootfs.ext2" "$BUILDROOT_OUTPUT/images/rootfs.ext4"
make -C "$BUILDROOT_SOURCE" O="$BUILDROOT_OUTPUT" BR2_EXTERNAL="$EXTERNAL" -j"$JOBS"

ROOTFS="$BUILDROOT_OUTPUT/images/rootfs.ext2"
[[ -f "$ROOTFS" ]] || { echo "Buildroot did not produce $ROOTFS" >&2; exit 1; }
e2fsck -fn "$ROOTFS" >/dev/null
ROOTFS_BYTES=$(stat -c %s "$ROOTFS")
SOURCE_BYTES=$(stat -c %s "$SOURCE_IMAGE")
if (( ROOTFS_BYTES > SOURCE_BYTES - ROOTFS_OFFSET )); then
    echo "Rootfs ($ROOTFS_BYTES bytes) does not fit in the RV98 MMC image." >&2
    exit 1
fi

mkdir -p "$IMAGES_DIR"
cp --reflink=auto --sparse=always "$SOURCE_IMAGE" "$IMAGES_DIR/sys_full.img"
dd if="$ROOTFS" of="$IMAGES_DIR/sys_full.img" bs=4M seek=8 conv=notrunc,fsync status=progress
cp "$SOURCE_DTB" "$IMAGES_DIR/rv98.dtb"
cp "$EXTERNAL/README.md" "$IMAGES_DIR/README.md"
cat > "$IMAGES_DIR/setup.sh" <<EOF
#!/usr/bin/env sh
set -eu
SIMRV_RV98_DOOM_DIR=\$(CDPATH= cd -- "\$(dirname -- "\$0")" && pwd)
export SIMRV_RV98_DOOM_IMAGE="\$SIMRV_RV98_DOOM_DIR/sys_full.img"
export SIMRV_RV98_DOOM_DTB="\$SIMRV_RV98_DOOM_DIR/rv98.dtb"
echo "RV98 Doom image: \$SIMRV_RV98_DOOM_IMAGE"
echo "RV98 DTB:        \$SIMRV_RV98_DOOM_DTB"
EOF
chmod +x "$IMAGES_DIR/setup.sh"
echo "Built RV98 Doom image: $IMAGES_DIR/sys_full.img"
echo "Launch with: simrv --config configs/models/rvcomp.cfg --dtb $IMAGES_DIR/rv98.dtb -m $IMAGES_DIR/sys_full.img"
