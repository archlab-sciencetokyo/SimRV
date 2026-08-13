#!/usr/bin/env bash
# @file build-linux-image.sh
# @brief Modernized, high-speed boot image compiler for SimRV.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$ROOT_DIR/linux-build"
ARCH="${ARCH:-rv64}"
IMAGES_DIR="$ROOT_DIR/linux-images/$ARCH"

# Versions
OPENSBI_VER="1.9"
LINUX_VER="7.1.8"
BUSYBOX_VER="1.38.0"
ALPINE_VER="3.24.1"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

print_step() { echo -e "${GREEN}[STEP]${NC} $1"; }
print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

LIBC="${LIBC:-auto}"
CROSS_COMPILE="${CROSS_COMPILE:-}"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch)
            ARCH="$2"
            shift
            ;;
        --libc)
            LIBC="$2"
            shift
            ;;
        --cross-compile)
            CROSS_COMPILE="$2"
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

# Auto-detect cross compiler if not set
if [[ -z "$CROSS_COMPILE" ]]; then
    if [[ "$LIBC" == "musl" ]]; then
        CANDIDATES=("riscv64-unknown-linux-musl-" "riscv64-linux-musl-" "riscv64-alpine-linux-musl-")
    elif [[ "$LIBC" == "glibc" ]]; then
        CANDIDATES=("riscv64-unknown-linux-gnu-" "riscv64-linux-gnu-")
    else
        # auto: preference order (musl -> glibc)
        CANDIDATES=("riscv64-unknown-linux-musl-" "riscv64-linux-musl-" "riscv64-unknown-linux-gnu-" "riscv64-linux-gnu-")
    fi

    for c in "${CANDIDATES[@]}"; do
        if command -v "${c}gcc" >/dev/null 2>&1; then
            CROSS_COMPILE="$c"
            break
        fi
    done
fi

if [[ -z "$CROSS_COMPILE" ]] || ! command -v "${CROSS_COMPILE}gcc" >/dev/null 2>&1; then
    print_error "Cross compiler '${CROSS_COMPILE}gcc' not found on PATH."
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
    wget -q --show-progress --tries=3 "https://www.kernel.org/pub/linux/kernel/v7.x/linux-${LINUX_VER}.tar.xz"
fi

if [[ "$XLEN" == "32" ]]; then
    if [[ ! -f "busybox-${BUSYBOX_VER}.tar.bz2" ]]; then
        print_step "Downloading BusyBox ${BUSYBOX_VER}..."
        wget -q --show-progress "https://busybox.net/downloads/busybox-${BUSYBOX_VER}.tar.bz2"
    fi
else
    if [[ ! -f "alpine-minirootfs-${ALPINE_VER}-riscv64.tar.gz" ]]; then
        print_step "Downloading Alpine Linux minirootfs..."
        wget -q --show-progress -O "alpine-minirootfs-${ALPINE_VER}-riscv64.tar.gz" "https://dl-cdn.alpinelinux.org/alpine/v3.24/releases/riscv64/alpine-minirootfs-${ALPINE_VER}-riscv64.tar.gz"
    fi
fi

# ----------------------------------------------------------------------------
# Step 2: Extract Sources
# ----------------------------------------------------------------------------
cd "$BUILD_DIR"

if [[ ! -d "opensbi-${OPENSBI_VER}" ]]; then
    print_step "Extracting OpenSBI..."
    tar -xf "$BUILD_DIR/sources/opensbi-${OPENSBI_VER}.tar.gz"
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
    tar -xf "$BUILD_DIR/sources/linux-${LINUX_VER}.tar.xz"
fi

if [[ "$XLEN" == "32" && ! -d "busybox-${BUSYBOX_VER}" ]]; then
    print_step "Extracting BusyBox..."
    tar -xf "$BUILD_DIR/sources/busybox-${BUSYBOX_VER}.tar.bz2"
fi

