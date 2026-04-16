#!/usr/bin/env bash
# @file build-linux-image.sh
# @brief Build RISC-V RV32GC Linux images for SimRV
#
# Creates kernel, rootfs, and device tree for Linux boot testing.
# Outputs ready-to-use images in ./linux-images/ directory.
#
# Usage:
#   ./scripts/build-linux-image.sh [--help] [--clean] [--scratch] [--arch rv32|rv64]
#
# Environment variables:
#   LINUX_BUILD_DIR: Build directory (default: ./linux-build)
#   LINUX_IMAGES_DIR: Output directory (default: ./linux-images)
#   RISCV_GNU_TOOLCHAIN_DIR: Toolchain install path (default: /opt/riscv)
#   JOBS: Parallel build jobs (default: $(nproc))

set -euo pipefail

# ============================================================================
# Configuration
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${LINUX_BUILD_DIR:-$ROOT_DIR/linux-build}"
IMAGES_DIR="${LINUX_IMAGES_DIR:-$ROOT_DIR/linux-images}"
TOOLCHAIN_DIR="${RISCV_GNU_TOOLCHAIN_DIR:-/opt/riscv}"
JOBS="${JOBS:-$(nproc)}"
ARCH="${ARCH:-rv32}"
USE_LOCAL_SCRATCH="${SIMRV_USE_LOCAL_SCRATCH:-0}"
SCRATCH_BUILD_DIR="${SIMRV_SCRATCH_BUILD_DIR:-/var/tmp/${USER:-user}/simrv-linux-build}"
DTS_TEMPLATE="${SIMRV_DTS_TEMPLATE:-$SCRIPT_DIR/templates/virt-rv32.dts}"
BOOT_TEMPLATE="${SIMRV_BOOT_TEMPLATE:-$SCRIPT_DIR/templates/linux-boot.S}"

# Versions
LINUX_VERSION="linux-6.1.89"  # LTS kernel with good RV32 support
BUILDROOT_VERSION="${SIMRV_BUILDROOT_VERSION:-buildroot-2026.02}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Flags
CLEAN_BUILD=0

# Selected cross-compiler prefix (for example: riscv64-unknown-linux-gnu-)
TOOLCHAIN_PREFIX=""

# Shared tool names used when wiring shim/bin compatibility links.
TOOLCHAIN_UTILS=(cpp ar as ld nm objcopy objdump ranlib strip readelf gcc-ar gcc-nm gcc-ranlib)

# ============================================================================
# Functions
# ============================================================================

print_header() {
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

print_step() {
    echo -e "${GREEN}[STEP]${NC} $1"
}

print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

safe_remove_dir() {
    local dir="$1"
    local retries="${2:-3}"
    local i
    for i in $(seq 1 "$retries"); do
        if [[ ! -e "$dir" ]]; then
            return 0
        fi
        # Ensure writable bits are set so cleanup does not fail on permission leftovers.
        chmod -R u+w "$dir" 2>/dev/null || true
        if rm -rf "$dir"; then
            return 0
        fi
        print_warn "Cleanup attempt $i/$retries failed for $dir; retrying..."
    done
    return 1
}

detect_toolchain_prefix() {
    local base="${1:-}"
    local candidates=(
        "riscv32-unknown-linux-gnu-"
        "riscv64-unknown-linux-gnu-"
        "riscv64-linux-gnu-"
        "riscv32-unknown-elf-"
        "riscv64-unknown-elf-"
        "riscv64-elf-"
    )

    local prefix
    for prefix in "${candidates[@]}"; do
        if [[ -n "$base" && -x "$base/bin/${prefix}gcc" ]]; then
            TOOLCHAIN_PREFIX="$prefix"
            return 0
        fi
        if command -v "${prefix}gcc" >/dev/null 2>&1; then
            TOOLCHAIN_PREFIX="$prefix"
            return 0
        fi
    done
    return 1
}

resolve_existing_toolchain() {
    if [[ -d "$TOOLCHAIN_DIR/bin" ]]; then
        export PATH="$TOOLCHAIN_DIR/bin:$PATH"
    fi

    if ! select_and_verify_toolchain "$TOOLCHAIN_DIR" && ! select_and_verify_toolchain ""; then
        print_error "RISC-V toolchain not found. Please load/install it before running this script."
        print_info "Hint: module load archlab/riscv"
        print_info "Or set RISCV_GNU_TOOLCHAIN_DIR=/path/to/toolchain"
        return 1
    fi

    return 0
}

select_and_verify_toolchain() {
    if detect_toolchain_prefix "$1"; then
        print_info "Detected toolchain prefix: $TOOLCHAIN_PREFIX"
        verify_toolchain_startup_objects
        return $?
    fi
    return 1
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --help)
                show_help
                exit 0
                ;;
            --clean)
                CLEAN_BUILD=1
                ;;
            --scratch)
                USE_LOCAL_SCRATCH=1
                ;;
            --arch)
                if [[ $# -lt 2 ]]; then
                    print_error "--arch requires rv32 or rv64"
                    exit 1
                fi
                ARCH="$2"
                shift
                ;;
            *)
                print_error "Unknown option: $1"
                show_help
                exit 1
                ;;
        esac
        shift
    done
}

