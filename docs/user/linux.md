# Building Linux Images for SimRV

This guide explains how to build RISC-V RV32GC (or RV64GC) Linux kernel and rootfs
images for SimRV testing. The default RV64 image builder also provisions a reproducible Alpine
Linux graphical path using Xorg, JWM, VirtIO framebuffer/input, a terminal, and small
offline-friendly demo applications.

## Overview

SimRV supports full Linux OS boot as part of its integration validation gate. You need:

- **`SIMRV_LINUX_MEM_IMG`**: OpenSBI `FW_PAYLOAD` convenience image containing Linux
- **`SIMRV_LINUX_DISK_IMG`**: Root filesystem image
- **`SIMRV_LINUX_DTB`** (optional): Device tree blob

Every build also publishes standalone `Image`, `fw_dynamic.bin`, `devicetree.dtb`, and
`manifest.json` artifacts. The payload is convenient for SimRV; FPGA bootloaders should use
the standalone OpenSBI/Linux/DTB/rootfs set and supply the next-stage handoff.

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

# Optional headless Buildroot FPGA baseline
./scripts/build-linux-image.sh --arch rv64 --rootfs buildroot --profile fpga \
    --libc musl --cross-compile riscv64-unknown-linux-musl-

# Optional alternate Alpine mirror (also settable via SIMRV_ALPINE_MIRROR)
./scripts/build-linux-image.sh --alpine-mirror https://dl-5.alpinelinux.org/alpine
```

This will:

1. ✅ Check for a pre-installed RISC-V GNU toolchain (or build one from source)
2. ✅ Download Linux kernel, OpenSBI, and the selected Alpine or Buildroot sources
3. ✅ Build kernel and rootfs
4. ✅ Create compatible images in `./linux-images/<arch>/` or the profile subdirectory

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
It also exports `SIMRV_LINUX_IMAGE`, `SIMRV_LINUX_OPENSBI`, `SIMRV_LINUX_ROOTFS`, and
`SIMRV_LINUX_PROFILE` for bootloaders and test harnesses.

### FPGA baseline profile

The opt-in Buildroot profile is headless and uses BusyBox init, a conventional serial getty,
DHCP on `eth0`, `/etc/fstab`, and a journaled ext4 root filesystem labeled `simrv-root`:

```bash
./scripts/build-linux-image.sh --arch rv64 --rootfs buildroot --profile fpga \
    --cross-compile riscv64-unknown-linux-musl-
source linux-images/rv64/buildroot-fpga/setup.sh
scripts/check-linux-artifacts.py linux-images/rv64/buildroot-fpga
```

### Expanded debug profile

The debug profile extends the Alpine GUI image with kernel diagnostics useful for boot and VirtIO
profiling: timestamped/caller-attributed printk, debugfs, full kallsyms, Magic SysRq, function
tracing, block I/O tracing, stack traces, and DWARF-5 debug information. It is opt-in and writes
to a separate profile directory so the normal GUI image remains small:

```sh
./scripts/build-linux-image.sh --arch rv64 --rootfs alpine --profile debug \
  --alpine-mirror https://dl-cdn.alpinelinux.org/alpine/v3.24
python3 scripts/check-linux-artifacts.py linux-images/rv64/alpine-debug
```

The same profile is available through CMake as `linux-images-debug` when
`SIMRV_ENABLE_LINUX_TOOLS` is enabled. The manifest records the exact kernel
configuration hash and enabled debug feature set for reproducibility. Because this is an explicit
early-userspace debug profile, its DTB also includes `rdinit=/init`.

All profiles retain conventional built-in initramfs, serial console, VirtIO, ext4/JBD2, procfs,
sysfs, and tmpfs support. Debug tracing is deliberately excluded from the normal GUI and FPGA
baseline images.

### Guest Lifecycle Management

SimRV supports clean guest shutdown and reboot via standard OpenSBI SBI reset services and the SiFive test-finisher MMIO device (`0x00100000`):

```sh
poweroff   # Syncs filesystems, stops services, and halts SimRV
reboot     # Cleanly restarts the guest system
```

### RV64 graphical smoke path

The RV64 disk image contains an idempotent `start-jwm` helper. The image builder bundles the
pinned desktop packages when the RISC-V user emulator is available; otherwise the helper falls
back to installing them on the first networked boot. BusyBox `getty` authenticates on `ttyS0`, and
root's `/root/.profile` starts the helper in the background only after a successful login. It waits
for `/dev/fb0` and the VirtIO input device, then starts Xorg on the simulated framebuffer and JWM.
The JWM Applications
menu includes one SimRV Games submenu for Chess, Gomoku, Klondike, and Snake; it is rebuilt idempotently on every
boot so persistent root filesystems do not accumulate duplicate menus. The guest log reports
`[JWM]` and `[NET]` milestones on the UART, which makes the path suitable for headless CI smoke
checks as well as interactive TUI/Sixel sessions. If the host terminal has no Sixel support, use
the framebuffer or a plain terminal attachment; guest graphics are independent of Sixel
presentation.

The Alpine simulator image intentionally has an empty root password so serial login and privilege
tests work out of the box. Set a board-specific password or key-based policy before using an image
on shared or production FPGA hardware.

For TAP networking, run `scripts/setup-simrv-tap.sh` before launching the guest. It configures the
gateway/NAT path and, when `socat` is available, relays UDP and TCP DNS on `10.0.2.1:53` to the
host's first non-loopback resolver. The guest network service uses that gateway resolver first, which
avoids depending on a WSL-specific resolver address.
The emulator process itself must also have permission to open `/dev/net/tun`; grant that device
access to the launching user or run the TAP-backed emulator with the host's appropriate privilege.

For repeated profiling or emulator smoke runs, keep the generated `root.img` as a golden image and
clone it for each run instead of reusing the writable disk. The helper below uses a reflink when the
host filesystem supports one, refuses to overwrite an existing run image, and validates the clone
read-only before launch:

```sh
scripts/clone-linux-disk.sh linux-images/rv64/root.img /tmp/simrv-run/root.img
```

Discard a run image after a timeout or failed shutdown. A successful guest poweroff should still be
followed by a read-only check such as `e2fsck -fn /tmp/simrv-run/root.img`; the helper intentionally
does not repair or modify either image.

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
    perl-ExtUtils-MakeMaker \
    gcc-riscv64-linux-gnu gcc-c++-riscv64-linux-gnu \
    binutils-riscv64-linux-gnu
```

