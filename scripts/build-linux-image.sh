#!/usr/bin/env bash
# @file build-linux-image.sh
# @brief Modernized, high-speed boot image compiler for SimRV.
# Packages OpenSBI v1.8.1 and Linux v7.0.9 with embedded initramfs.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/linux-build"
ARCH="${ARCH:-rv64}"
IMAGES_DIR="$ROOT_DIR/linux-images/$ARCH"

# Versions
OPENSBI_VER="1.8.1"
LINUX_VER="7.0.9"
BUSYBOX_VER="1.36.1"
ALPINE_VER="3.20.0"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

print_step() { echo -e "${GREEN}[STEP]${NC} $1"; }
print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch)
            ARCH="$2"
            shift
            ;;
        --clean)
            print_step "Cleaning build and images directories..."
            rm -rf "$BUILD_DIR" "$IMAGES_DIR"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            exit 1
            ;;
    esac
    shift
done

mkdir -p "$BUILD_DIR/sources" "$IMAGES_DIR"

# Verify cross compiler
CROSS_COMPILE="riscv64-unknown-linux-gnu-"
if ! command -v "${CROSS_COMPILE}gcc" >/dev/null 2>&1; then
    print_error "Cross compiler ${CROSS_COMPILE}gcc not found on PATH."
    exit 1
fi

# Set architecture specifics
if [[ "$ARCH" == "rv64" ]]; then
    XLEN=64
    M_ARCH="rv64gc"
    M_ABI="lp64d"
    LINUX_DEFCONFIG="defconfig"
else
    XLEN=32
    M_ARCH="rv32gc"
    M_ABI="ilp32d"
    LINUX_DEFCONFIG="rv32_defconfig"
fi

print_info "Building Linux Image for Target: ${ARCH} (XLEN=${XLEN}, ${M_ARCH}, ${M_ABI})"

# ----------------------------------------------------------------------------
# Step 1: Download Sources
# ----------------------------------------------------------------------------
cd "$BUILD_DIR/sources"

if [[ ! -f "opensbi-${OPENSBI_VER}.tar.gz" ]]; then
    print_step "Downloading OpenSBI ${OPENSBI_VER}..."
    wget -q --show-progress -O "opensbi-${OPENSBI_VER}.tar.gz" "https://github.com/riscv-software-src/opensbi/archive/refs/tags/v${OPENSBI_VER}.tar.gz"
fi

if [[ ! -f "linux-${LINUX_VER}.tar.xz" ]]; then
    print_step "Downloading Linux ${LINUX_VER}..."
    wget -q --show-progress "https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-${LINUX_VER}.tar.xz"
fi

if [[ "$XLEN" == "32" ]]; then
    if [[ ! -f "busybox-${BUSYBOX_VER}.tar.bz2" ]]; then
        print_step "Downloading BusyBox ${BUSYBOX_VER}..."
        wget -q --show-progress "https://busybox.net/downloads/busybox-${BUSYBOX_VER}.tar.bz2"
    fi
else
    if [[ ! -f "alpine-minirootfs-${ALPINE_VER}-riscv64.tar.gz" ]]; then
        print_step "Downloading Alpine Linux minirootfs..."
        wget -q --show-progress -O "alpine-minirootfs-${ALPINE_VER}-riscv64.tar.gz" "https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/riscv64/alpine-minirootfs-${ALPINE_VER}-riscv64.tar.gz"
    fi
fi

# ----------------------------------------------------------------------------
# Step 2: Extract Sources
# ----------------------------------------------------------------------------
cd "$BUILD_DIR"

if [[ ! -d "opensbi-${OPENSBI_VER}" ]]; then
    print_step "Extracting OpenSBI..."
    tar -xf "sources/opensbi-${OPENSBI_VER}.tar.gz"
fi

# Configure OpenSBI features (disable legacy SBI and semihosting console)
OPENSBI_DEFCONFIG="$BUILD_DIR/opensbi-${OPENSBI_VER}/platform/generic/configs/defconfig"
if [[ -f "$OPENSBI_DEFCONFIG" ]]; then
    print_step "Configuring OpenSBI defconfig..."
    # Disable semihosting console
    sed -i 's/CONFIG_SERIAL_SEMIHOSTING=y/# CONFIG_SERIAL_SEMIHOSTING is not set/' "$OPENSBI_DEFCONFIG"
    # Disable legacy SBI extensions
    if grep -q "CONFIG_SBI_ECALL_LEGACY=y" "$OPENSBI_DEFCONFIG"; then
        sed -i 's/CONFIG_SBI_ECALL_LEGACY=y/# CONFIG_SBI_ECALL_LEGACY is not set/' "$OPENSBI_DEFCONFIG"
    elif ! grep -q "CONFIG_SBI_ECALL_LEGACY" "$OPENSBI_DEFCONFIG"; then
        echo "# CONFIG_SBI_ECALL_LEGACY is not set" >> "$OPENSBI_DEFCONFIG"
    fi
