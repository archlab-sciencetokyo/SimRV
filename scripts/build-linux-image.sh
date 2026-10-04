#!/usr/bin/env bash
# @file build-linux-image.sh
# @brief Modernized, high-speed boot image compiler for SimRV.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${SIMRV_LINUX_BUILD_DIR:-$ROOT_DIR/linux-build}"
ARCH="${ARCH:-rv64}"

# Versions
OPENSBI_VER="1.9"
LINUX_VER="${LINUX_VER:-7.2.3}"
BUSYBOX_VER="1.38.0"
ALPINE_VER="3.24.1"
ALPINE_MIRROR="${SIMRV_ALPINE_MIRROR:-https://dl-cdn.alpinelinux.org/alpine/v3.24}"
BUILDROOT_VER="2025.02.18"
BUILDROOT_SHA256="e38ad1df6ea0479fff6419a87a64535d02131674d463be154a763ef725b55321"

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
ROOTFS_VARIANT="${ROOTFS_VARIANT:-alpine}"
PROFILE="${PROFILE:-gui}"
DRAM_SIZE_MB="${SIMRV_LINUX_DRAM_SIZE_MB:-512}"
CLEAN_ACTION=""

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
        --linux-version)
            LINUX_VER="$2"
            shift
            ;;
        --cross-compile)
            CROSS_COMPILE="$2"
            shift
            ;;
        --rootfs)
            ROOTFS_VARIANT="$2"
            shift
            ;;
        --profile)
            PROFILE="$2"
            shift
            ;;
        --dram-size-mb)
            DRAM_SIZE_MB="$2"
            shift
            ;;
        --alpine-mirror)
            ALPINE_MIRROR="$2"
            shift
            ;;
        --clean)
            CLEAN_ACTION="all"
            ;;
        --clean-build)
            CLEAN_ACTION="build"
            ;;
        --clean-old-kernels)
            CLEAN_ACTION="old-kernels"
            ;;
        -h|--help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  --arch <rv64|rv32>              Target architecture (default: rv64)"
            echo "  --libc <auto|musl|glibc>        C library selection (default: auto)"
            echo "  --linux-version <ver>           Linux kernel version (default: 7.2.3)"
            echo "  --cross-compile <prefix>        Cross compiler prefix"
            echo "  --rootfs <alpine|buildroot>     Root filesystem implementation (default: alpine)"
            echo "  --profile <gui|fpga|debug>      Linux deployment profile (default: gui)"
            echo "  --dram-size-mb <mb>             DTB DRAM size (default: 512)"
            echo "  --alpine-mirror <url>           Alpine v3.24 mirror base URL"
            echo "  --clean                         Clean build and images directories"
            echo "  --clean-build                   Clean build directory while keeping images"
            echo "  --clean-old-kernels             Prune older kernel trees, keeping current version"
            echo "  -h, --help                      Show this help message"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            echo "Run '$0 --help' for usage."
            exit 1
            ;;
esac
shift
done

if [[ "$ROOTFS_VARIANT" != "alpine" && "$ROOTFS_VARIANT" != "buildroot" ]]; then
    print_error "Unsupported rootfs '$ROOTFS_VARIANT' (expected alpine or buildroot)."
    exit 1
fi
if [[ "$PROFILE" != "gui" && "$PROFILE" != "fpga" && "$PROFILE" != "debug" ]]; then
    print_error "Unsupported profile '$PROFILE' (expected gui, fpga, or debug)."
    exit 1
fi
if [[ "$ROOTFS_VARIANT" == "buildroot" && "$PROFILE" != "fpga" ]]; then
    print_error "Buildroot currently provides the headless fpga profile; use --profile fpga."
    exit 1
fi
if [[ "$PROFILE" == "debug" && "$ROOTFS_VARIANT" != "alpine" ]]; then
    print_error "The debug profile currently extends the Alpine GUI image; use --rootfs alpine."
    exit 1
fi
if ! [[ "$DRAM_SIZE_MB" =~ ^[0-9]+$ ]] || (( DRAM_SIZE_MB < 128 )); then
    print_error "--dram-size-mb must be an integer of at least 128."
    exit 1
fi
if [[ "$ROOTFS_VARIANT" == "buildroot" ]]; then
    ROOTFS_LABEL="simrv-root"
    if ! perl -MExtUtils::MakeMaker -e 1 >/dev/null 2>&1; then
        print_error "Buildroot requires Perl ExtUtils::MakeMaker (install perl-devel/perl-ExtUtils-MakeMaker)."
        exit 1
    fi
else
    ROOTFS_LABEL="simrv-alpine"
fi

IMAGES_ROOT="${SIMRV_LINUX_IMAGES_ROOT:-$ROOT_DIR/linux-images}"
if [[ "$ROOTFS_VARIANT" == "alpine" && "$PROFILE" == "gui" ]]; then
    IMAGES_DIR="$IMAGES_ROOT/$ARCH"
else
    IMAGES_DIR="$IMAGES_ROOT/$ARCH/${ROOTFS_VARIANT}-${PROFILE}"
fi
case "$CLEAN_ACTION" in
    all)
        print_step "Cleaning build and ${ARCH} image directories..."
        rm -rf "$BUILD_DIR" "$IMAGES_DIR"
        exit 0
        ;;
    build)
        print_step "Cleaning intermediate build directory ($BUILD_DIR) while preserving $IMAGES_DIR..."
        rm -rf "$BUILD_DIR"
        exit 0
        ;;
    old-kernels)
        print_step "Pruning old kernel build trees in $BUILD_DIR..."
        if [[ -d "$BUILD_DIR" ]]; then
            find "$BUILD_DIR" -mindepth 1 -maxdepth 1 -type d -name "linux-*" ! -name "linux-${LINUX_VER}" -exec rm -rf {} +
            print_info "Kept current kernel tree (linux-${LINUX_VER}) if present."
        else
            print_info "Build directory ($BUILD_DIR) does not exist."
        fi
        exit 0
        ;;
esac
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

if [[ "$XLEN" == "32" || "$ROOTFS_VARIANT" == "buildroot" ]]; then
    if [[ ! -f "busybox-${BUSYBOX_VER}.tar.bz2" ]]; then
        print_step "Downloading BusyBox ${BUSYBOX_VER}..."
        wget -q --show-progress "https://busybox.net/downloads/busybox-${BUSYBOX_VER}.tar.bz2"
    fi
else
    if [[ ! -f "alpine-minirootfs-${ALPINE_VER}-riscv64.tar.gz" ]]; then
        print_step "Downloading Alpine Linux minirootfs..."
        wget -q --show-progress -O "alpine-minirootfs-${ALPINE_VER}-riscv64.tar.gz" "${ALPINE_MIRROR}/releases/riscv64/alpine-minirootfs-${ALPINE_VER}-riscv64.tar.gz"
    fi
fi