# ----------------------------------------------------------------------------
# Step 3: Compile User-space RootFS (embedded as initramfs)
# ----------------------------------------------------------------------------
INITRAMFS_DIR="$BUILD_DIR/initramfs-${ARCH}"
rm -rf "$INITRAMFS_DIR"
mkdir -p "$INITRAMFS_DIR"

if [[ "$XLEN" == "32" ]]; then
    BUSYBOX_BUILD="$BUILD_DIR/busybox-${BUSYBOX_VER}"
    if [[ ! -f "$BUSYBOX_BUILD/_install/bin/busybox" ]]; then
        print_step "Configuring BusyBox..."
        make -C "$BUSYBOX_BUILD" clean || true
        make -C "$BUSYBOX_BUILD" ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" LD="${CROSS_COMPILE}ld -m elf32lriscv" "EXTRA_CFLAGS=-march=${M_ARCH} -mabi=${M_ABI}" defconfig
        sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' "$BUSYBOX_BUILD/.config"
        sed -i 's/CONFIG_TC=y/# CONFIG_TC is not set/' "$BUSYBOX_BUILD/.config"
        sed -i 's/CONFIG_FEATURE_TC_INGRESS=y/# CONFIG_FEATURE_TC_INGRESS is not set/' "$BUSYBOX_BUILD/.config"
        print_step "Compiling BusyBox (RV32)..."
        make -C "$BUSYBOX_BUILD" ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" LD="${CROSS_COMPILE}ld -m elf32lriscv" "EXTRA_CFLAGS=-march=${M_ARCH} -mabi=${M_ABI}" "EXTRA_LDFLAGS=-Wl,-m,elf32lriscv" -j"$(nproc)" install
    fi
    cp -a "$BUSYBOX_BUILD/_install/"* "$INITRAMFS_DIR/"
else
    print_step "Extracting Alpine Linux minirootfs (RV64)..."
    tar -xf "sources/alpine-minirootfs-${ALPINE_VER}-riscv64.tar.gz" -C "$INITRAMFS_DIR"
fi

# Compile custom Snake game
print_step "Compiling custom Snake game..."
if [[ "$ARCH" == "rv64" ]] && [[ -f "$INITRAMFS_DIR/lib/libc.musl-riscv64.so.1" ]]; then
    "${CROSS_COMPILE}gcc" -O2 -march="${M_ARCH}" -mabi="${M_ABI}" -Wl,-dynamic-linker=/lib/ld-musl-riscv64.so.1 -nodefaultlibs "$SCRIPT_DIR/snake.c" "$INITRAMFS_DIR/lib/libc.musl-riscv64.so.1" -lgcc -o "$INITRAMFS_DIR/usr/bin/snake"
else
    "${CROSS_COMPILE}gcc" -static -O2 -march="${M_ARCH}" -mabi="${M_ABI}" "$SCRIPT_DIR/snake.c" -o "$INITRAMFS_DIR/usr/bin/snake"
fi

# Set up init script and inittab
mkdir -p "$INITRAMFS_DIR/proc" "$INITRAMFS_DIR/sys" "$INITRAMFS_DIR/dev" "$INITRAMFS_DIR/etc" "$INITRAMFS_DIR/tmp"
mknod -m 600 "$INITRAMFS_DIR/dev/console" c 5 1 2>/dev/null || true
mknod -m 666 "$INITRAMFS_DIR/dev/ttyS0" c 4 64 2>/dev/null || true
mknod -m 666 "$INITRAMFS_DIR/dev/null" c 1 3 2>/dev/null || true
mknod -m 666 "$INITRAMFS_DIR/dev/tty" c 5 0 2>/dev/null || true

cat > "$INITRAMFS_DIR/etc/inittab" <<'EOF'
ttyS0::respawn:-/bin/sh
EOF

echo "SimRV" > "$INITRAMFS_DIR/etc/hostname"

cat > "$INITRAMFS_DIR/init" <<'EOF'
#!/bin/sh
mkdir -p /dev /proc /sys /etc /tmp /run
/bin/mount -t proc proc /proc 2>/dev/null || true
/bin/mount -t sysfs sysfs /sys 2>/dev/null || true
/bin/mount -t devtmpfs devtmpfs /dev 2>/dev/null || true