create_toolchain_shim() {
    local shim_path="$1"
    local real_tool="$2"
    local default_flags="$3"

    cat > "$shim_path" <<EOF
#!/usr/bin/env bash
set -euo pipefail
real_tool="$real_tool"
args=("\$@")
needs_march=1
needs_mabi=1
for a in "\${args[@]}"; do
    [[ "\$a" == -march=* ]] && needs_march=0
    [[ "\$a" == -mabi=* ]] && needs_mabi=0
done
for i in "\${!args[@]}"; do
    if [[ "\${args[\$i]}" == "-dumpversion" || "\${args[\$i]}" == "-dumpfullversion" ]]; then
        # Buildroot 2024.02 only models up to external GCC 13.x.
        echo "13.2.0"
        exit 0
    fi
    if [[ "\${args[\$i]}" == "-print-file-name=libc.a" ]]; then
        libc_so="\$("\$real_tool" "\${args[@]/-print-file-name=libc.a/-print-file-name=libc.so}")"
        if [[ "\$libc_so" == libc.so ]]; then
            echo "libc.a"
        else
            echo "\${libc_so%/libc.so}/libc.a"
        fi
        exit 0
    fi
done
extra=()
if [[ \$needs_march -eq 1 ]]; then
    extra+=("${default_flags%% *}")
fi
if [[ \$needs_mabi -eq 1 ]]; then
    extra+=("${default_flags##* }")
fi
exec "\$real_tool" "\${extra[@]}" "\${args[@]}"
EOF
    chmod +x "$shim_path"
}

verify_toolchain_startup_objects() {
    local gcc_path
    local target_flags
    local resolved
    local missing=0

    if ! gcc_path="$(command -v "${TOOLCHAIN_PREFIX}gcc" 2>/dev/null)"; then
        print_error "Cannot verify startup objects because the cross-compiler is not on PATH"
        return 1
    fi

    if [[ "$ARCH" == "rv64" ]]; then
        target_flags="-mabi=lp64d -march=rv64gc"
    else
        target_flags="-mabi=ilp32d -march=rv32gc"
    fi

    for obj in Scrt1.o crti.o crtbeginS.o; do
        resolved="$($gcc_path $target_flags -print-file-name="$obj" 2>/dev/null || true)"
        if [[ -z "$resolved" || "$resolved" == "$obj" || ! -e "$resolved" ]]; then
            print_warn "Compiler could not resolve startup object for $ARCH: $obj"
            missing=1
        fi
    done

    if [[ $missing -ne 0 ]]; then
        print_error "The selected external toolchain cannot resolve startup objects for $ARCH target linking"
        print_error "Buildroot needs startup files like Scrt1.o, crti.o, and crtbeginS.o to link BusyBox"
        print_error "Check toolchain search paths/sysroot and ensure module/installed toolchain is correct"
        return 1
    fi

    return 0
}

show_help() {
    cat <<EOF
Build RISC-V RV32GC Linux images for SimRV

Usage: $0 [OPTIONS]

Options:
  --help              Show this help message
  --clean             Clean build directories before building
    --scratch           Build in local scratch directory (overrides LINUX_BUILD_DIR)
  --arch rv32|rv64    Target architecture (default: rv32)

Environment Variables:
  LINUX_BUILD_DIR         Build directory (default: ./linux-build)
  LINUX_IMAGES_DIR        Output directory (default: ./linux-images)
  RISCV_GNU_TOOLCHAIN_DIR Toolchain install path (default: /opt/riscv; only used if not using module)
  JOBS                    Parallel build jobs (default: nproc)
    SIMRV_USE_LOCAL_SCRATCH  Build in local scratch dir instead of workspace (1=yes)
    SIMRV_SCRATCH_BUILD_DIR  Scratch build directory (default: /var/tmp/$USER/simrv-linux-build)
    SIMRV_SKIP_DOWNLOAD      Skip source download step (1=yes, default=0)
    SIMRV_SKIP_EXTRACT       Skip source extract step (1=yes, default=0)
    SIMRV_SKIP_KERNEL        Skip Linux kernel build step (1=yes, default=0)
    SIMRV_BUILDROOT_ONLY     Run only Buildroot/rootfs stage (1=yes, default=0)

Toolchain Detection:
    Uses an existing toolchain from RISCV_GNU_TOOLCHAIN_DIR/bin or PATH.
    The script does not install/build a toolchain.

Output:
  Generates in $IMAGES_DIR/:
    - vmlinux           Kernel image
  - root.bin          Root filesystem image
  - devicetree.dtb    Device tree blob
  - setup.sh          Environment setup script for SimRV

Example:
  ./scripts/build-linux-image.sh
    LINUX_IMAGES_DIR=./my-images ./scripts/build-linux-image.sh
    module load archlab/riscv && ./scripts/build-linux-image.sh

EOF
}