if [[ "$ROOTFS_VARIANT" == "buildroot" ]]; then
    if [[ ! -f "buildroot-${BUILDROOT_VER}.tar.xz" ]]; then
        print_step "Downloading Buildroot ${BUILDROOT_VER}..."
        wget -q --show-progress --tries=3 -O "buildroot-${BUILDROOT_VER}.tar.xz" \
            "https://buildroot.org/downloads/buildroot-${BUILDROOT_VER}.tar.xz"
    fi
    printf '%s  %s\n' "$BUILDROOT_SHA256" "buildroot-${BUILDROOT_VER}.tar.xz" | sha256sum -c -
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

if [[ ( "$XLEN" == "32" || "$ROOTFS_VARIANT" == "buildroot" ) && ! -d "busybox-${BUSYBOX_VER}" ]]; then
    print_step "Extracting BusyBox..."
    tar -xf "$BUILD_DIR/sources/busybox-${BUSYBOX_VER}.tar.bz2"
fi
if [[ "$ROOTFS_VARIANT" == "buildroot" && ! -d "buildroot-${BUILDROOT_VER}" ]]; then
    print_step "Extracting Buildroot..."
    tar -xf "$BUILD_DIR/sources/buildroot-${BUILDROOT_VER}.tar.xz"
fi

# ----------------------------------------------------------------------------
# Step 3: Compile User-space RootFS (embedded as initramfs)
# ----------------------------------------------------------------------------
INITRAMFS_DIR="$BUILD_DIR/initramfs-${ARCH}"
rm -rf "$INITRAMFS_DIR"
mkdir -p "$INITRAMFS_DIR"

if [[ "$XLEN" == "32" || "$ROOTFS_VARIANT" == "buildroot" ]]; then
    BUSYBOX_BUILD="$BUILD_DIR/busybox-${BUSYBOX_VER}"
    if [[ ! -f "$BUSYBOX_BUILD/_install/bin/busybox" ]]; then
        # BusyBox uses CC for its final link. Passing the ISA/ABI explicitly
        # keeps the early userspace architecture aligned with the kernel.
        BUSYBOX_CC="${CROSS_COMPILE}gcc -march=${M_ARCH} -mabi=${M_ABI}"
        print_step "Configuring BusyBox..."
        make -C "$BUSYBOX_BUILD" clean || true
        BUSYBOX_LD="${CROSS_COMPILE}ld"
        BUSYBOX_LDFLAGS=""
        if [[ "$XLEN" == "32" ]]; then
            BUSYBOX_LD="${CROSS_COMPILE}ld -m elf32lriscv"
            BUSYBOX_LDFLAGS="-Wl,-m,elf32lriscv"
        fi
        make -C "$BUSYBOX_BUILD" ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" CC="$BUSYBOX_CC" LD="$BUSYBOX_LD" defconfig
        sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' "$BUSYBOX_BUILD/.config"
        sed -i 's/CONFIG_TC=y/# CONFIG_TC is not set/' "$BUSYBOX_BUILD/.config"
        sed -i 's/CONFIG_FEATURE_TC_INGRESS=y/# CONFIG_FEATURE_TC_INGRESS is not set/' "$BUSYBOX_BUILD/.config"
        print_step "Compiling BusyBox (${ARCH})..."
        make -C "$BUSYBOX_BUILD" ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" CC="$BUSYBOX_CC" LD="$BUSYBOX_LD" "EXTRA_LDFLAGS=$BUSYBOX_LDFLAGS" -j"$(nproc)" install
    fi
    cp -a "$BUSYBOX_BUILD/_install/"* "$INITRAMFS_DIR/"
else
    print_step "Extracting Alpine Linux minirootfs (RV64)..."
    tar -xf "sources/alpine-minirootfs-${ALPINE_VER}-riscv64.tar.gz" -C "$INITRAMFS_DIR"
fi

# Set up init script and inittab
mkdir -p "$INITRAMFS_DIR/proc" "$INITRAMFS_DIR/sys" "$INITRAMFS_DIR/dev" "$INITRAMFS_DIR/etc" "$INITRAMFS_DIR/tmp"
# Use fakeroot so mknod succeeds without real root, giving the initramfs proper
# /dev/console and /dev/ttyS0 nodes at kernel boot time (prevents the kernel
# warning "unable to open an initial console" and the subsequent sh-exits-immediately panic).
if command -v fakeroot >/dev/null 2>&1; then
    fakeroot -- bash -c "
        mknod -m 600 '$INITRAMFS_DIR/dev/console' c 5 1
        mknod -m 666 '$INITRAMFS_DIR/dev/ttyS0'   c 4 64
        mknod -m 666 '$INITRAMFS_DIR/dev/null'     c 1 3
        mknod -m 666 '$INITRAMFS_DIR/dev/tty'      c 5 0
        mknod -m 666 '$INITRAMFS_DIR/dev/zero'     c 1 5
        mknod -m 600 '$INITRAMFS_DIR/dev/mem'      c 1 1
    " 2>/dev/null || true
else
    mknod -m 600 "$INITRAMFS_DIR/dev/console" c 5 1 2>/dev/null || true
    mknod -m 666 "$INITRAMFS_DIR/dev/ttyS0" c 4 64 2>/dev/null || true
    mknod -m 666 "$INITRAMFS_DIR/dev/null" c 1 3 2>/dev/null || true
    mknod -m 666 "$INITRAMFS_DIR/dev/tty" c 5 0 2>/dev/null || true
    mknod -m 666 "$INITRAMFS_DIR/dev/zero" c 1 5 2>/dev/null || true
    mknod -m 600 "$INITRAMFS_DIR/dev/mem" c 1 1 2>/dev/null || true
fi

cat > "$INITRAMFS_DIR/etc/inittab" <<'EOF'
ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100
EOF

echo "SimRV" > "$INITRAMFS_DIR/etc/hostname"

cat > "$INITRAMFS_DIR/init" <<'EOF'
#!/bin/sh
mkdir -p /dev /proc /sys /etc /tmp /run /dev/pts
# These belong to the previous guest lifetime. A simulator restart cannot
# preserve a live Xorg process, so remove them before BusyBox starts tty1.
rm -f /tmp/.X0-lock /tmp/.X11-unix/X0 /tmp/simrv-jwm.lock/pid
rmdir /tmp/simrv-jwm.lock 2>/dev/null || true
/bin/mount -t proc proc /proc 2>/dev/null || true
/bin/mount -t sysfs sysfs /sys 2>/dev/null || true
/bin/mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
/bin/mount -t devpts devpts /dev/pts 2>/dev/null || true

# Fallback device nodes if devtmpfs is absent
[ -c /dev/console ] || mknod -m 600 /dev/console c 5 1 2>/dev/null || true
[ -c /dev/tty ] || mknod -m 666 /dev/tty c 5 0 2>/dev/null || true
[ -c /dev/ttyS0 ] || mknod -m 666 /dev/ttyS0 c 4 64 2>/dev/null || true
[ -c /dev/null ] || mknod -m 666 /dev/null c 1 3 2>/dev/null || true
[ -c /dev/zero ] || mknod -m 666 /dev/zero c 1 5 2>/dev/null || true
[ -c /dev/mem ] || mknod -m 600 /dev/mem c 1 1 2>/dev/null || true
if [ -c /dev/fb0 ]; then
    echo "Framebuffer: /dev/fb0 detected (640x480x32)"
