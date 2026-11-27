# SimRV Scripts Directory

This directory contains development, benchmarking, release verification, and Linux image tooling for SimRV.

---

## Tool Categories

### 1. Benchmarking & Performance Diagnostics

- **`simrv-benchmark`** (Native C++): Native benchmarking and comparative analysis suite. Supports full-system instruction and cycle-accurate benchmarking, statistical aggregations (mean, median, standard deviation), and `compare` mode between baseline and candidate JSON runs.
- **`benchmark.sh`**: Shell runner for quick execution and cycle-accurate benchmarking (`--ca`). Wraps native `simrv-benchmark`.

### 2. Release Qualification & Verification (Native C++)

Release qualification and verification tools have been rewritten as fast native C++ utilities:

- **`simrv-release-check`**: Validates release manifest constraints, dependency revisions, and binary version parity.
- **`simrv-smoke`**: Distribution-independent GA smoke check for versioning, capability discovery, environment diagnostics, and SoC manifest compatibility.
- **`simrv-evidence`**: Captures machine-readable evidence metadata for CI gates and qualification manifests.
- **`simrv-check-artifacts`**: Validates published Linux kernel and rootfs profile bundles against manifest digests and filesystem contracts.
- **`simrv-config-wizard`**: CLI wizard for scaffolding SimRV system and SoC configurations (.cfg).

### 3. Linux Kernel, Rootfs & Integration

- **`simrv-linux-builder`** (Native C++): Builds Linux, OpenSBI, BusyBox/Alpine or Buildroot root filesystems, RV64 UEFI firmware and image manifests. Enable with `-DSIMRV_ENABLE_LINUX_TOOLS=ON`; use `linux-images-configure` for the version picker or the `linux-images*` targets for preset builds. The picker reads upstream catalogs and saves editable version pins, catalog URLs, archive URLs, local archive paths and SHA-256 values in `scripts/linux-versions.conf`.
- **`build-linux-image.sh`**: Compatibility launcher for `simrv-linux-builder`. It contains no build stages. Set `SIMRV_LINUX_BUILDER` to select a specific executable; otherwise it checks the RV64/RV32 dev and release build directories. The native builder supports `SIMRV_GUEST_BUILD_DIR` / `SIMRV_LINUX_BUILD_DIR` and `SIMRV_GUEST_IMAGES_DIR` / `SIMRV_LINUX_IMAGES_DIR`.
- **`templates/`**: Device tree sources (`virt-rv64.dts`, `virt-rv32.dts`) and early boot payloads.

### 4. Code Quality & Extension Verification

- **`simrv-vector`** (Native C++): Automated runner for the RISC-V Vector (RVV 1.0) compliance test suite. Compiles generated vector tests with RISC-V GCC, converts with objcopy, extracts tohost addresses, and runs parallel validation on SimRV.
- **`simrv-tools`**: Collects host-side scaffolding, mission, disk-image, and shell-completion commands outside the simulator runtime binary.
- **`simrv --notices`**: Displays third-party open source licenses and attributions.
- **`simrv-parity`**: Generic RTL parity and calibration runner. Load an optional target adapter using the versioned C ABI and use editable model `.cfg` files.