check_dependencies() {
    print_step "Checking dependencies..."
    
    local missing=0
    local deps=("git" "wget" "make" "gcc" "g++" "bc" "flex" "bison" "openssl")
    
    for dep in "${deps[@]}"; do
        if ! command -v "$dep" >/dev/null 2>&1; then
            print_warn "Missing: $dep"
            missing=1
        fi
    done
    
    if [[ $missing -eq 1 ]]; then
        print_error "Install missing dependencies above, then retry"
        exit 1
    fi
    
    print_info "All dependencies satisfied"
}

setup_directories() {
    print_step "Setting up directories..."
    mkdir -p "$BUILD_DIR" "$IMAGES_DIR"
    print_info "Build dir: $BUILD_DIR"
    print_info "Images dir: $IMAGES_DIR"
}

download_sources() {
    local src_dir="$BUILD_DIR/sources"
    mkdir -p "$src_dir"
    
    print_step "Downloading kernel and buildroot sources..."
    
    if [[ ! -f "$src_dir/$LINUX_VERSION.tar.xz" ]]; then
        print_info "Downloading $LINUX_VERSION..."
        cd "$src_dir"
        wget -q "https://cdn.kernel.org/pub/linux/kernel/v6.x/$LINUX_VERSION.tar.xz"
    fi
    
    if [[ ! -f "$src_dir/$BUILDROOT_VERSION.tar.gz" ]]; then
        print_info "Downloading $BUILDROOT_VERSION..."
        cd "$src_dir"
        wget -q "https://buildroot.org/downloads/$BUILDROOT_VERSION.tar.gz"
    fi
    
    print_info "Sources ready"
}

extract_sources() {
    print_step "Extracting sources..."
    
    cd "$BUILD_DIR"
    
    if [[ ! -d "linux" ]]; then
        tar -xf "$BUILD_DIR/sources/$LINUX_VERSION.tar.xz"
        mv "$LINUX_VERSION" linux
    fi
    
    if [[ ! -d "buildroot" ]]; then
        tar -xf "$BUILD_DIR/sources/$BUILDROOT_VERSION.tar.gz"
        mv "$BUILDROOT_VERSION" buildroot
    fi
    
    print_info "Sources extracted"
}

build_kernel() {
    print_step "Building Linux kernel for $ARCH..."
    
    cd "$BUILD_DIR/linux"
    
    export PATH="$TOOLCHAIN_DIR/bin:$PATH"
    if [[ -z "$TOOLCHAIN_PREFIX" ]]; then
        if ! detect_toolchain_prefix "$TOOLCHAIN_DIR" && ! detect_toolchain_prefix; then
            print_error "No suitable RISC-V cross-compiler found for kernel build"
            return 1
        fi
    fi
    export CROSS_COMPILE="$TOOLCHAIN_PREFIX"
    print_info "Using CROSS_COMPILE=$CROSS_COMPILE"
    if [[ "$CROSS_COMPILE" == *"elf-" ]]; then
        print_warn "Using an ELF toolchain; Linux kernel builds can work, but linux-gnu toolchains are preferred"
    fi
    
    print_info "Configuring kernel..."
    if [[ "$ARCH" == "rv64" ]]; then
        make ARCH=riscv rv64_defconfig
    else
        make ARCH=riscv rv32_defconfig
    fi

    # Optional: customize kernel config
    # make ARCH=riscv menuconfig
    
    print_info "Building kernel..."
    make ARCH=riscv -j"$JOBS" clean
    make ARCH=riscv -j"$JOBS" vmlinux
    
    print_info "Kernel build complete"
}