fi

# If an external root disk (/dev/vda) is provided, mount and switch_root into it
if [ -b /dev/vda ]; then
    mkdir -p /newroot
    mounted=0
    # VirtIO block discovery can race init on fast boots. Give the device and
    # its ext4 journal a few seconds to become readable before falling back to
    # the initramfs shell.
    for _ in 1 2 3 4 5; do
        if mount -t ext4 -L ROOTFS_LABEL_PLACEHOLDER /newroot 2>/dev/null || \
           mount -t ext4 /dev/vda /newroot 2>/dev/null; then
            mounted=1
            break
        fi
        echo "Waiting for /dev/vda ext4 filesystem..." > /dev/ttyS0
        sleep 1
    done
    if [ "$mounted" -eq 1 ]; then
        echo "Mounted /dev/vda as root filesystem"
        mount --move /dev /newroot/dev 2>/dev/null || true
        mount --move /proc /newroot/proc 2>/dev/null || true
        mount --move /sys /newroot/sys 2>/dev/null || true
        # BusyBox places the applet in /sbin; use an absolute path because the
        # initramfs shell may not have a usable PATH during early handoff.
        echo "Switching root to ROOT_INIT_PLACEHOLDER"
        # Reattach PID 1 to the existing kernel console after moving devtmpfs;
        # this is the conventional BusyBox initramfs handoff form.
        if ! exec /sbin/switch_root -c /dev/console /newroot ROOT_INIT_PLACEHOLDER; then
            echo "switch_root failed; continuing in initramfs" > /dev/ttyS0
        fi
    fi
fi

[ -f /etc/hostname ] && hostname -F /etc/hostname 2>/dev/null || true

echo ""
echo "=================================================="
echo "          Welcome to SimRV Linux Boot             "
echo "=================================================="
if [ -f /etc/alpine-release ]; then
    echo "Alpine Linux (riscv64) version $(cat /etc/alpine-release 2>/dev/null || echo "3.24")"
else
    echo "Minimal BusyBox Linux (riscv32)"
fi
echo "=================================================="
echo ""

# Hand off to BusyBox init as PID 1 so it can fork children with setsid()
# (PID 1 itself cannot call setsid — EPERM; init forks children that can)
exec /sbin/init
EOF
chmod +x "$INITRAMFS_DIR/init"
if [[ "$PROFILE" == "debug" ]]; then
    # Keep early-userspace tracing confined to the expanded debug profile.
    sed -i '2a set -x' "$INITRAMFS_DIR/init"
fi
# Both profiles use the conventional BusyBox init location on the mounted
# root filesystem. Keep /init only for explicit recovery/debug boots.
ROOT_INIT_PATH="/sbin/init"
sed -i "s#ROOTFS_LABEL_PLACEHOLDER#$ROOTFS_LABEL#; s#ROOT_INIT_PLACEHOLDER#$ROOT_INIT_PATH#" "$INITRAMFS_DIR/init"

# Generate cpio archive using gen_init_cpio (built from the kernel source).
# This tool accepts a plain-text manifest with explicit 'nod' entries so device
# nodes are embedded with the correct major:minor numbers without needing root
# or fakeroot at all.
GEN_INIT_CPIO_BIN="$BUILD_DIR/gen_init_cpio"
GEN_INIT_CPIO_SRC="$BUILD_DIR/linux-${LINUX_VER}/usr/gen_init_cpio.c"
if [[ ! -x "$GEN_INIT_CPIO_BIN" && -f "$GEN_INIT_CPIO_SRC" ]]; then
    print_step "Building gen_init_cpio..."
    gcc -O2 -o "$GEN_INIT_CPIO_BIN" "$GEN_INIT_CPIO_SRC"
fi

CPIO_LIST="$BUILD_DIR/initramfs_list_${ARCH}.txt"
print_step "Generating initramfs manifest..."

# Fixed device nodes — must come first, before any directory walk
cat > "$CPIO_LIST" <<'LISTEOF'
# Mandatory device nodes (no root required via gen_init_cpio)
dir /dev 0755 0 0
nod /dev/console 0600 0 0 c 5 1
nod /dev/ttyS0   0666 0 0 c 4 64
nod /dev/null    0666 0 0 c 1 3
nod /dev/tty     0666 0 0 c 5 0
nod /dev/zero    0666 0 0 c 1 5
nod /dev/urandom 0666 0 0 c 1 9
nod /dev/random  0666 0 0 c 1 8
nod /dev/mem     0600 0 0 c 1 1
LISTEOF

# Walk the initramfs dir and emit the rest of the manifest entries
python3 - "$INITRAMFS_DIR" >> "$CPIO_LIST" <<'PYEOF'
import os, stat, sys
initramfs_dir = sys.argv[1]
for root, dirs, files in os.walk(initramfs_dir, followlinks=False):
    dirs.sort(); files.sort()
    rel_root = os.path.relpath(root, initramfs_dir)
    if rel_root == ".":
        rel_root = ""
    # Skip /dev — already handled by hardcoded nod entries above
    if rel_root == "dev" or rel_root.startswith("dev/"):
        dirs.clear()
        continue
    if rel_root:
        st = os.stat(root)
        print(f"dir /{rel_root} 0{oct(stat.S_IMODE(st.st_mode))[2:]} 0 0")
    for fname in sorted(files):
        fpath = os.path.join(root, fname)
        rel_path = os.path.relpath(fpath, initramfs_dir)
        if rel_path.startswith("dev/"): continue
        try:
            st = os.lstat(fpath)
        except OSError:
            continue
        mode = f"0{oct(stat.S_IMODE(st.st_mode))[2:]}"
        if stat.S_ISLNK(st.st_mode):
            print(f"slink /{rel_path} {os.readlink(fpath)} {mode} 0 0")
        elif stat.S_ISREG(st.st_mode):
            print(f"file /{rel_path} {fpath} {mode} 0 0")
        elif stat.S_ISCHR(st.st_mode):
            print(f"nod /{rel_path} {mode} 0 0 c {os.major(st.st_rdev)} {os.minor(st.st_rdev)}")
        elif stat.S_ISBLK(st.st_mode):
            print(f"nod /{rel_path} {mode} 0 0 b {os.major(st.st_rdev)} {os.minor(st.st_rdev)}")
PYEOF

if [[ -x "$GEN_INIT_CPIO_BIN" ]]; then
    print_step "Generating initramfs cpio via gen_init_cpio..."
    "$GEN_INIT_CPIO_BIN" "$CPIO_LIST" > "$BUILD_DIR/initramfs_${ARCH}.cpio"
else
    # Fallback: standard cpio (device nodes will be missing without root)
    print_step "WARNING: gen_init_cpio not built; falling back to cpio (device nodes require root)"
    cd "$INITRAMFS_DIR"
    find . -print0 | cpio --null --create --format=newc --quiet > "$BUILD_DIR/initramfs_${ARCH}.cpio"