`e2fsprogs` provides `mkfs.ext4`, which is required when packaging the RV64
Alpine root disk. Fedora's RISC-V GCC packages provide a kernel-capable cross
compiler and binutils, but do **not** provide a target glibc sysroot. They
therefore cannot statically link the RV32 BusyBox rootfs by themselves.

If APK reports that `dl-cdn.alpinelinux.org` cannot resolve, test host DNS and
HTTPS access first. The mirror is normally healthy; restricted containers or
WSL-generated resolvers can block DNS from the build environment. Use an
approved host-network build or set `SIMRV_ALPINE_MIRROR` to a reachable Alpine
mirror. The builder writes the selected mirror into the guest's
`/etc/repositories`, so host-side APK bundling and the guest first-boot
fallback use the same repository family.

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

# Optional headless FPGA profile
cmake --build --preset rv64-release --target linux-images-buildroot
```

---

## What Gets Built

### Directory Structure

```
linux-build/
├── sources/
│   ├── linux-7.2.3.tar.xz
│   ├── opensbi-1.9.tar.gz
│   └── buildroot-2025.02.18.tar.xz  # Buildroot profile only
├── linux-7.2.3/
│   └── vmlinux
└── buildroot-output-rv64/        # Buildroot profile only
    └── images/rootfs.ext4

linux-images/
├── rv32/
│   ├── fw_payload.bin    # OpenSBI FW_PAYLOAD + Linux kernel
│   ├── fw_dynamic.bin    # Standalone OpenSBI firmware for FPGA handoff
│   ├── Image             # Standalone Linux kernel image
│   ├── root.bin          # Root filesystem image
│   ├── manifest.json     # Versions, bootargs, memory, and SHA-256 hashes
│   ├── devicetree.dtb    # Device tree blob
│   ├── virt.dts          # Device tree source
│   └── setup.sh          # Environment variable export script
└── rv64/                 # (when built with --arch rv64)
    └── ...
```

### Output Components

#### `fw_payload.bin` (OpenSBI Firmware Payload)

OpenSBI generic-platform `FW_PAYLOAD` image containing the Linux kernel and generated device tree.
Loaded by SimRV via `-m` and executed starting at `0x80000000`.

#### `fw_dynamic.bin`, `Image`, and `manifest.json`

These are the canonical separable artifacts for FPGA work. `fw_dynamic.bin` is OpenSBI firmware
for a bootloader that supplies the next-stage address and DTB; `Image` is the Linux kernel binary;
`manifest.json` records exact versions, DTB memory size, kernel arguments, the rootfs label, and
hashes. Validate a profile with `scripts/check-linux-artifacts.py` before handing it to FPGA tools.

#### `root.img` / `root.bin` (Root Filesystem)

Standard ext4 filesystem with journaling enabled, containing:

- BusyBox shell utilities
- Essential C libraries (musl or glibc)
- Basic device nodes
- Init system

#### `devicetree.dtb` (Device Tree)

Describes the simulated hardware to Linux. Includes:

- CPU core (RV32GC or RV64GC, 1 hart)
- The selected DRAM size at `0x80000000` (512 MB by default; override with `--dram-size-mb`)
- UART serial console
- VirtIO block device controller (disk)
- PLIC interrupt controller
- CLINT timer

> [!NOTE]
> The published DTB and simulator must use the same DRAM size. The builder defaults to 512 MB,
> matching the Linux boot memory floor; use `--dram-size-mb` and the corresponding simulator
> `--dram-size` option when targeting a different FPGA memory map.

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

Customize the Buildroot external-tree overlay under
`configs/buildroot/simrv-fpga/rootfs-overlay/` and the profile defconfigs in the same directory:

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