build_rootfs_buildroot() {
    print_step "Building rootfs with Buildroot..."
    
    cd "$BUILD_DIR/buildroot"
    
    export PATH="$TOOLCHAIN_DIR/bin:$PATH"

    # If Buildroot was previously configured for an internal toolchain,
    # stale output can keep reusing uClibc artifacts. Remove the output tree
    # so the new external-toolchain config starts from a clean state.
    if [[ -d output ]]; then
        print_info "Cleaning stale Buildroot output tree..."
        # Avoid races when files are being updated: rename first, then delete.
        local stale_output
        stale_output="output.stale.$(date +%s).$$"
        if mv output "$stale_output"; then
            if ! safe_remove_dir "$stale_output" 5; then
                print_error "Failed to remove stale Buildroot tree: $stale_output"
                print_error "Another process may still be writing there. Stop active builds and retry."
                return 1
            fi
        else
            print_warn "Could not rename output tree; attempting direct removal"
            if ! safe_remove_dir output 5; then
                print_error "Failed to remove Buildroot output tree"
                print_error "Another process may still be writing there. Stop active builds and retry."
                return 1
            fi
        fi
    fi
    
    # Create minimal Buildroot config
    print_info "Creating minimal RV32 buildroot config..."

    local br2_config="$BUILD_DIR/buildroot/.config"
    local toolchain_prefix="${TOOLCHAIN_PREFIX:-riscv64-unknown-linux-gnu-}"
    local buildroot_prefix="${toolchain_prefix%-}"
    local compiler_path=""
    local toolchain_path=""
    local real_toolchain_root=""
    local real_toolchain_bin=""
    local shim_root="$BUILD_DIR/buildroot-toolchain-shim"
    local shim_bin="$shim_root/bin"
    local busybox_fragment="$BUILD_DIR/buildroot/busybox-simrv.fragment"
    local toolchain_headers_cfg="BR2_TOOLCHAIN_EXTERNAL_HEADERS_6_6=y"
    local toolchain_headers_latest_cfg="BR2_TOOLCHAIN_HEADERS_LATEST=y"
    local toolchain_openmp_cfg="# BR2_TOOLCHAIN_EXTERNAL_OPENMP is not set"
    local toolchain_ssp_cfg="# BR2_TOOLCHAIN_EXTERNAL_HAS_SSP is not set"
    local toolchain_ssp_strong_cfg="# BR2_TOOLCHAIN_EXTERNAL_HAS_SSP_STRONG is not set"
    local shim_default_flags=""
    local br2_arch_size="BR2_RISCV_32=y"
    local br2_abi="BR2_RISCV_ABI_ILP32D=y"

    if [[ "$ARCH" == "rv64" ]]; then
        br2_arch_size="BR2_RISCV_64=y"
        br2_abi="BR2_RISCV_ABI_LP64D=y"
        shim_default_flags="-march=rv64gc -mabi=lp64d"
    else
        shim_default_flags="-march=rv32gc -mabi=ilp32d"
    fi

    # Buildroot expects prefix without trailing '-' and a concrete toolchain path.
    if command -v "${toolchain_prefix}gcc" >/dev/null 2>&1; then
        compiler_path="$(command -v "${toolchain_prefix}gcc")"
        toolchain_path="$(cd "$(dirname "$compiler_path")/.." && pwd)"
    elif command -v "${buildroot_prefix}-gcc" >/dev/null 2>&1; then
        compiler_path="$(command -v "${buildroot_prefix}-gcc")"
        toolchain_path="$(cd "$(dirname "$compiler_path")/.." && pwd)"
    elif [[ -x "$TOOLCHAIN_DIR/bin/${buildroot_prefix}-gcc" ]]; then
        compiler_path="$TOOLCHAIN_DIR/bin/${buildroot_prefix}-gcc"
        toolchain_path="$TOOLCHAIN_DIR"
    else
        print_error "Cannot locate cross-compiler for Buildroot external toolchain (${buildroot_prefix}-gcc)"
        return 1
    fi

    real_toolchain_root="$toolchain_path"
    real_toolchain_bin="$real_toolchain_root/bin"

    # Detect toolchain headers version from sysroot and select a matching
    # Buildroot kernel-header series from the active Buildroot source tree.
    local real_cc="$real_toolchain_bin/${buildroot_prefix}-gcc"
    local real_sysroot=""
    local version_h=""
    local linux_version_code=""
    local hdr_major=""
    local hdr_minor=""
    local hdr_target_num=""
    local headers_options_file="$BUILD_DIR/buildroot/toolchain/toolchain-external/toolchain-external-custom/Config.in.options"
    local best_headers_token=""
    local best_headers_num=-1
    local max_headers_token=""
    local max_headers_num=-1
    local t_major=""
    local t_minor=""
    local t_num=""
    if [[ -x "$real_cc" ]]; then
        real_sysroot="$($real_cc -print-sysroot 2>/dev/null || true)"
    fi
    if [[ -n "$real_sysroot" && -f "$real_sysroot/usr/include/linux/version.h" ]]; then
        version_h="$real_sysroot/usr/include/linux/version.h"
    elif [[ -f "$real_toolchain_root/sysroot/usr/include/linux/version.h" ]]; then
        version_h="$real_toolchain_root/sysroot/usr/include/linux/version.h"
    fi
    if [[ -n "$version_h" ]]; then
        linux_version_code="$(awk '/^#define[[:space:]]+LINUX_VERSION_CODE[[:space:]]+[0-9]+/{print $3; exit}' "$version_h")"
        if [[ -n "$linux_version_code" ]]; then
            hdr_major="$((linux_version_code >> 16))"
            hdr_minor="$(((linux_version_code >> 8) & 0xFF))"
            hdr_target_num="$((hdr_major * 1000 + hdr_minor))"

            if [[ -f "$headers_options_file" ]]; then
                while read -r token; do
                    t_major="${token%%_*}"
                    t_minor="${token#*_}"
                    t_num="$((t_major * 1000 + t_minor))"
                    if (( t_num > max_headers_num )); then
                        max_headers_num=$t_num
                        max_headers_token="$token"
                    fi
                    if (( t_num <= hdr_target_num && t_num > best_headers_num )); then
                        best_headers_num=$t_num
                        best_headers_token="$token"
                    fi
                done < <(grep -oE 'BR2_TOOLCHAIN_EXTERNAL_HEADERS_[0-9_]+$' "$headers_options_file" | sed 's/BR2_TOOLCHAIN_EXTERNAL_HEADERS_//' | sort -u)
            fi

            if [[ -n "$best_headers_token" ]]; then
                toolchain_headers_cfg="BR2_TOOLCHAIN_EXTERNAL_HEADERS_${best_headers_token}=y"
                toolchain_headers_latest_cfg="# BR2_TOOLCHAIN_HEADERS_LATEST is not set"
            elif [[ -n "$max_headers_token" ]]; then
                toolchain_headers_cfg="BR2_TOOLCHAIN_EXTERNAL_HEADERS_${max_headers_token}=y"
                toolchain_headers_latest_cfg="BR2_TOOLCHAIN_HEADERS_LATEST=y"
            fi

            print_info "Detected toolchain headers: ${hdr_major}.${hdr_minor}.x"
            print_info "Using Buildroot header selector: ${toolchain_headers_cfg%\=y}"
        fi
    fi

    # Buildroot enforces parity for external toolchain capabilities.
    # Probe the active compiler and mirror those features in generated BR2 config.
    local feature_test_obj="$BUILD_DIR/.br-toolchain-feature-test.$$"
    if [[ -x "$real_cc" ]]; then
        if printf '#include <omp.h>\nint main(void) { return omp_get_thread_num(); }\n' | \
            "$real_cc" $shim_default_flags -fopenmp -x c -o "$feature_test_obj" - >/dev/null 2>&1; then
            toolchain_openmp_cfg="BR2_TOOLCHAIN_EXTERNAL_OPENMP=y"
        fi

        if echo 'int main(void) { return 0; }' | \
            "$real_cc" $shim_default_flags -Werror -fstack-protector -x c -o "$feature_test_obj" - >/dev/null 2>&1; then
            toolchain_ssp_cfg="BR2_TOOLCHAIN_EXTERNAL_HAS_SSP=y"
            if echo 'int main(void) { return 0; }' | \
                "$real_cc" $shim_default_flags -Werror -fstack-protector-strong -x c -o "$feature_test_obj" - >/dev/null 2>&1; then
                toolchain_ssp_strong_cfg="BR2_TOOLCHAIN_EXTERNAL_HAS_SSP_STRONG=y"
            fi
        fi
    fi
    rm -f "$feature_test_obj" "$feature_test_obj".*

    # Buildroot rejects toolchains when "-print-file-name=libc.a" returns the literal
    # token "libc.a" (common for shared-only sysroots). Provide a local shim that
    # maps that probe to libc.so while forwarding all other invocations unchanged.
    rm -rf "$shim_root"
    mkdir -p "$shim_bin"

    create_toolchain_shim "$shim_bin/${buildroot_prefix}-gcc" "${real_toolchain_bin}/${buildroot_prefix}-gcc" "$shim_default_flags"
    create_toolchain_shim "$shim_bin/${buildroot_prefix}-g++" "${real_toolchain_bin}/${buildroot_prefix}-g++" "$shim_default_flags"

    # Link remaining toolchain utilities directly to the real toolchain.
    local t
    for t in "${TOOLCHAIN_UTILS[@]}"; do
        if [[ -x "$real_toolchain_bin/${buildroot_prefix}-$t" ]]; then
            ln -sf "$real_toolchain_bin/${buildroot_prefix}-$t" "$shim_bin/${buildroot_prefix}-$t"
        fi
    done

    toolchain_path="$shim_root"

    print_info "Buildroot external compiler: $compiler_path"
    print_info "Buildroot external path: $toolchain_path"

    cat > "$br2_config" << 'BUILDROOT_CONFIG'
BR2_riscv=y
BR2_riscv_g=y
BR2_RISCV_ISA_RVC=y
BR2_PACKAGE_BUSYBOX=y
BR2_SYSTEM_DHCP="eth0"
BR2_TARGET_OPTIMIZATION="-O2 -g0 -march=rv32gc -mabi=ilp32d"
BR2_TARGET_GENERIC_REMOUNT_ROOT_FS=y
BR2_TARGET_ROOTFS_TAR=y
BR2_TARGET_ROOTFS_TAR_GZIP=y
BR2_TARGET_ROOTFS_EXT2=y
BR2_TARGET_ROOTFS_EXT2_SIZE="128M"
BR2_PACKAGE_LINUX_TOOLS_CPUPOWER=n
# BR2_PACKAGE_IFUPDOWN_SCRIPTS is not set
# BR2_PACKAGE_URANDOM_SCRIPTS is not set
BUILDROOT_CONFIG

    cat >> "$br2_config" <<BUILDROOT_ARCH
${br2_arch_size}
${br2_abi}
BUILDROOT_ARCH

    cat >> "$br2_config" <<BUILDROOT_EXTERNAL
BR2_TOOLCHAIN_EXTERNAL=y
BR2_TOOLCHAIN_EXTERNAL_CUSTOM=y
BR2_TOOLCHAIN_EXTERNAL_PREINSTALLED=y
BR2_TOOLCHAIN_EXTERNAL_PATH="${toolchain_path}"
BR2_TOOLCHAIN_EXTERNAL_CUSTOM_PREFIX="${buildroot_prefix}"
BR2_TOOLCHAIN_EXTERNAL_CUSTOM_GLIBC=y
BR2_TOOLCHAIN_EXTERNAL_CXX=y
# BR2_TOOLCHAIN_EXTERNAL_INET_RPC is not set
${toolchain_ssp_cfg}
${toolchain_ssp_strong_cfg}
${toolchain_openmp_cfg}
# BR2_TOOLCHAIN_EXTERNAL_FORTRAN is not set
# BR2_TOOLCHAIN_EXTERNAL_DLANG is not set
BR2_TOOLCHAIN_EXTERNAL_GCC_13=y
${toolchain_headers_cfg}
${toolchain_headers_latest_cfg}
BUILDROOT_EXTERNAL

    cat > "$busybox_fragment" <<'BUSYBOX_FRAGMENT'
# Keep the rootfs minimal for SimRV and avoid optional BusyBox applets
# that require extra kernel/userland features we do not need.
# CONFIG_TC is not set
BUSYBOX_FRAGMENT

    cat >> "$br2_config" <<BUILDROOT_BUSYBOX
BR2_PACKAGE_BUSYBOX_CONFIG_FRAGMENT_FILES="$busybox_fragment"
BUILDROOT_BUSYBOX

    # Buildroot requires a configured tree before build.
    # Normalize/complete the generated config first.
    print_info "Finalizing Buildroot configuration..."
    if ! make BR2_CONFIG="$br2_config" olddefconfig >/dev/null; then
        print_error "Buildroot configuration failed (olddefconfig)"
        return 1
    fi

    # BusyBox and other packages may invoke cross-tool helpers from
    # output/host/bin directly. Seed compatibility links so gcc-ar/gcc-nm/
    # gcc-ranlib resolve even when using an external preinstalled toolchain.
    local br_host_bin="$BUILD_DIR/buildroot/output/host/bin"
    mkdir -p "$br_host_bin"
    for t in gcc g++ "${TOOLCHAIN_UTILS[@]}"; do
        if [[ -x "$shim_bin/${buildroot_prefix}-$t" ]]; then
            ln -sf "$shim_bin/${buildroot_prefix}-$t" "$br_host_bin/${buildroot_prefix}-$t"
        fi
    done

    print_info "Building rootfs..."
    make BR2_CONFIG="$br2_config" -j"$JOBS"
    
    print_info "Rootfs build complete"
}

