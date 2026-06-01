# SimRV

[![C/C++ CI](https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml/badge.svg?branch=dev)](https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml)

SimRV is a cycle-oriented RISC-V functional simulator focused on RV32 development.
It supports Linux and application execution, includes VirtIO-style console/disk devices,
and uses a CMake/Ninja-based validation workflow.

## Quick Start

Build:

```bash
# RV32 build (default)
cmake --preset rv32-release
cmake --build --preset rv32-release

# RV64 build
cmake --preset rv64-release
cmake --build --preset rv64-release
```

Run an application image:

```bash
./build/rv32-release/SimRV -m path/to/hello.bin
```

Run Linux image:

```bash
./build/rv64-release/SimRV -m linux-images/rv64/fw_payload.bin -d linux-images/rv64/root.bin -c linux-images/rv64/devicetree.dtb
```

To build these images yourself:

```bash
# Build RV32 linux images (outputs to linux-images/rv32/)
./scripts/build-linux-image.sh --arch rv32

# Build RV64 linux images (outputs to linux-images/rv64/)
./scripts/build-linux-image.sh --arch rv64
```

Show command-line help:

```bash
./build/rv32-release/SimRV
```

Run an assembly source directly (assembled at launch):

```bash
./build/rv32-release/SimRV -A path/to/prog.S
```

Select a supported MISA profile:

```bash
./build/rv32-release/SimRV -m path/to/hello.bin --misa rv32imac
```

## Supported RISC-V Extensions

Current implementation is RV32 with default MISA profile set to GC and privilege-related
S/U bits enabled in the simulator state.

| Extension Group | Status | Notes |
| --- | --- | --- |
| RV32I | Supported | Base integer instruction set |
| RV32M | Supported | Integer multiply/divide and remainder instructions are available. |
| RV32A | Supported | LR/SC and AMO word operations are available. |
| RV32C | Supported | Compressed instruction decode and execution are available. |
| RV32F | Supported | Single-precision floating-point instructions and FP CSRs are available. |
| RV32D | Supported | Double-precision floating-point instructions are available. |
| Privileged + CSR | Supported | Machine/Supervisor/User state, CSR access, traps, and interrupts are implemented. |

Validation policy:

- Current public smoke coverage centers on RV32I/M/A via `isa-smoke-rv32gc`.

## Testing

CTest gate suite (regression + rv32gc smoke):

```bash
ctest --test-dir build/rv32-release --output-on-failure -L gate
```

ISA-only smoke subset:

```bash
RISCV_TESTS_DIR=/path/to/riscv-tests ctest --test-dir build/rv32-release --output-on-failure -L rv32gc
```

For ISA CTest runs, the `RISCV_TESTS_DIR` environment variable should be set to your `riscv-tests` path.

```bash
RISCV_TESTS_DIR=/path/to/riscv-tests ctest --test-dir build/rv32-release --output-on-failure -L gate
```

## Benchmarking

Run the benchmark CTest entry:

```bash
RISCV_TESTS_DIR=/path/to/riscv-tests ctest --test-dir build/rv32-release --output-on-failure -L benchmark
```

The benchmark executes multiple runs and reports mean/median/min/max KIPS.

Useful benchmark environment variables:

- `SIMRV_BENCH_ITERS` (default: `5`)
- `SIMRV_BENCH_END` (default: `2000000`)
- `SIMRV_BENCH_TIMEOUT` (default: `20`)
- `SIMRV_BENCH_IMG` (optional direct binary image path)
- `SIMRV_BENCH_TEST` (default riscv-tests ELF name: `rv32ui-p-add`)

## Documentation

- Architecture overview: `docs/ARCHITECTURE.md`
- Refactor concept: `REFACTOR_CLASS_CONCEPT.md`
- RV64 migration roadmap: `ROADMAP_RV32GC_to_RV64GC.md`

Generate API docs (if Doxygen is installed):

```bash
cmake --build --preset rv32-release --target docs
```

## GitHub CI/CD

This repository now includes two GitHub Actions workflows:

- CI build: `.github/workflows/c-cpp.yml`
  - Runs on pushes and pull requests to `main` and `dev`
  - Configures and builds SimRV with the `rv32-release` preset
  - Runs CTest gate suite (`-L gate`) with `riscv-tests`

- Release binaries: `.github/workflows/release-binaries.yml`
  - Runs when a GitHub Release is published
  - Linux-only (`ubuntu-latest`)
  - Verifies CTest gate suite (`-L gate`) with `riscv-tests` before packaging
  - Builds SimRV on Linux (`x86_64`), packages the executable, and uploads:
    - `SimRV-linux-x86_64-<tag>.tar.gz`
    - `SimRV-linux-x86_64-<tag>.sha256`

To publish binaries automatically:

1. Push a tag (for example `v2.0.0`).
2. Create/publish a GitHub Release for that tag.
3. Download assets from the Release page after the workflow finishes.

## Project Layout

- `src/`: simulator implementation
- `include/simrv/`: simulator headers
- `scripts/`: regression and ISA helper scripts
- `docs/`: architecture and design notes

## Recommended Compilers

- `clang++` 17+ (default in preset)
- `gcc/g++` 12+

## Development Flow

Main branch roles in this repository:

- `main`: stable/release-oriented branch
- `dev`: integration branch for ongoing development
