# Building Linux Images for SimRV

This guide explains how to build RISC-V RV32GC (or RV64GC) Linux kernel and rootfs
images for SimRV testing.

## Overview

SimRV supports full Linux OS boot as part of its integration validation gate. You need:

- **`SIMRV_LINUX_MEM_IMG`**: Firmware payload image (BBL + Linux kernel)
- **`SIMRV_LINUX_DISK_IMG`**: Root filesystem image
- **`SIMRV_LINUX_DTB`** (optional): Device tree blob

Pre-built images for both RV32 and RV64 should be placed under
`linux-images/rv32/` and `linux-images/rv64/` respectively. The
[`build-linux-image.sh`](https://github.com/archlab-sciencetokyo/SimRV/blob/dev/scripts/build-linux-image.sh) script automates
building them from source.

---

## Quick Start

### One-Command Build

```bash
# Default build (auto-detects musl / glibc toolchain)
./scripts/build-linux-image.sh

# Build with explicit C library target & cross compiler prefix
./scripts/build-linux-image.sh --libc musl --cross-compile riscv64-unknown-linux-musl-
./scripts/build-linux-image.sh --libc glibc --cross-compile riscv64-unknown-linux-gnu-
```

This will:

1. ✅ Check for a pre-installed RISC-V GNU toolchain (or build one from source)
2. ✅ Download Linux kernel and Buildroot sources
3. ✅ Build kernel and rootfs
4. ✅ Create compatible images in `./linux-images/rv32/`

**Time estimate:**

| Scenario | First Build | Subsequent |
| :--- | :--- | :--- |
| With pre-installed toolchain | ~10–15 min | ~5–10 min |
| Building toolchain from scratch | ~25–40 min | ~5–10 min |

### Setup Environment

After building, export the image paths:

```bash
source ./linux-images/rv32/setup.sh
```

This exports `SIMRV_LINUX_MEM_IMG`, `SIMRV_LINUX_DISK_IMG`, and `SIMRV_LINUX_DTB`.

### Guest Lifecycle Management

SimRV supports clean guest shutdown and reboot via standard OpenSBI SBI reset services and the SiFive test-finisher MMIO device (`0x00100000`):

```sh
poweroff   # Syncs filesystems, stops services, and halts SimRV
reboot     # Cleanly restarts the guest system
```

### Run Linux Boot Test

**Direct invocation:**

```bash
source ./linux-images/rv32/setup.sh
./build/rv32-release/simrv \
    -m $SIMRV_LINUX_MEM_IMG \
    -D $SIMRV_LINUX_DISK_IMG \
    --fdt $SIMRV_LINUX_DTB \
    --cli
```

**TUI mode:**

```bash
cmake --build --preset rv32-release --target run-tui
```

**Standard console mode:**

```bash
cmake --build --preset rv32-release --target run-linux
```

**Full integration gate (includes Linux boot test):**

```bash
source ./linux-images/rv32/setup.sh
cmake --build --preset rv32-release --target integration-gate
```

---

## Prerequisites

**Required system packages:**

### Ubuntu / Debian

```bash
sudo apt-get install -y \
    build-essential flex bison bc libssl-dev \
    git wget texinfo device-tree-compiler
```

### Fedora / RHEL

```bash
sudo dnf install -y \
    gcc gcc-c++ clang cmake ninja-build make ccache \
    flex bison bc perl python3 git wget curl \
    tar xz bzip2 cpio fakeroot patch rsync dtc dwarves \
    openssl openssl-devel elfutils-libelf-devel ncurses-devel \
    zlib-devel libzstd-devel e2fsprogs \
    gcc-riscv64-linux-gnu gcc-c++-riscv64-linux-gnu \
    binutils-riscv64-linux-gnu
```

`e2fsprogs` provides `mkfs.ext4`, which is required when packaging the RV64
Alpine root disk. Fedora's RISC-V GCC packages provide a kernel-capable cross
compiler and binutils, but do **not** provide a target glibc sysroot. They
therefore cannot statically link the RV32 BusyBox rootfs by themselves.

---

## Toolchain Setup

The build script needs a RISC-V cross-compilation toolchain. You have two options:

### Option A: Pre-installed Toolchain (Recommended)

Install complete Linux-targeting
[riscv-gnu-toolchain](https://github.com/riscv-collab/riscv-gnu-toolchain)
builds with glibc. A locally built multilib toolchain can serve both targets;
the official release archives use separate RV32 and RV64 prefixes. Verify that
each compiler resolves its target startup files before building:

```bash
export PATH=/opt/riscv/linux-glibc-rv64/bin:/opt/riscv/linux-glibc-rv32/bin:$PATH
riscv64-unknown-linux-gnu-gcc -march=rv64gc -mabi=lp64d -print-file-name=crt1.o
riscv32-unknown-linux-gnu-gcc -march=rv32gc -mabi=ilp32d -print-file-name=crt1.o

./scripts/build-linux-image.sh --arch rv32 \
    --cross-compile riscv32-unknown-linux-gnu-
./scripts/build-linux-image.sh --arch rv64 \
    --cross-compile riscv64-unknown-linux-gnu-
```

Each command must print an existing target path rather than the bare string
`crt1.o`.

### Option B: Build Toolchain from Source

Install the additional toolchain-build prerequisites, then build a multilib
Linux/glibc toolchain:

```bash
sudo dnf install -y \
    autoconf automake libmpc-devel mpfr-devel gmp-devel gawk \
    texinfo patchutils expat-devel libslirp-devel meson

git clone https://github.com/riscv-collab/riscv-gnu-toolchain
cd riscv-gnu-toolchain
./configure --prefix=/opt/riscv --enable-linux --enable-multilib
make linux -j"$(nproc)"
```

The source checkout and build require several gigabytes. The image script does
not build the cross-toolchain automatically.

### Building on Scratch Storage

Keep large kernel trees, root filesystems, and generated images off the source
filesystem by setting both supported roots:

```bash
export SIMRV_LINUX_BUILD_DIR=/scratch/$USER/simrv-linux/build
export SIMRV_LINUX_IMAGES_ROOT=/scratch/$USER/simrv-linux/images
mkdir -p "$SIMRV_LINUX_BUILD_DIR" "$SIMRV_LINUX_IMAGES_ROOT"

./scripts/build-linux-image.sh --arch rv32 \
    --cross-compile riscv32-unknown-linux-gnu-
./scripts/build-linux-image.sh --arch rv64 \
    --cross-compile riscv64-unknown-linux-gnu-
```

---

## Building Variants

### Clean Rebuild

```bash
./scripts/build-linux-image.sh --clean
```

Deletes the previous build and starts fresh.

### Parallel Jobs

Control build parallelism:

```bash
JOBS=4 ./scripts/build-linux-image.sh
```

Default is `$(nproc)` (number of CPU cores).

### Architecture Selection

RV32GC is the default. For RV64GC:

```bash
./scripts/build-linux-image.sh --arch rv64
```

The CMake `linux-images` target uses the `SIMRV_XLEN` preset automatically:

```bash
cmake --build --preset rv32-release --target linux-images
cmake --build --preset rv64-release --target linux-images
```

---

## What Gets Built

### Directory Structure

```
linux-build/
├── sources/
│   ├── linux-6.1.x.tar.xz
│   └── buildroot-2026.02.tar.gz
├── linux/
│   └── arch/riscv/boot/Image
├── buildroot/
│   └── output/images/rootfs.ext2
└── riscv-gnu-toolchain/         # (if built from scratch)
    └── bin/riscv64-unknown-linux-gnu-*

linux-images/
├── rv32/
│   ├── fw_payload.bin    # Berkeley Boot Loader + Linux kernel
│   ├── root.bin          # Root filesystem image
│   ├── devicetree.dtb    # Device tree blob
│   ├── virt.dts          # Device tree source
│   └── setup.sh          # Environment variable export script
└── rv64/                 # (when built with --arch rv64)
    └── ...
```

### Output Components

#### `fw_payload.bin` (Firmware Payload)

Combined Berkeley Boot Loader (BBL) + Linux kernel image. Loaded by SimRV via
`-m` and executed starting at `0x80000000`.

#### `root.bin` (Root Filesystem)

Minimal ext2 filesystem containing:

- BusyBox shell utilities
- Essential C libraries (musl or glibc)
- Basic device nodes
- Init system

#### `devicetree.dtb` (Device Tree)

Describes the simulated hardware to Linux. Includes:

- CPU core (RV32GC or RV64GC, 1 hart)
- 256 MB DRAM at `0x80000000`
- UART serial console
- VirtIO block device controller (disk)
- PLIC interrupt controller
- CLINT timer

> [!NOTE]
> The DRAM size in the device tree must match the SimRV build-time
> `SIMRV_DRAM_SIZE_MB` setting (default: 256 MB).

---

## Troubleshooting

### `Command not found: riscv64-unknown-linux-gnu-gcc`

The RISC-V cross-compiler was not found. Install the toolchain or point the
script at an existing one:

```bash
RISCV_GNU_TOOLCHAIN_DIR=/opt/riscv ./scripts/build-linux-image.sh
```

Or install the toolchain from source:

```bash
git clone https://github.com/riscv-collab/riscv-gnu-toolchain
cd riscv-gnu-toolchain
./configure --prefix=/opt/riscv --with-arch=rv32gc --with-abi=ilp32d
make linux -j$(nproc)
```

### `Cannot find sources`

Internet connectivity issue. Clear the source cache and retry:

```bash
rm -f linux-build/sources/*
./scripts/build-linux-image.sh
```

### `linux-images/` directory empty after build

Verify the underlying build artifacts exist:

```bash
ls -la linux-build/buildroot/output/images/
ls -la linux-build/linux/arch/riscv/boot/
```

If files exist there but not under `linux-images/`, the copy step failed —
check the script output for errors.

### `dtc not found` warning

Install the device-tree compiler and retry:

```bash
sudo apt-get install device-tree-compiler
```

Or compile the device tree manually:

```bash
dtc -I dts -O dtb -o linux-images/rv32/devicetree.dtb linux-images/rv32/virt.dts
```

---

## Customization

### Kernel Configuration

1. Start a build: `./scripts/build-linux-image.sh`
2. Stop at the desired point (or use `--clean` to reset)
3. Uncomment the `make menuconfig` line in the script to enable interactive
   kernel config
4. Re-run the script

### Rootfs Contents

Edit the Buildroot config section in `create_rootfs_buildroot()` inside the script:

```bash
# Example additions to buildroot .config:
BR2_PACKAGE_OPENSSH=y
BR2_PACKAGE_CURL=y
```

Then rebuild:

```bash
./scripts/build-linux-image.sh --clean
```

---

## Integration with Validation Gates

The generated images integrate with the comprehensive CMake validation gate:

```bash
source linux-images/rv32/setup.sh
cmake --build --preset rv32-release --target integration-gate
```

The Linux boot test runs SimRV with a 1,000,000 cycle limit and checks for a
clean boot sequence. It is labeled `gate;regress;linux` in CTest and included
in `integration-gate`.

---

## Performance Notes

- **First build (with pre-installed toolchain)**: ~10–15 min
- **First build (toolchain from scratch)**: ~25–40 min
- **Subsequent builds**: ~5–10 min
- **Parallel speedup**: Near-linear with CPU cores (`JOBS=N`)
- **Disk space**: ~3–5 GB total

---

## References

- [RISC-V GNU Toolchain](https://github.com/riscv-collab/riscv-gnu-toolchain)
- [Linux RISC-V Support](https://kernel.org/)
- [Buildroot Documentation](https://buildroot.org/)
- [SimRV Architecture](../architecture/overview.md)

---

## Next Steps

After images are ready:

1. ✅ Export environment: `source linux-images/rv32/setup.sh`
2. ✅ Manual boot test: `./build/rv32-release/simrv -m $SIMRV_LINUX_MEM_IMG -D $SIMRV_LINUX_DISK_IMG --fdt $SIMRV_LINUX_DTB --cli`
3. ✅ TUI boot: `cmake --build --preset rv32-release --target run-tui`
4. ✅ Full validation: `cmake --build --preset rv32-release --target integration-gate`