create_images() {
    print_step "Creating SimRV-compatible images..."
    
    export PATH="$TOOLCHAIN_DIR/bin:$PATH"
    
    local linux_img="$BUILD_DIR/linux/arch/riscv/boot/Image"
    local rootfs_tar="$BUILD_DIR/buildroot/output/images/rootfs.tar.gz"
    local rootfs_ext2="$BUILD_DIR/buildroot/output/images/rootfs.ext2"
    local bootloader_asm="$BUILD_DIR/linux-boot.S"
    local bootloader_elf="$BUILD_DIR/linux-boot.elf"
    local bootloader_bin="$BUILD_DIR/linux-boot.bin"
    local boot_offset=4194304
    local combined_img="$IMAGES_DIR/vmlinux"
    
    # Copy rootfs as disk image
    if [[ -f "$rootfs_ext2" ]]; then
        print_info "Using ext2 rootfs image"
        cp "$rootfs_ext2" "$IMAGES_DIR/root.bin"
    elif [[ -f "$rootfs_tar" ]]; then
        print_warn "ext2 rootfs not found, using tar.gz (may need manual conversion)"
        cp "$rootfs_tar" "$IMAGES_DIR/root.tar.gz"
    fi
    
    # Build a tiny firmware stub that hands control to Linux in supervisor mode.
    if [[ -f "$BOOT_TEMPLATE" && -f "$linux_img" ]]; then
        print_info "Building boot firmware stub..."
        cp "$BOOT_TEMPLATE" "$bootloader_asm"
        local gcc_bin
        gcc_bin="$(command -v "${TOOLCHAIN_PREFIX}gcc" 2>/dev/null || true)"
        local objcopy_bin
        objcopy_bin="$(command -v "${TOOLCHAIN_PREFIX}objcopy" 2>/dev/null || true)"
        if [[ -z "$gcc_bin" || -z "$objcopy_bin" ]]; then
            print_error "Cannot find RISC-V compiler tools for firmware stub generation"
            return 1
        fi
        "$gcc_bin" -march=rv32gc -mabi=ilp32d -nostdlib -nostartfiles -Wl,-N -Wl,--build-id=none \
            -Ttext=0x80000000 -o "$bootloader_elf" "$bootloader_asm"
        "$objcopy_bin" -O binary "$bootloader_elf" "$bootloader_bin"

        print_info "Creating combined Linux boot image..."
        cp "$bootloader_bin" "$combined_img"
        truncate -s "$boot_offset" "$combined_img"
        cat "$linux_img" >> "$combined_img"
        print_info "Combined boot image: $combined_img"
    elif [[ -f "$linux_img" ]]; then
        print_warn "Boot firmware template missing; copying kernel image only"
        cp "$linux_img" "$combined_img"
    fi
    
    # Create device tree blob
    print_info "Creating device tree blob..."
    create_device_tree
    
    print_info "Images created in $IMAGES_DIR/"
}

