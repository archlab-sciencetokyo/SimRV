# SimRV Scripts Directory

This directory contains development, benchmarking, release verification, and Linux image tooling for SimRV.

---

## Tool Categories

### 1. Benchmarking & Performance Diagnostics

- **`simrv-benchmark`** (Native C++): Native benchmarking and comparative analysis suite. Supports full-system instruction and cycle-accurate benchmarking, statistical aggregations (mean, median, standard deviation), and `compare` mode between baseline and candidate JSON runs.
- **`benchmark.sh`**: Shell runner for quick execution and cycle-accurate benchmarking (`--ca`). Wraps native `simrv-benchmark`.
- **`simrv-parity` & `simrv-tune`** (Native C++): Adapter-driven RTL/SimRV architectural and cycle-parity runner with dynamic `.so` plugin support (`include/simrv/rtl/ParityPlugin.h`). Built-in targets include `rvcomp` and `cfu-pg`. Auto-tunes CPU/SoC configurations against hardware traces.

### 2. Release Qualification & Verification (Native C++)

Release qualification and verification tools have been rewritten as fast native C++ utilities:

- **`simrv-release-check`**: Validates release manifest constraints, dependency revisions, and binary version parity.
- **`simrv-smoke`**: Distribution-independent GA smoke check for versioning, capability discovery, environment diagnostics, and SoC manifest compatibility.
- **`simrv-evidence`**: Captures machine-readable evidence metadata for CI gates and qualification manifests.
- **`simrv-check-artifacts`**: Validates published Linux kernel and rootfs profile bundles against manifest digests and filesystem contracts.
- **`simrv-cpu-wizard`**: CLI wizard for scaffolding SimRV system and SoC configurations (.cfg).

### 3. Linux Kernel, Rootfs & Integration

- **`build-linux-image.sh`**: Multi-stage compiler for OpenSBI, Linux kernel, and BusyBox/Alpine rootfs images, including the RV64 Alpine UEFI disk profile. Supports `SIMRV_GUEST_BUILD_DIR` / `SIMRV_LINUX_BUILD_DIR` and `SIMRV_LINUX_IMAGES_DIR`.
- **`setup-simrv-tap.sh`**: Helper script to configure TAP networking for guest simulation.
- **`templates/`**: Device tree sources (`virt-rv64.dts`, `virt-rv32.dts`) and early boot payloads.

### 4. Code Quality & Extension Verification

- **`simrv-vector`** (Native C++): Automated runner for the RISC-V Vector (RVV 1.0) compliance test suite. Compiles generated vector tests with RISC-V GCC, converts with objcopy, extracts tohost addresses, and runs parallel validation on SimRV.
- **Native CLI Commands in SimRV**:
  - `simrv --check-mission <file>`: Validates mission fields, previews grouped lesson flow, and checks checkpoint symbols.
  - `simrv --create-mission <file>`: Scaffolds a new classroom guidance mission template.
  - `simrv --create-disk <out.img> -m <kernel.efi>`: Creates an unprivileged GPT FAT32 UEFI disk image.
  - `simrv --notices`: Displays third-party open source licenses and attributions.