fi

# ----------------------------------------------------------------------------
# Step 4: Compile Linux Kernel with Built-in Initramfs
# ----------------------------------------------------------------------------
LINUX_BUILD="$BUILD_DIR/linux-${LINUX_VER}"
cd "$LINUX_BUILD"

# The source tree is shared by RV32 and RV64 builds. Remove generated objects
# before changing XLEN so stale architecture-specific objects cannot be linked
# into the next kernel.
print_step "Cleaning Linux build tree before configuring ${ARCH}..."
if [[ "${SIMRV_LINUX_REUSE_KERNEL:-0}" == "1" ]]; then
    print_info "Reusing existing Linux build tree (SIMRV_LINUX_REUSE_KERNEL=1)."
else
    make ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" mrproper
fi

cp "$BUILD_DIR/initramfs_${ARCH}.cpio" "$LINUX_BUILD/initramfs.cpio"

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
./scripts/config --enable CONFIG_VIRTIO_PCI
./scripts/config --enable CONFIG_VIRTIO_MMIO
./scripts/config --enable CONFIG_VIRTIO_BLK
./scripts/config --enable CONFIG_VIRTIO_NET
./scripts/config --enable CONFIG_VIRTIO_CONSOLE
./scripts/config --enable CONFIG_NET
./scripts/config --enable CONFIG_INET
./scripts/config --enable CONFIG_NETDEVICES
./scripts/config --enable CONFIG_ETHERNET
./scripts/config --enable CONFIG_EXT4_FS
./scripts/config --enable CONFIG_JBD2
./scripts/config --enable CONFIG_EXT4_FS_POSIX_ACL
./scripts/config --enable CONFIG_TMPFS
./scripts/config --enable CONFIG_PROC_FS
./scripts/config --enable CONFIG_SYSFS
./scripts/config --enable CONFIG_INPUT
./scripts/config --enable CONFIG_INPUT_EVDEV
./scripts/config --enable CONFIG_INPUT_KEYBOARD
./scripts/config --enable CONFIG_INPUT_MOUSE
./scripts/config --enable CONFIG_VIRTIO_INPUT
./scripts/config --enable CONFIG_POWER_RESET
./scripts/config --enable CONFIG_POWER_RESET_SYSCON
./scripts/config --enable CONFIG_POWER_RESET_SYSCON_POWEROFF
./scripts/config --enable CONFIG_DEVTMPFS
./scripts/config --enable CONFIG_DEVTMPFS_MOUNT
./scripts/config --enable CONFIG_DEVMEM
./scripts/config --enable CONFIG_TTY
./scripts/config --enable CONFIG_VT
./scripts/config --enable CONFIG_FB
./scripts/config --enable CONFIG_FB_SIMPLE
./scripts/config --enable CONFIG_FRAMEBUFFER_CONSOLE

# High-speed boot optimizations: disable heavy debug features, RAID6 benchmarks, and unused subsystems
./scripts/config --disable CONFIG_SLUB_DEBUG
./scripts/config --disable CONFIG_DEBUG_KERNEL
./scripts/config --disable CONFIG_PROFILING
./scripts/config --disable CONFIG_DRM
./scripts/config --disable CONFIG_SOUND
./scripts/config --disable CONFIG_WLAN
./scripts/config --disable CONFIG_RAID6_PQ_BENCHMARK
./scripts/config --disable CONFIG_MD_RAID456
./scripts/config --disable CONFIG_MD
./scripts/config --disable CONFIG_BLK_DEV_MD
./scripts/config --enable CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
./scripts/config --disable CONFIG_RISCV_EMULATED_UNALIGNED_ACCESS
./scripts/config --enable CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE

# Conventional low-overhead boot diagnostics. These are useful on both the
# GUI and FPGA profiles and make serial boot timing unambiguous.
./scripts/config --enable CONFIG_PRINTK_TIME
./scripts/config --enable CONFIG_PRINTK_CALLER

if [[ "$PROFILE" == "debug" ]]; then
    print_info "Enabling expanded kernel debug/profile instrumentation."
    ./scripts/config --enable CONFIG_DEBUG_KERNEL
    ./scripts/config --enable CONFIG_DEBUG_FS
    ./scripts/config --enable CONFIG_KALLSYMS
    ./scripts/config --enable CONFIG_KALLSYMS_ALL
    ./scripts/config --enable CONFIG_STACKTRACE
    ./scripts/config --enable CONFIG_MAGIC_SYSRQ
    ./scripts/config --enable CONFIG_FTRACE
    ./scripts/config --enable CONFIG_FUNCTION_TRACER
    ./scripts/config --enable CONFIG_FUNCTION_GRAPH_TRACER
    ./scripts/config --enable CONFIG_BLK_DEV_IO_TRACE
    ./scripts/config --enable CONFIG_DEBUG_INFO
    ./scripts/config --enable CONFIG_DEBUG_INFO_DWARF5
else
    ./scripts/config --disable CONFIG_DEBUG_FS
    ./scripts/config --disable CONFIG_KALLSYMS_ALL
    ./scripts/config --disable CONFIG_FUNCTION_GRAPH_TRACER
    ./scripts/config --disable CONFIG_BLK_DEV_IO_TRACE
fi



# Always resolve new configs non-interactively to prevent prompt hangs
make ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" olddefconfig

# Build kernel (vmlinux)
print_step "Compiling/linking Linux Kernel v${LINUX_VER}..."
make ARCH=riscv CROSS_COMPILE="$CROSS_COMPILE" -j"$(nproc)" vmlinux

# Objcopy to bin format
"${CROSS_COMPILE}objcopy" -O binary vmlinux "$BUILD_DIR/vmlinux_${ARCH}.bin"
cp "$BUILD_DIR/vmlinux_${ARCH}.bin" "$BUILD_DIR/Image_${ARCH}"

# ----------------------------------------------------------------------------
# Step 5: Compile OpenSBI Generic Payload
# ----------------------------------------------------------------------------
OPENSBI_BUILD="$BUILD_DIR/opensbi-${OPENSBI_VER}"
cd "$OPENSBI_BUILD"

# Remove cached build directory to force complete rebuild
rm -rf "$OPENSBI_BUILD/build"

# Compile the virt-compatible DTB from the selected memory and rootfs
# contract. Keep the checked-in templates readable while making the published
# artifact match the simulator/FPGA memory map.
DRAM_BYTES=$((DRAM_SIZE_MB * 1024 * 1024))
DRAM_HEX=$(printf '0x%x' "$DRAM_BYTES")
GENERATED_DTS="$BUILD_DIR/virt-rv${XLEN}-${ROOTFS_VARIANT}-${PROFILE}.dts"
cp "$SCRIPT_DIR/templates/virt-rv${XLEN}.dts" "$GENERATED_DTS"
if [[ "$XLEN" == "64" ]]; then
    sed -i -E "s#reg = <0x0 0x80000000 0x0 0x(20000000|40000000)>;#reg = <0x0 0x80000000 0x0 ${DRAM_HEX}>;#" "$GENERATED_DTS"