fi


if [[ ! -d "linux-${LINUX_VER}" ]]; then
    print_step "Extracting Linux..."
    tar -xf "sources/linux-${LINUX_VER}.tar.xz"
fi

if [[ "$XLEN" == "32" && ! -d "busybox-${BUSYBOX_VER}" ]]; then
    print_step "Extracting BusyBox..."
    tar -xf "sources/busybox-${BUSYBOX_VER}.tar.bz2"
fi

# ----------------------------------------------------------------------------
# Step 3: Compile User-space RootFS (embedded as initramfs)
# ----------------------------------------------------------------------------
INITRAMFS_DIR="$BUILD_DIR/initramfs-${ARCH}"
rm -rf "$INITRAMFS_DIR"
mkdir -p "$INITRAMFS_DIR"

if [[ "$XLEN" == "32" ]]; then
    # Compile BusyBox for RV32
    BUSYBOX_BUILD="$BUILD_DIR/busybox-${BUSYBOX_VER}"
    if [[ ! -f "$BUSYBOX_BUILD/busybox" ]]; then
        print_step "Configuring BusyBox..."
        make -C "$BUSYBOX_BUILD" clean || true
        make -C "$BUSYBOX_BUILD" ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" LD="${CROSS_COMPILE}ld -m elf32lriscv" "EXTRA_CFLAGS=-march=${M_ARCH} -mabi=${M_ABI}" defconfig
        # Force static build
        sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' "$BUSYBOX_BUILD/.config"
        # Disable TC applet which fails with modern headers
        sed -i 's/CONFIG_TC=y/# CONFIG_TC is not set/' "$BUSYBOX_BUILD/.config"
        sed -i 's/CONFIG_FEATURE_TC_INGRESS=y/# CONFIG_FEATURE_TC_INGRESS is not set/' "$BUSYBOX_BUILD/.config"
        print_step "Compiling BusyBox (RV32)..."
        make -C "$BUSYBOX_BUILD" ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" LD="${CROSS_COMPILE}ld -m elf32lriscv" "EXTRA_CFLAGS=-march=${M_ARCH} -mabi=${M_ABI}" "EXTRA_LDFLAGS=-Wl,-m,elf32lriscv" -j"$(nproc)" install
    fi
    cp -a "$BUSYBOX_BUILD/_install/"* "$INITRAMFS_DIR/"
else
    # Extract Alpine Linux minirootfs for RV64
    print_step "Extracting Alpine Linux minirootfs (RV64)..."
    tar -xf "sources/alpine-minirootfs-${ALPINE_VER}-riscv64.tar.gz" -C "$INITRAMFS_DIR"
fi

# Compile custom Snake game
print_step "Compiling custom Snake game..."
if [[ "$ARCH" == "rv64" ]]; then
    "${CROSS_COMPILE}gcc" -O2 -march="${M_ARCH}" -mabi="${M_ABI}" -Wl,-dynamic-linker=/lib/ld-musl-riscv64.so.1 -nodefaultlibs "$SCRIPT_DIR/snake.c" "$INITRAMFS_DIR/lib/libc.musl-riscv64.so.1" -lgcc -o "$INITRAMFS_DIR/usr/bin/snake"
else
    "${CROSS_COMPILE}gcc" -static -O2 -march="${M_ARCH}" -mabi="${M_ABI}" "$SCRIPT_DIR/snake.c" -o "$INITRAMFS_DIR/usr/bin/snake"
fi

# Set up init script and inittab
mkdir -p "$INITRAMFS_DIR/proc" "$INITRAMFS_DIR/sys" "$INITRAMFS_DIR/dev" "$INITRAMFS_DIR/etc" "$INITRAMFS_DIR/tmp"

cat > "$INITRAMFS_DIR/etc/inittab" <<'EOF'
ttyS0::respawn:-/bin/sh
EOF

echo "SimRV" > "$INITRAMFS_DIR/etc/hostname"

cat > "$INITRAMFS_DIR/init" <<'EOF'
#!/bin/sh
/bin/mount -t proc proc /proc
/bin/mount -t sysfs sysfs /sys
/bin/mount -t devtmpfs devtmpfs /dev || true
[ -f /etc/hostname ] && hostname -F /etc/hostname
exec 0</dev/ttyS0
exec 1>/dev/ttyS0
exec 2>/dev/ttyS0
clear
echo "=================================================="
echo "          Welcome to SimRV Linux Boot             "
echo "=================================================="
if [ -f /etc/alpine-release ]; then
    echo "Alpine Linux (riscv64) version $(cat /etc/alpine-release)"