create_device_tree() {
    if [[ ! -f "$DTS_TEMPLATE" ]]; then
        print_error "Device-tree template not found: $DTS_TEMPLATE"
        return 1
    fi
    cp "$DTS_TEMPLATE" "$IMAGES_DIR/virt.dts"
    
    # Compile device tree
    if command -v dtc >/dev/null 2>&1; then
        dtc -I dts -O dtb -o "$IMAGES_DIR/devicetree.dtb" "$IMAGES_DIR/virt.dts"
        print_info "Device tree compiled to devicetree.dtb"
    else
        print_warn "dtc not found; device tree source saved as virt.dts (requires manual compilation)"
    fi
}

create_setup_script() {
    print_step "Creating environment setup script..."
    
    cat > "$IMAGES_DIR/setup.sh" << SETUP_EOF
#!/usr/bin/env bash
# Setup environment for SimRV Linux boot testing
# Generated by build-linux-image.sh

IMAGES_DIR="\$(cd "\$(dirname "\${BASH_SOURCE[0]}")" && pwd)"

# Use the combined boot image as memory image
export SIMRV_LINUX_MEM_IMG="\$IMAGES_DIR/vmlinux"

# Use ext2 rootfs as disk image
export SIMRV_LINUX_DISK_IMG="\$IMAGES_DIR/root.bin"

# Device tree (compile if needed)
if [[ -f "\$IMAGES_DIR/devicetree.dtb" ]]; then
    export SIMRV_LINUX_DTB="\$IMAGES_DIR/devicetree.dtb"
else
    export SIMRV_LINUX_DTB=""
fi

echo "SimRV Linux Image Setup"
echo "├─ Memory image:  \$SIMRV_LINUX_MEM_IMG"
echo "├─ Disk image:    \$SIMRV_LINUX_DISK_IMG"
echo "└─ Device tree:   \${SIMRV_LINUX_DTB:-(not compiled)}"
echo
echo "To use with phase2-gate.sh:"
echo "  source \$IMAGES_DIR/setup.sh"
echo "  bash scripts/phase2-gate.sh"
echo
echo "Or use directly with SimRV simulator:"
echo "  \$SIMRV_BIN -m \$SIMRV_LINUX_MEM_IMG -d \$SIMRV_LINUX_DISK_IMG"
SETUP_EOF
    
    chmod +x "$IMAGES_DIR/setup.sh"
    print_info "Setup script created: setup.sh"
}