else
    sed -i -E "s#reg = <0x80000000 0x(20000000|40000000)>;#reg = <0x80000000 ${DRAM_HEX}>;#" "$GENERATED_DTS"
fi
sed -i -E "s#root=([^ ]+)#root=LABEL=${ROOTFS_LABEL}#; s# rdinit=/init##" "$GENERATED_DTS"
if [[ "$PROFILE" == "debug" ]]; then
    # The debug image is an explicit early-userspace/recovery profile.
    sed -i 's# rw loglevel=7# rw rdinit=/init loglevel=7#' "$GENERATED_DTS"
fi

# Compile DTS to DTB
DTC_BIN="$(command -v dtc || true)"
if [[ -z "$DTC_BIN" && -x "$LINUX_BUILD/scripts/dtc/dtc" ]]; then
    DTC_BIN="$LINUX_BUILD/scripts/dtc/dtc"
fi
if [[ -z "$DTC_BIN" ]]; then
    print_error "Device-tree compiler 'dtc' was not found in PATH or the Linux build tree."
    exit 1
fi
"$DTC_BIN" -I dts -O dtb -o "$IMAGES_DIR/devicetree.dtb" "$GENERATED_DTS"

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
cp "$BUILD_DIR/Image_${ARCH}" "$IMAGES_DIR/Image"

# Also publish an unbundled OpenSBI image for FPGA boot flows. A bootloader is
# responsible for supplying the next-stage address and DTB to this artifact;
# fw_payload.bin remains the self-contained SimRV convenience image.
if [[ ! -f "$OPENSBI_BUILD/build/platform/generic/firmware/fw_dynamic.bin" ]]; then
    make PLATFORM=generic CROSS_COMPILE="$CROSS_COMPILE" \
         "CC=${CROSS_COMPILE}gcc -march=${M_ARCH} -mabi=${M_ABI}" \
         PLATFORM_RISCV_XLEN="$XLEN" FW_DYNAMIC=y FW_TEXT_START=0x80000000 \
         -j"$(nproc)"
fi
cp "$OPENSBI_BUILD/build/platform/generic/firmware/fw_dynamic.bin" "$IMAGES_DIR/fw_dynamic.bin"

# Create standard setup.sh
cat > "$IMAGES_DIR/setup.sh" <<EOF
#!/bin/bash
IMAGES_DIR="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"
export SIMRV_LINUX_MEM_IMG="\$IMAGES_DIR/fw_payload.bin"
export SIMRV_LINUX_DISK_IMG="\$IMAGES_DIR/root.img"
export SIMRV_LINUX_DTB="\$IMAGES_DIR/devicetree.dtb"
export SIMRV_LINUX_IMAGE="\$IMAGES_DIR/Image"
export SIMRV_LINUX_OPENSBI="\$IMAGES_DIR/fw_dynamic.bin"
export SIMRV_LINUX_PROFILE="${PROFILE}"
export SIMRV_LINUX_ROOTFS="${ROOTFS_VARIANT}"
export SIMRV_LINUX_TIMEOUT=60
export SIMRV_LINUX_END=1200000
echo "SimRV OpenSBI + Linux ${ROOTFS_VARIANT}/${PROFILE} setup complete"
echo "├─ Payload image: \$SIMRV_LINUX_MEM_IMG"
echo "├─ OpenSBI dynamic: \$SIMRV_LINUX_OPENSBI"
echo "├─ Linux Image: \$SIMRV_LINUX_IMAGE"
echo "├─ Disk image: \$SIMRV_LINUX_DISK_IMG"
echo "└─ Device tree: \$SIMRV_LINUX_DTB"
EOF
chmod +x "$IMAGES_DIR/setup.sh"

# Create the profile-specific journaled ext4 disk image.
ROOTFS_DISK_DIR="$BUILD_DIR/rootfs-disk-${ARCH}"
rm -rf "$ROOTFS_DISK_DIR"
mkdir -p "$ROOTFS_DISK_DIR"

if [[ "$ROOTFS_VARIANT" == "buildroot" ]]; then
    BUILDROOT_SOURCE="$BUILD_DIR/buildroot-${BUILDROOT_VER}"
    BUILDROOT_OUTPUT="$BUILD_DIR/buildroot-output-${ARCH}"
    BUILDROOT_CONFIG="$ROOT_DIR/configs/buildroot/simrv-fpga/${ARCH}_defconfig"
    print_step "Configuring Buildroot ${BUILDROOT_VER} FPGA rootfs..."
    if [[ "${SIMRV_LINUX_REUSE_BUILDROOT:-0}" != "1" ]]; then
        rm -rf "$BUILDROOT_OUTPUT"
    else
        print_info "Reusing existing Buildroot output (SIMRV_LINUX_REUSE_BUILDROOT=1)."
    fi
    mkdir -p "$BUILDROOT_OUTPUT"
    cp "$BUILDROOT_CONFIG" "$BUILDROOT_OUTPUT/.config"
    make -C "$BUILDROOT_SOURCE" O="$BUILDROOT_OUTPUT" \
         BR2_EXTERNAL="$ROOT_DIR/configs/buildroot/simrv-fpga" olddefconfig
    make -C "$BUILDROOT_SOURCE" O="$BUILDROOT_OUTPUT" BR2_EXTERNAL="$ROOT_DIR/configs/buildroot/simrv-fpga" -j"$(nproc)"
    cp "$BUILDROOT_OUTPUT/images/rootfs.ext4" "$IMAGES_DIR/root.img"
    cp "$IMAGES_DIR/root.img" "$IMAGES_DIR/root.bin"
    e2fsck -fy "$IMAGES_DIR/root.img" >/dev/null
else
  if [[ "$XLEN" == "64" ]]; then
    print_step "Extracting Alpine Linux minirootfs into ext4 root disk..."
    tar -xf "$BUILD_DIR/sources/alpine-minirootfs-${ALPINE_VER}-riscv64.tar.gz" -C "$ROOTFS_DISK_DIR"
    mkdir -p "$ROOTFS_DISK_DIR/proc" "$ROOTFS_DISK_DIR/sys" "$ROOTFS_DISK_DIR/dev" "$ROOTFS_DISK_DIR/etc" "$ROOTFS_DISK_DIR/tmp" "$ROOTFS_DISK_DIR/run"
    # This is a disposable simulator image, so keep the serial root login
    # usable for bring-up and privilege tests. Production FPGA images should
    # replace this with a board-specific password or key-based login policy.
    sed -i 's/^root:[^:]*:/root::/' "$ROOTFS_DISK_DIR/etc/shadow"
    # Pin the repository family used by both host-side APK bundling and the
    # guest first-boot fallback. This avoids inheriting stale mirror entries
    # from a downloaded minirootfs and makes alternate mirrors explicit.
    printf '%s\n' "${ALPINE_MIRROR}/main" "${ALPINE_MIRROR}/community" > "$ROOTFS_DISK_DIR/etc/repositories"
    cat > "$ROOTFS_DISK_DIR/etc/inittab" <<'EOF'