# Fallback device nodes if devtmpfs is absent
[ -c /dev/console ] || mknod -m 600 /dev/console c 5 1 2>/dev/null || true
[ -c /dev/ttyS0 ] || mknod -m 666 /dev/ttyS0 c 4 64 2>/dev/null || true
[ -c /dev/null ] || mknod -m 666 /dev/null c 1 3 2>/dev/null || true
[ -c /dev/zero ] || mknod -m 666 /dev/zero c 1 5 2>/dev/null || true

[ -f /etc/hostname ] && hostname -F /etc/hostname 2>/dev/null || true

echo ""
echo "=================================================="
echo "          Welcome to SimRV Linux Boot             "
echo "=================================================="
if [ -f /etc/alpine-release ]; then
    echo "Alpine Linux (riscv64) version $(cat /etc/alpine-release)"
else
    echo "Minimal BusyBox Linux (riscv32)"
fi
echo "=================================================="
echo ""

# Launch interactive shell
exec /bin/sh
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

rm -f .config
print_step "Configuring Linux Kernel..."
make ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" "$LINUX_DEFCONFIG"

# Configure embedded initramfs
./scripts/config --enable CONFIG_BLK_DEV_INITRD
./scripts/config --set-val CONFIG_INITRAMFS_SOURCE '"initramfs.cpio"'

# Disable modules (keep single static kernel)
./scripts/config --disable CONFIG_MODULES

# Built-in essential serial, console, virtio, and devtmpfs drivers
./scripts/config --enable CONFIG_SERIAL_8250
./scripts/config --enable CONFIG_SERIAL_8250_CONSOLE
./scripts/config --enable CONFIG_SERIAL_8250_MMIO
./scripts/config --enable CONFIG_SERIAL_OF_PLATFORM
./scripts/config --enable CONFIG_VIRTIO
./scripts/config --enable CONFIG_VIRTIO_MENU
./scripts/config --enable CONFIG_VIRTIO_MMIO
./scripts/config --enable CONFIG_VIRTIO_BLK
./scripts/config --enable CONFIG_VIRTIO_CONSOLE
./scripts/config --enable CONFIG_POWER_RESET
./scripts/config --enable CONFIG_POWER_RESET_SYSCON
./scripts/config --enable CONFIG_POWER_RESET_SYSCON_POWEROFF
./scripts/config --enable CONFIG_DEVTMPFS
./scripts/config --enable CONFIG_DEVTMPFS_MOUNT
./scripts/config --enable CONFIG_TTY
./scripts/config --enable CONFIG_VT

# High-speed boot optimizations: disable heavy debug features, RAID6 benchmarks, and unused subsystems
./scripts/config --disable CONFIG_SLUB_DEBUG
./scripts/config --disable CONFIG_DEBUG_KERNEL
./scripts/config --disable CONFIG_PROFILING
./scripts/config --disable CONFIG_DRM
./scripts/config --disable CONFIG_SOUND
./scripts/config --disable CONFIG_ETHERNET
./scripts/config --disable CONFIG_WLAN
./scripts/config --disable CONFIG_RAID6_PQ_BENCHMARK
./scripts/config --disable CONFIG_MD_RAID456
./scripts/config --disable CONFIG_MD
./scripts/config --disable CONFIG_BLK_DEV_MD
./scripts/config --enable CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
./scripts/config --disable CONFIG_RISCV_EMULATED_UNALIGNED_ACCESS
./scripts/config --enable CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE



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

# Compile DTS to DTB
dtc -I dts -O dtb -o "$IMAGES_DIR/devicetree.dtb" "$SCRIPT_DIR/templates/virt-rv${XLEN}.dts"

print_step "Compiling OpenSBI v${OPENSBI_VER} (FW_PAYLOAD)..."

