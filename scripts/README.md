# SimRV Scripts Directory

This directory contains development, benchmarking, release verification, and Linux image tooling for SimRV.

---

## Tool Categories

### 1. Benchmarking & Performance Diagnostics

- **`benchmark.sh`**: Primary shell runner for fast single-binary runs and cycle-accurate benchmarking (`--ca`). Wraps `benchmark.py`.
- **`benchmark.py`**: Unified publication benchmark framework and analysis tool:
  - Default / `run`: Measures execution speed, Spike differential speedups, and host Linux `perf` events (`--perf`).
  - `compare`: Compares two JSON benchmark reports, computing geometric mean changes and regressions against thresholds.
  - `aggregate`: Aggregates multi-run and multi-host benchmark experiment JSON outputs into statistical summaries.
  - `gdb`: Paired execution benchmarking comparing baseline CLI speed against remote GDB RSP step latency.
- **`benchmark_modes.py`**: Microarchitecture and simulation mode sweeps (`fast`, `detailed`, `cycle-accurate`, `three-stage`, `dual-issue`, `five-stage`).

### 2. Release Qualification & Reproducibility

- **`release_gate.py`**: Complete local release qualification matrix running multi-compiler (GCC, Clang) and dual-architecture (RV32, RV64) clean gate builds.
- **`release_check.py`**: Verifies release manifest constraints, binary ELF headers, MISA extensions, and version parity.
- **`release_evidence.py`**: Captures machine-readable evidence metadata for CI gates and qualification manifests.
- **`reproduce.py`**: Unified entry point for research reproduction workflows:
  - `--prepare`: Clones and verifies pinned, non-redistributed dependencies (`riscv-tests`, `vector-tests`, `spike`).
  - `--package`: Assembles deterministic `.tar.gz` and `.sha256` research reproducibility bundles.
  - Default: Executes reproducible experiment sweeps and generates qualification reports.

### 3. Linux Kernel, Rootfs & Integration

- **`build-linux-image.sh`**: Multi-stage compiler for OpenSBI, Linux kernel, and BusyBox/Alpine rootfs images. Includes storage management flags (`--clean-build`, `--clean-old-kernels`).
- **`test_linux_pty.py`**: Automated headless pseudo-terminal interaction tests verifying guest login, shell commands, and poweroff lifecycles.
- **`templates/`**: Device tree sources (`virt-rv64.dts`, `virt-rv32.dts`) and early boot payloads.

### 4. Code Quality & Extension Verification

- **`run_vector_tests.py`**: Automated runner for the RISC-V Vector (RVV 1.0) compliance test suite.
- **`code_metrics.py`**: Source code diagnostic analyzer tracking lines of code, cyclomatic complexity, and test ratios.