ttyS0::respawn:/sbin/getty -L 115200 ttyS0 vt100
::sysinit:/usr/local/bin/simrv-network
EOF
    echo "SimRV" > "$ROOTFS_DISK_DIR/etc/hostname"
    echo -e "127.0.0.1\tlocalhost SimRV\n::1\t\tlocalhost SimRV" > "$ROOTFS_DISK_DIR/etc/hosts"
    cat > "$ROOTFS_DISK_DIR/etc/fstab" <<'EOF'
LABEL=simrv-alpine / ext4 defaults 0 1
proc /proc proc defaults 0 0
sysfs /sys sysfs defaults 0 0
devtmpfs /dev devtmpfs mode=0755,nosuid 0 0
devpts /dev/pts devpts gid=5,mode=620 0 0
tmpfs /run tmpfs mode=0755,nosuid,nodev 0 0
EOF
    cat > "$ROOTFS_DISK_DIR/etc/os-release" <<'EOF'
NAME="SimRV Alpine Linux"
ID=alpine
ID_LIKE="alpine"
PRETTY_NAME="SimRV Alpine Linux"
EOF
mkdir -p "$ROOTFS_DISK_DIR/usr/local/bin" "$ROOTFS_DISK_DIR/root" \
             "$ROOTFS_DISK_DIR/etc/X11/xinit"
    cat > "$ROOTFS_DISK_DIR/etc/X11/xinit/xserverrc" <<'EOF'
#!/bin/sh
exec /usr/libexec/Xorg -config /etc/X11/xorg.conf -ac "$@"
EOF
    chmod 755 "$ROOTFS_DISK_DIR/etc/X11/xinit/xserverrc"
    cat > "$ROOTFS_DISK_DIR/usr/local/bin/start-jwm" <<'EOF'
#!/bin/sh
set -eu

echo "[JWM] Starting Xorg and JWM on framebuffer (uid=$(id -u), gid=$(id -g))..." > /dev/ttyS0
export DISPLAY=:0
export HOME=/root
export XAUTHORITY=/root/.Xauthority
export XDG_RUNTIME_DIR=/tmp/runtime-root
LOCK_DIR=/tmp/simrv-jwm.lock