show_summary() {
    print_header "Build Complete!"
    echo
    echo "Linux images ready for SimRV:"
    echo
    echo "  Output directory: $IMAGES_DIR"
    echo
    echo "  Files generated:"
    ls -lh "$IMAGES_DIR"/ 2>/dev/null | tail -n +2 | awk '{printf "    %-30s %8s\n", $9, $5}'
    echo
    echo "To use with SimRV:"
    echo "  source $IMAGES_DIR/setup.sh"
    echo "  ./build/ninja-clang-release/SimRV -m \$SIMRV_LINUX_MEM_IMG -d \$SIMRV_LINUX_DISK_IMG -c \$SIMRV_LINUX_DTB"
    echo
    echo "To run phase2-gate validation:"
    echo "  source $IMAGES_DIR/setup.sh"
    echo "  bash scripts/phase2-gate.sh"
    echo
}

# ============================================================================
# Main
# ============================================================================

main() {
    local skip_download="${SIMRV_SKIP_DOWNLOAD:-0}"
    local skip_extract="${SIMRV_SKIP_EXTRACT:-0}"
    local skip_kernel="${SIMRV_SKIP_KERNEL:-0}"
    local buildroot_only="${SIMRV_BUILDROOT_ONLY:-0}"

    print_header "SimRV RV32 Linux Image Builder"
    echo "Architecture: $ARCH"
    echo "Build directory: $BUILD_DIR"
    echo "Images directory: $IMAGES_DIR"
    echo "Toolchain: $TOOLCHAIN_DIR"
    echo "Parallel jobs: $JOBS"
    echo
    
    parse_args "$@"

    if [[ "$USE_LOCAL_SCRATCH" == "1" ]]; then
        BUILD_DIR="$SCRATCH_BUILD_DIR"
        print_info "Using local scratch build dir: $BUILD_DIR"
    fi
    
    # Clean if requested
    if [[ $CLEAN_BUILD -eq 1 ]]; then
        print_step "Cleaning previous build..."
        rm -rf "$BUILD_DIR"
    fi
    
    # Build pipeline
    check_dependencies
    setup_directories
    resolve_existing_toolchain

    if [[ "$skip_download" == "1" ]]; then
        print_info "Skipping source download (SIMRV_SKIP_DOWNLOAD=1)"
    else
        download_sources
    fi

    if [[ "$skip_extract" == "1" ]]; then
        print_info "Skipping source extract (SIMRV_SKIP_EXTRACT=1)"
    elif [[ "$buildroot_only" == "1" && -d "$BUILD_DIR/buildroot" ]]; then
        print_info "Skipping source extract (SIMRV_BUILDROOT_ONLY=1 and buildroot source exists)"
    else
        extract_sources
    fi

    if [[ "$skip_kernel" == "1" || "$buildroot_only" == "1" ]]; then
        print_info "Skipping kernel build (SIMRV_SKIP_KERNEL=1 or SIMRV_BUILDROOT_ONLY=1)"
    else
        build_kernel
    fi

    build_rootfs_buildroot

    if [[ "$buildroot_only" == "1" ]]; then
        print_header "Buildroot Stage Complete"
        echo "Buildroot/rootfs stage finished (SIMRV_BUILDROOT_ONLY=1)."
        echo "Kernel/image packaging steps were skipped."
        return 0
    fi

    create_images
    create_setup_script
    show_summary
}

main "$@"