make PLATFORM=generic CROSS_COMPILE="$CROSS_COMPILE" \
     "CC=${CROSS_COMPILE}gcc -march=${M_ARCH} -mabi=${M_ABI}" \
     PLATFORM_RISCV_XLEN="$XLEN" \
     FW_PAYLOAD_PATH="$BUILD_DIR/vmlinux_${ARCH}.bin" \
     FW_PAYLOAD_FDT_PATH="$IMAGES_DIR/devicetree.dtb" \
     FW_TEXT_START=0x80000000 \
     FW_PAYLOAD_FDT_ADDR=0x8FF00000 \
     -j"$(nproc)"



# ----------------------------------------------------------------------------
# Step 6: Package Outputs
# ----------------------------------------------------------------------------
print_step "Packaging output artifacts..."
cp "$OPENSBI_BUILD/build/platform/generic/firmware/fw_payload.bin" "$IMAGES_DIR/fw_payload.bin"
cp "$OPENSBI_BUILD/build/platform/generic/firmware/fw_payload.elf" "$IMAGES_DIR/fw_payload.elf"
cp "$LINUX_BUILD/vmlinux" "$IMAGES_DIR/vmlinux"

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

# Create ext4 disk image containing Alpine Linux minirootfs for fast boot
ROOTFS_DISK_DIR="$BUILD_DIR/rootfs-disk-${ARCH}"
rm -rf "$ROOTFS_DISK_DIR"
mkdir -p "$ROOTFS_DISK_DIR"

if [[ "$XLEN" == "64" ]]; then
    print_step "Extracting Alpine Linux minirootfs into ext4 root disk..."
    tar -xf "$BUILD_DIR/sources/alpine-minirootfs-${ALPINE_VER}-riscv64.tar.gz" -C "$ROOTFS_DISK_DIR"
    mkdir -p "$ROOTFS_DISK_DIR/proc" "$ROOTFS_DISK_DIR/sys" "$ROOTFS_DISK_DIR/dev" "$ROOTFS_DISK_DIR/etc" "$ROOTFS_DISK_DIR/tmp" "$ROOTFS_DISK_DIR/run"
    cat > "$ROOTFS_DISK_DIR/etc/inittab" <<'EOF'
ttyS0::respawn:-/bin/sh
EOF
    echo "SimRV" > "$ROOTFS_DISK_DIR/etc/hostname"
    cat > "$ROOTFS_DISK_DIR/init" <<'EOF'
#!/bin/sh
mkdir -p /dev /proc /sys /etc /tmp /run
/bin/mount -t proc proc /proc 2>/dev/null || true
/bin/mount -t sysfs sysfs /sys 2>/dev/null || true
/bin/mount -t devtmpfs devtmpfs /dev 2>/dev/null || true

[ -c /dev/console ] || mknod -m 600 /dev/console c 5 1 2>/dev/null || true
[ -c /dev/ttyS0 ] || mknod -m 666 /dev/ttyS0 c 4 64 2>/dev/null || true
[ -c /dev/null ] || mknod -m 666 /dev/null c 1 3 2>/dev/null || true
[ -c /dev/zero ] || mknod -m 666 /dev/zero c 1 5 2>/dev/null || true

[ -f /etc/hostname ] && hostname -F /etc/hostname 2>/dev/null || true

echo ""
echo "=================================================="
echo "          Welcome to SimRV Linux Boot             "
echo "=================================================="
echo "Alpine Linux (riscv64) version $(cat /etc/alpine-release 2>/dev/null || echo "3.24")"
echo "=================================================="
echo ""

exec /bin/sh
EOF
    chmod +x "$ROOTFS_DISK_DIR/init"
    dd if=/dev/zero of="$IMAGES_DIR/root.img" bs=1M count=64 status=none
    mkfs.ext4 -d "$ROOTFS_DISK_DIR" -F "$IMAGES_DIR/root.img"
    cp -f "$IMAGES_DIR/root.img" "$IMAGES_DIR/root.bin"
else
    dd if=/dev/zero of="$IMAGES_DIR/root.img" bs=1M count=1 status=none
    cp -f "$IMAGES_DIR/root.img" "$IMAGES_DIR/root.bin"
fi

print_step "Success! Output images placed in: $IMAGES_DIR"
