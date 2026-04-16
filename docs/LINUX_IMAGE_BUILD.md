# Building Linux Images for SimRV

This guide explains how to build RISC-V RV32GC Linux kernel and rootfs images for SimRV testing.

## Overview

SimRV supports Linux OS boot testing via the `phase2-gate` validation target. To enable this, you need:

- **SIMRV_LINUX_MEM_IMG**: Kernel image (Berkeley Boot Loader + Linux kernel)
- **SIMRV_LINUX_DISK_IMG**: Root filesystem image
- **SIMRV_LINUX_DTB** (optional): Device tree blob

The [build-linux-image.sh](../scripts/build-linux-image.sh) script automates the entire process.

## Quick Start

### One-Command Build

```bash
./scripts/build-linux-image.sh
```

This will:

1. ✅ Automatically load `archlab/riscv` module (or use fallback toolchain)
2. ✅ Download Linux kernel and Buildroot sources
3. ✅ Build kernel and rootfs
4. ✅ Create compatible images in `./linux-images/`

**Time estimate**:

- With module system: ~15-25 min ⚡ (fastest)
- With pre-installed toolchain: ~10-15 min
- Building toolchain from scratch: ~25-40 min (first run only)

### Setup Environment

After building, initialize the test environment:

```bash
source ./linux-images/setup.sh
```

This exports:

- `SIMRV_LINUX_MEM_IMG`
- `SIMRV_LINUX_DISK_IMG`
- `SIMRV_LINUX_DTB`

### Run Linux Boot Test

```bash
source ./linux-images/setup.sh
./build/ninja-clang-release/SimRV -m $SIMRV_LINUX_MEM_IMG -d $SIMRV_LINUX_DISK_IMG -c $SIMRV_LINUX_DTB
```

Or run the full Phase 2 validation gate:

```bash
source ./linux-images/setup.sh
cmake --build --preset ninja-clang-release --target phase2-gate
```

## Building Variants

### Prerequisites: Module System or Toolchain

**With module system (RECOMMENDED):**

```bash
module load archlab/riscv
./scripts/build-linux-image.sh
```

**With pre-installed toolchain:**

```bash
RISCV_GNU_TOOLCHAIN_DIR=/path/to/toolchain ./scripts/build-linux-image.sh
```

### Clean Rebuild

```bash
./scripts/build-linux-image.sh --clean
```

Deletes previous build and starts fresh.

### Toolchain Selection

If toolchain is already in PATH (e.g., module loaded):

```bash
module load archlab/riscv
./scripts/build-linux-image.sh
```

Or with environment variable:

```bash
RISCV_GNU_TOOLCHAIN_DIR=/opt/riscv ./scripts/build-linux-image.sh
```

### Parallel Jobs

Control build parallelism:

```bash
JOBS=4 ./scripts/build-linux-image.sh
```

Default is `$(nproc)` (number of CPU cores).

## Architecture Options