else
    echo "Minimal BusyBox Linux (riscv32)"
fi
echo "=========================================="
exec /sbin/init
EOF
chmod +x "$INITRAMFS_DIR/init"

# Generate cpio archive
cd "$INITRAMFS_DIR"
find . -print0 | cpio --null -ov --format=newc > "$BUILD_DIR/initramfs_${ARCH}.cpio"

# ----------------------------------------------------------------------------
# Step 4: Compile Linux Kernel with Built-in Initramfs
# ----------------------------------------------------------------------------
LINUX_BUILD="$BUILD_DIR/linux-${LINUX_VER}"
cd "$LINUX_BUILD"

cp "$BUILD_DIR/initramfs_${ARCH}.cpio" "$LINUX_BUILD/initramfs.cpio"

if [[ ! -f .config ]]; then
    print_step "Configuring Linux Kernel..."
    make ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" "$LINUX_DEFCONFIG"
    
    # Configure embedded initramfs
    ./scripts/config --enable CONFIG_BLK_DEV_INITRD
    ./scripts/config --set-val CONFIG_INITRAMFS_SOURCE '"initramfs.cpio"'
    
    # Disable modules (keep single static kernel)
    ./scripts/config --disable CONFIG_MODULES
fi

# Always resolve new configs non-interactively to prevent prompt hangs
make ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" olddefconfig

# Build kernel (vmlinux)
print_step "Compiling/linking Linux Kernel v${LINUX_VER}..."
make ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" -j"$(nproc)" vmlinux

# Objcopy to bin format
"${CROSS_COMPILE}objcopy" -O binary vmlinux "$BUILD_DIR/vmlinux_${ARCH}.bin"

# ----------------------------------------------------------------------------
# Step 5: Compile OpenSBI Generic Payload
# ----------------------------------------------------------------------------
OPENSBI_BUILD="$BUILD_DIR/opensbi-${OPENSBI_VER}"
cd "$OPENSBI_BUILD"

# Remove cached build directory to force complete rebuild
rm -rf "$OPENSBI_BUILD/build"

print_step "Compiling OpenSBI v${OPENSBI_VER} (FW_PAYLOAD)..."

make PLATFORM=generic CROSS_COMPILE="$CROSS_COMPILE" \
     "CC=${CROSS_COMPILE}gcc -march=${M_ARCH} -mabi=${M_ABI}" \
     PLATFORM_RISCV_XLEN="$XLEN" \
     FW_PAYLOAD_PATH="$BUILD_DIR/vmlinux_${ARCH}.bin" \
     FW_TEXT_START=0x80000000 \
     FW_PAYLOAD_FDT_ADDR=0x83000000 \
     -j"$(nproc)"

# ----------------------------------------------------------------------------
# Step 6: Package Outputs
# ----------------------------------------------------------------------------
print_step "Packaging output artifacts..."
cp "$OPENSBI_BUILD/build/platform/generic/firmware/fw_payload.bin" "$IMAGES_DIR/fw_payload.bin"
cp "$OPENSBI_BUILD/build/platform/generic/firmware/fw_payload.elf" "$IMAGES_DIR/fw_payload.elf"
cp "$LINUX_BUILD/vmlinux" "$IMAGES_DIR/vmlinux"

# Compile DTS to DTB
dtc -I dts -O dtb -o "$IMAGES_DIR/devicetree.dtb" "$SCRIPT_DIR/templates/virt-rv${XLEN}.dts"

# Create standard setup.sh
cat > "$IMAGES_DIR/setup.sh" <<EOF
#!/bin/bash
IMAGES_DIR="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
export SIMRV_LINUX_MEM_IMG="\$IMAGES_DIR/fw_payload.bin"
export SIMRV_LINUX_DISK_IMG="\$IMAGES_DIR/root.bin"
export SIMRV_LINUX_DTB="\$IMAGES_DIR/devicetree.dtb"
export SIMRV_LINUX_TIMEOUT=60
export SIMRV_LINUX_END=1200000
echo "SimRV Native OpenSBI + Linux Image Setup Complete"
echo "├─ Memory image (OpenSBI + Kernel): \$SIMRV_LINUX_MEM_IMG"
echo "├─ Disk image (Mock): \$SIMRV_LINUX_DISK_IMG"
echo "└─ Device tree: \$SIMRV_LINUX_DTB"
EOF
chmod +x "$IMAGES_DIR/setup.sh"

# Create mock disk image for Phase 2 validation checker
dd if=/dev/zero of="$IMAGES_DIR/root.bin" bs=1M count=1 status=none

print_step "Success! Output images placed in: $IMAGES_DIR"