pid_is_process() {
    pid=$1
    name=$2
    [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null || return 1
    [ -r "/proc/$pid/cmdline" ] || return 1
    tr '\000' ' ' < "/proc/$pid/cmdline" | grep -q "$name"
}

# BusyBox init can retry a once action after a console/session transition. Keep
# X server startup idempotent so a second xinit cannot collide with :0.
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    if [ -r "$LOCK_DIR/pid" ] && pid_is_process "$(cat "$LOCK_DIR/pid")" start-jwm; then
        echo "[JWM] X session is already starting or running; skipping duplicate launch." > /dev/ttyS0
        exit 0
    fi
    rm -f "$LOCK_DIR/pid"
    rmdir "$LOCK_DIR" 2>/dev/null || exit 0
    mkdir "$LOCK_DIR"
fi
echo "$$" > "$LOCK_DIR/pid"
cleanup() {
    rm -f "$LOCK_DIR/pid"
    rmdir "$LOCK_DIR" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

mkdir -p "$XDG_RUNTIME_DIR" /tmp/.X11-unix /tmp/.ICE-unix /var/log
chmod 700 "$XDG_RUNTIME_DIR"
chmod 1777 /tmp/.X11-unix /tmp/.ICE-unix

# The Alpine minirootfs is intentionally small and does not include the X11
# desktop packages. Install them lazily on the first networked boot so a clean
# image and a rebuilt image have the same GUI behavior.
if [ ! -x /usr/bin/xinit ]; then
    echo "[JWM] Installing X11 desktop packages on first boot..." > /dev/ttyS0
    if ! apk add --no-cache xorg-server xinit jwm xterm xf86-video-fbdev \
        xf86-input-evdev font-misc-misc font-dejavu links-graphics bsd-games \
        gnuchess > /tmp/simrv-apk.log 2>&1; then
        echo "[JWM] X11 package installation failed; see /tmp/simrv-apk.log" > /dev/ttyS0
        sed -n '1,24p' /tmp/simrv-apk.log > /dev/ttyS0 2>/dev/null || true
        exit 1
    fi
fi

# APK font triggers cannot run under host-side RISC-V emulation. Build the
# legacy X11 font index and refresh fontconfig on the guest before xterm starts.
if [ -x /usr/bin/mkfontscale ] && [ -d /usr/share/fonts/misc ]; then
    mkfontscale /usr/share/fonts/misc 2>/dev/null || true
    mkfontdir /usr/share/fonts/misc 2>/dev/null || true
fi
# Host-side package bundling intentionally skips APK triggers. Rebuild the
# fontconfig cache inside the guest before starting pango/JWM; stale or absent
# caches can make the graphical session crash during font discovery.
if [ -x /usr/bin/fc-cache ]; then
    fc-cache -f >/tmp/simrv-font-cache.log 2>&1 || true
fi
if [ -f /usr/share/fonts/misc/6x13.pcf.gz ]; then
    cat > /usr/share/fonts/misc/fonts.dir <<'FONTDIR'
1
6x13.pcf.gz -misc-fixed-medium-r-semicondensed--13-120-75-75-c-60-iso8859-1
FONTDIR
fi
if [ -f /etc/system.jwmrc ]; then
    sed -i 's/Sans-12:bold/DejaVu Sans-12:bold/g; s/>Sans-12</>DejaVu Sans-12</g' \
        /etc/system.jwmrc
    sed -i 's/<TrayButton label="JWM">root:1<\//<TrayButton label="SimRV">root:1<\//; s/<Background type="solid">#111111<\//<Background type="gradient">#21152b:#071622<\//' /etc/system.jwmrc
    sed -i 's#<Program icon="web-browser" label="Firefox">firefox</Program>#<Program icon="web-browser" label="Web Browser">links -g</Program>#' /etc/system.jwmrc
    sed -i '/<Swallow width="32" height="32" name="xclock">xclock<\/Swallow>/d; /<Clock format=.*exec:xclock<\/Clock>/d' /etc/system.jwmrc
    # Rebuild the custom Games submenu instead of appending it on every boot.
    # The rootfs is persistent, so this must also clean menus produced by older
    # non-idempotent versions of start-jwm.
    sed -i '/^[[:space:]]*<Menu icon="games" label="Games">$/,/^[[:space:]]*<\/Menu>$/d' \
        /etc/system.jwmrc
    sed -i '/label="Gimp"/i\
            <!-- SimRV Games submenu -->\
            <Menu icon="games" label="Games">\
                <Program label="Chess">xterm -T Chess -e gnuchess</Program>\
                <Program label="Gomoku">xterm -T Gomoku -e gomoku</Program>\
                <Program label="Klondike">xterm -T Klondike -e klondike</Program>\
                <Program label="Snake">xterm -T Snake -e snake</Program>\
            </Menu>' /etc/system.jwmrc
fi

if [ -r /tmp/.X0-lock ]; then
    xpid=$(tr -d '[:space:]' < /tmp/.X0-lock)
    if pid_is_process "$xpid" Xorg; then
        echo "[JWM] Xorg is already running on :0; skipping duplicate launch." > /dev/ttyS0
        exit 0
    fi
    echo "[JWM] Removing stale :0 lock." > /dev/ttyS0
    rm -f /tmp/.X0-lock /tmp/.X11-unix/X0
fi

# devtmpfs and virtio-input create these devices after the root filesystem is
# handed off. Do not race Xorg against them; otherwise it can exit with "no
# screens found" or start without a keyboard/pointer.
for _ in 1 2 3 4 5 6 7 8 9 10; do
    [ -c /dev/fb0 ] && [ -c /dev/input/event0 ] && break
    sleep 1
done

cd /root
/usr/bin/xinit /root/.xinitrc -- :0 -config /etc/X11/xorg.conf -ac vt1 \
    2>&1 | tee /tmp/xorg.log > /dev/ttyS0
EOF
    chmod 755 "$ROOTFS_DISK_DIR/usr/local/bin/start-jwm"
    cat > "$ROOTFS_DISK_DIR/usr/local/bin/simrv-network" <<'EOF'
#!/bin/sh
set -eu

# The TAP helper on the host provides 10.0.2.1/24. Keep this quiet when the
# guest is launched without a network device or with the deterministic user backend.
if ! ip link show eth0 >/dev/null 2>&1; then
    exit 0
fi
ip link set eth0 up 2>/dev/null || true
ip addr add 10.0.2.2/24 dev eth0 2>/dev/null || true
ip route add default via 10.0.2.1 dev eth0 2>/dev/null || true
# The TAP helper relays DNS from the host at the guest gateway. Keep public
# fallbacks for native hosts where the relay is unavailable.
printf '%s\n' 'nameserver 10.0.2.1' 'nameserver 1.1.1.1' 'nameserver 8.8.8.8' > /etc/resolv.conf
echo '[NET] eth0 configured as 10.0.2.2/24' > /dev/ttyS0
EOF
    chmod 755 "$ROOTFS_DISK_DIR/usr/local/bin/simrv-network"
    cat > "$ROOTFS_DISK_DIR/root/.profile" <<'EOF'
#!/bin/sh

# Start the GUI only after an authenticated serial login. Keep it in the
# background so the login shell remains available for diagnostics.
case "$(tty 2>/dev/null || true)" in
    /dev/ttyS0)
        if [ "$(id -u)" = 0 ] && [ -x /usr/local/bin/start-jwm ]; then
            /usr/local/bin/start-jwm >/tmp/simrv-jwm.log 2>&1 &
        fi
        ;;
esac
EOF
    chmod 755 "$ROOTFS_DISK_DIR/root/.profile"
    cat > "$ROOTFS_DISK_DIR/root/.xinitrc" <<'EOF'
#!/bin/sh
export DISPLAY=:0
export XAUTHORITY=/root/.Xauthority
xhost + 2>/dev/null || true
xterm -fn 6x13 -geometry 60x18+10+10 -bg '#1e293b' -fg '#f8fafc' -title "SimRV Terminal" &
exec jwm
EOF
    chmod 755 "$ROOTFS_DISK_DIR/root/.xinitrc"
    cat > "$ROOTFS_DISK_DIR/etc/X11/xorg.conf" <<'EOF'
Section "ServerLayout"
    Identifier "Layout0"
    Screen 0 "Screen0"
    InputDevice "VirtioInput" "CoreKeyboard"
    InputDevice "VirtioInput" "CorePointer"
EndSection

Section "InputDevice"
    Identifier "VirtioInput"
    Driver "evdev"
    Option "Device" "/dev/input/event0"
EndSection

Section "Files"
    FontPath "/usr/share/fonts/misc"
    FontPath "/usr/share/fonts/TTF"
    FontPath "/usr/share/fonts/100dpi"
    FontPath "/usr/share/fonts/75dpi"
EndSection

Section "Device"
    Identifier "Card0"
    Driver "fbdev"
    Option "fbdev" "/dev/fb0"
EndSection

Section "Screen"
    Identifier "Screen0"
    Device "Card0"
    DefaultDepth 24
    DefaultFbBpp 32
    SubSection "Display"
        Depth 24
        FbBpp 32
        Modes "640x480"
    EndSubSection
EndSection
EOF
    cat > "$ROOTFS_DISK_DIR/init" <<'EOF'
#!/bin/sh
mkdir -p /dev /proc /sys /etc /tmp /run /dev/pts
/bin/mount -t proc proc /proc 2>/dev/null || true
/bin/mount -t sysfs sysfs /sys 2>/dev/null || true
/bin/mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
/bin/mount -t devpts devpts /dev/pts 2>/dev/null || true

[ -c /dev/console ] || mknod -m 600 /dev/console c 5 1 2>/dev/null || true
[ -c /dev/tty ] || mknod -m 666 /dev/tty c 5 0 2>/dev/null || true
[ -c /dev/ttyS0 ] || mknod -m 666 /dev/ttyS0 c 4 64 2>/dev/null || true
[ -c /dev/null ] || mknod -m 666 /dev/null c 1 3 2>/dev/null || true
[ -c /dev/zero ] || mknod -m 666 /dev/zero c 1 5 2>/dev/null || true
[ -c /dev/mem ] || mknod -m 600 /dev/mem c 1 1 2>/dev/null || true

if [ -c /dev/fb0 ]; then
    echo "Framebuffer: /dev/fb0 detected (640x480x32)"
        if [ -w /dev/fb0 ]; then
            echo "Framebuffer: /dev/fb0 is writable"
        fi
else
    echo "Framebuffer: /dev/fb0 is not available"
fi

[ -f /etc/hostname ] && hostname -F /etc/hostname 2>/dev/null || true

echo ""
echo "=================================================="
echo "          Welcome to SimRV Linux Boot             "
echo "=================================================="
echo "Alpine Linux (riscv64) version $(cat /etc/alpine-release 2>/dev/null || echo "3.24")"
echo "=================================================="
echo ""

# Hand off to BusyBox init as PID 1 so it can fork children with setsid()
# (PID 1 itself cannot call setsid — EPERM; init forks children that can)
exec /sbin/init
EOF
    chmod +x "$ROOTFS_DISK_DIR/init"

    # Bundle the desktop stack when a RISC-V user emulator is available. APK
    # post-install triggers need to run on the guest kernel, so tolerate their
    # failure during host-side packaging as long as the required binaries were
    # extracted; the first-boot fallback below remains available otherwise.
    qemu_riscv64="${SIMRV_QEMU_RISCV64:-}"
    if [[ -z "$qemu_riscv64" && -x /opt/riscv/linux-glibc-rv64/bin/qemu-riscv64 ]]; then
        qemu_riscv64=/opt/riscv/linux-glibc-rv64/bin/qemu-riscv64
    fi
    if [[ -x "$qemu_riscv64" && -x "$ROOTFS_DISK_DIR/sbin/apk" ]]; then
        print_step "Bundling Alpine X11/JWM packages into the root disk..."
        set +e
        "$qemu_riscv64" -L "$ROOTFS_DISK_DIR" "$ROOTFS_DISK_DIR/sbin/apk" \
            --root "$ROOTFS_DISK_DIR" --no-cache --no-scripts add \
            xorg-server xinit jwm xterm xf86-video-fbdev xf86-input-evdev \
            font-misc-misc font-dejavu links-graphics bsd-games gnuchess
        apk_status=$?
        set -e
        if [[ -x "$ROOTFS_DISK_DIR/usr/bin/xinit" &&
              -x "$ROOTFS_DISK_DIR/usr/libexec/Xorg" &&
              -x "$ROOTFS_DISK_DIR/usr/bin/jwm" ]]; then
            print_info "X11/JWM package files bundled (apk status ${apk_status})."
            sed -i 's/Sans-12:bold/DejaVu Sans-12:bold/g; s/>Sans-12</>DejaVu Sans-12</g' \
                "$ROOTFS_DISK_DIR/etc/system.jwmrc"
            sed -i 's/<TrayButton label="JWM">root:1<\//<TrayButton label="SimRV">root:1<\//; s/<Background type="solid">#111111<\//<Background type="gradient">#21152b:#071622<\//' "$ROOTFS_DISK_DIR/etc/system.jwmrc"
            sed -i 's#<Program icon="web-browser" label="Firefox">firefox</Program>#<Program icon="web-browser" label="Web Browser">links -g</Program>#' "$ROOTFS_DISK_DIR/etc/system.jwmrc"
            sed -i '/<Swallow width="32" height="32" name="xclock">xclock<\/Swallow>/d; /<Clock format=.*exec:xclock<\/Clock>/d' "$ROOTFS_DISK_DIR/etc/system.jwmrc"
            # Keep the persistent JWM menu idempotent across image rebuilds.
            sed -i '/^[[:space:]]*<Menu icon="games" label="Games">$/,/^[[:space:]]*<\/Menu>$/d' \
                "$ROOTFS_DISK_DIR/etc/system.jwmrc"
            sed -i '/label="Gimp"/i\
            <!-- SimRV Games submenu -->\
            <Menu icon="games" label="Games">\
                <Program label="Chess">xterm -T Chess -e gnuchess</Program>\
                <Program label="Gomoku">xterm -T Gomoku -e gomoku</Program>\
                <Program label="Klondike">xterm -T Klondike -e klondike</Program>\
                <Program label="Snake">xterm -T Snake -e snake</Program>\
            </Menu>' "$ROOTFS_DISK_DIR/etc/system.jwmrc"
        else
            print_info "X11 bundle unavailable; retaining first-boot APK fallback."
        fi
    else
        print_info "RISC-V QEMU unavailable; retaining first-boot APK fallback."
    fi

    DISK_MB=$(du -sm "$ROOTFS_DISK_DIR" | cut -f1)
    DISK_MB=$(( DISK_MB + 128 ))
    # Keep a generous writable package/data area for browsers, games, and
    # normal Alpine package-manager use.
    if [ "$DISK_MB" -lt 4096 ]; then DISK_MB=4096; fi
    dd if=/dev/zero of="$IMAGES_DIR/root.img" bs=1M count="$DISK_MB" status=none
    # Keep the guest root disk aligned with normal Linux ext4 behavior. The
    # journal provides crash recovery when a simulator or host is interrupted.
    mkfs.ext4 -L "$ROOTFS_LABEL" -d "$ROOTFS_DISK_DIR" -F "$IMAGES_DIR/root.img"
    # Repair the newly-created golden before publishing it; future disposable-
    # run clones can then be validated without mutation.
    e2fsck -fy "$IMAGES_DIR/root.img" >/dev/null
    cp -f "$IMAGES_DIR/root.img" "$IMAGES_DIR/root.bin"
  else
        /usr/sbin/dd if=/dev/zero of="$IMAGES_DIR/root.img" bs=1M count=1 status=none
    tune2fs -L "$ROOTFS_LABEL" "$IMAGES_DIR/root.img" >/dev/null 2>&1 || true
    cp -f "$IMAGES_DIR/root.img" "$IMAGES_DIR/root.bin"
  fi
fi

BOOTARGS=$(sed -n 's/.*bootargs = "\([^"]*\)".*/\1/p' "$GENERATED_DTS")
MANIFEST="$IMAGES_DIR/manifest.json"
KERNEL_CONFIG="$LINUX_BUILD/.config"
KERNEL_CONFIG_SHA256=$(sha256sum "$KERNEL_CONFIG" | cut -d' ' -f1)
if [[ "$PROFILE" == "debug" ]]; then
    DEBUG_FEATURES='["debugfs","kallsyms-all","magic-sysrq","function-tracing","function-graph-tracer","block-i/o-tracing","stacktrace","dwarf5"]'
else
    DEBUG_FEATURES='[]'
fi
{
    echo '{'
    printf '  "architecture": "%s",\n' "$ARCH"
    printf '  "xlen": %s,\n' "$XLEN"
    printf '  "profile": "%s",\n' "$PROFILE"
    printf '  "rootfs": "%s",\n' "$ROOTFS_VARIANT"
    printf '  "opensbi_version": "%s",\n' "$OPENSBI_VER"
    printf '  "linux_version": "%s",\n' "$LINUX_VER"
    printf '  "buildroot_version": "%s",\n' "$BUILDROOT_VER"
    printf '  "buildroot_source_sha256": "%s",\n' "$BUILDROOT_SHA256"
    printf '  "dram_size_mb": %s,\n' "$DRAM_SIZE_MB"
    printf '  "rootfs_label": "%s",\n' "$ROOTFS_LABEL"
    printf '  "bootargs": "%s",\n' "$BOOTARGS"
    printf '  "kernel_config_sha256": "%s",\n' "$KERNEL_CONFIG_SHA256"
    printf '  "debug_features": %s,\n' "$DEBUG_FEATURES"
    echo '  "artifacts": {'
    first=1
    for artifact in fw_payload.bin fw_dynamic.bin Image vmlinux devicetree.dtb root.img root.bin; do
        if [[ -f "$IMAGES_DIR/$artifact" ]]; then
            [[ "$first" -eq 1 ]] || echo ','
            printf '    "%s": "%s"' "$artifact" "$(sha256sum "$IMAGES_DIR/$artifact" | cut -d' ' -f1)"
            first=0
        fi
    done
    echo
    echo '  }'
    echo '}'
} > "$MANIFEST"

print_step "Success! Output images placed in: $IMAGES_DIR"
print_info "Profile manifest: $MANIFEST"