RV32GC is the default (matching SimRV's current MISA profile). For future RV64GC support:

```bash
./scripts/build-linux-image.sh --arch rv64
```

## What Gets Built

### Directory Structure

```
linux-build/
├── sources/
│   ├── linux-6.1.89.tar.xz
│   └── buildroot-2026.02.tar.gz
├── linux/
│   ├── arch/riscv/boot/Image
│   └── ...
├── buildroot/
│   ├── output/images/rootfs.ext2
│   └── ...
└── riscv-gnu-toolchain/
    └── bin/riscv32-unknown-linux-gnu-*

linux-images/
├── vmlinux                 # Kernel image
├── root.bin                # Root filesystem
├── devicetree.dtb          # Device tree blob
├── virt.dts                # Device tree source
└── setup.sh                # Environment setup
```

### Output Components

#### vmlinux (Kernel Image)

Linux kernel executable image loaded into memory and executed by SimRV.

#### root.bin (Root Filesystem)

Minimal ext2 filesystem containing:

- BusyBox utilities
- Essential libraries (glibc)
- Basic device nodes
- Init system

#### devicetree.dtb (Device Tree)

Describes the simulated hardware (CPU, memory, peripherals) to Linux. Includes:

- CPU core (RV32GC)
- 512MB memory mapping
- UART serial console
- VirtIO disk controller
- PLIC interrupt controller
- CLINT timer

## Prerequisites

The build script checks for required system tools and automatically attempts to load the `archlab/riscv` module.

**Automatic detection:**

- ✅ Module system: `module load archlab/riscv` (automatic)
- ✅ Fallback: Pre-installed toolchain at `/opt/riscv`

**System build tools required:**

```
build-essential, flex, bison, bc, libssl-dev, git, wget, texinfo
```

### Ubuntu/Debian

For most use cases (using module system):

```bash
sudo apt-get install -y build-essential flex bison bc libssl-dev git wget device-tree-compiler
```

Combined (all-in-one):

```bash
sudo apt-get install -y \
   build-essential flex bison bc libssl-dev git wget texinfo device-tree-compiler
```

### macOS (Homebrew)

For most use cases:

```bash
brew install flex bison libssl@1.1 git wget
```

### With archlab module system (RECOMMENDED)

**No additional toolchain installation needed** – the script will automatically detect and load `archlab/riscv`.

```bash
module load archlab/riscv
./scripts/build-linux-image.sh
```

This uses the module-provided multilib compiler.

## Troubleshooting

### Build Fails: "Command not found: riscv32-unknown-linux-gnu-gcc"

**Solution**: The toolchain wasn't installed correctly. Verify:

```bash
ls /opt/riscv/bin/riscv32-unknown-* | head -5
```

If empty, or if using module system:

```bash
module load archlab/riscv
which riscv32-unknown-linux-gnu-gcc
./scripts/build-linux-image.sh
```

### Build Fails: "Cannot find sources"

**Solution**: Internet connectivity or download issues. Clear cache and retry:

```bash
rm -f linux-build/sources/*
./scripts/build-linux-image.sh
```

### Images Directory Empty

**Solution**: Verify build completed successfully:

```bash
ls -la linux-build/buildroot/output/images/
ls -la linux-build/linux/arch/riscv/boot/
```

If files exist but not in `linux-images/`, the copy step may have failed. Check script output for errors.

### dtc not found warning

**Solution**: Install device-tree-compiler:

```bash
sudo apt-get install device-tree-compiler
```

The `.dts` source is still created; you can compile manually:

```bash
dtc -I dts -O dtb -o linux-images/devicetree.dtb linux-images/virt.dts
```

## Customization

### Kernel Configuration

To customize kernel features (modules, drivers, etc.):

1. Start build: `./scripts/build-linux-image.sh`
2. Stop when prompted (or add `--clean` first to reset)
3. Edit the script around the `make menuconfig` line (currently commented out)
4. Uncomment and rerun to enable interactive kernel config

### Rootfs Contents

Edit the Buildroot config in `create_rootfs_buildroot()` function:

```bash
# Add to buildroot .config:
BR2_PACKAGE_OPENSSH=y
BR2_PACKAGE_CURL=y
```

Rebuild with:

```bash
./scripts/build-linux-image.sh --clean
```

## Testing with Phase 2 Gate

The generated images integrate seamlessly with the comprehensive validation gate:

```bash
# Setup environment
source linux-images/setup.sh

# Run all tests (includes Linux boot)
cmake --build --preset ninja-clang-release --target phase2-gate
```

Expected output excerpt:

```
PHASE 4: Linux Boot Test (Optional)
Linux kernel boot test (1000000 cycle limit)...
✅ [PASS] Linux boot test
```

## Performance Notes

- **First build**: ~25-40 min (kernel + Buildroot rootfs; no internal toolchain build when using `archlab/riscv`)
- **Subsequent builds**: ~5-10 min
- **Parallel speedup**: Near-linear with CPU cores (use `JOBS=`)
- **Disk space**: ~3-5 GB total

## References

- [RISC-V GNU Toolchain](https://github.com/riscv-collab/riscv-gnu-toolchain)
- [Linux RISC-V Support](https://kernel.org/)
- [Buildroot Documentation](https://buildroot.org/)
- [SimRV Architecture](./ARCHITECTURE.md)
- [Phase 2 Validation](../README.md#rv64gc-migration-tracking)

## Next Steps

After images are ready:

1. ✅ Environment setup: `source linux-images/setup.sh`
2. ✅ Manual boot test: `./build/ninja-clang-release/SimRV -m $SIMRV_LINUX_MEM_IMG ...`
3. ✅ Full validation: `cmake --build --preset ninja-clang-release --target phase2-gate`
4. ✅ Performance baseline: See `benchmark_logs/` after phase2-gate runs
