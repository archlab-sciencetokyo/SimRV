# SimRV

[![C/C++ CI](https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml/badge.svg?branch=dev)](https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml)

SimRV is a high-performance RISC-V functional simulator supporting both RV32GC and RV64GC.
It runs Linux and bare-metal application images, includes VirtIO-style console/disk devices,
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
./build/rv64-release/SimRV -m linux-images/rv64/fw_payload.bin -D linux-images/rv64/root.bin -c linux-images/rv64/devicetree.dtb
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

Both RV32GC and RV64GC are fully implemented.  The default MISA profile is GC for each
build, selectable at configure time via `SIMRV_XLEN` and overridable at runtime with `--misa`.

| Extension Group | Status | Notes |
| --- | --- | --- |
| RV32I / RV64I | ✅ Supported | Base integer instruction set for both widths |
| M | ✅ Supported | Integer multiply/divide and remainder |
| A | ✅ Supported | LR/SC and AMO word (and doubleword in RV64) operations |
| C | ✅ Supported | Compressed instruction decode and execution |
| F | ✅ Supported | Single-precision floating-point (FP CSRs included) |
| D | ✅ Supported | Double-precision floating-point |
| Privileged + CSR | ✅ Supported | M/S/U privilege, CSR access, traps, interrupts, TLB/MMU |
| SV32 (RV32) | ✅ Supported | Page-based virtual memory for RV32 |
| SV39 / SV48 (RV64) | ✅ Supported | Page-based virtual memory for RV64 |

Validation policy:

- Public smoke coverage centres on RV32I/M/A via `isa-smoke-rv32gc` and RV64I/M/A via `isa-smoke-rv64gc`.
- Full GDB remote debug and Spike lockstep co-simulation are available for deeper validation.

## Testing

CTest gate suite (regression + rv32gc smoke):

```bash
ctest --test-dir build/rv32-release --output-on-failure -L gate
```

ISA-only smoke subset:

```bash
# RV32
RISCV_TESTS_DIR=/path/to/riscv-tests ctest --test-dir build/rv32-release --output-on-failure -L rv32gc
# RV64
RISCV_TESTS_DIR=/path/to/riscv-tests ctest --test-dir build/rv64-release --output-on-failure -L rv64gc
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

## GDB Debugging

SimRV includes a GDB RSP stub for source-level debugging:

```bash
# Start SimRV with GDB stub listening on port 1234 (default)
./build/rv32-release/SimRV -m path/to/hello.bin --gdb

# In another terminal, attach with riscv GDB
riscv32-unknown-elf-gdb hello.elf -ex "target remote :1234"

# RV64 example
./build/rv64-release/SimRV -m path/to/hello64.bin --gdb --gdb-port 2345
riscv64-unknown-linux-gnu-gdb hello64.elf -ex "target remote :2345"
```

Supported RSP operations: register read/write, memory read/write, software breakpoints,
single-step, continue, detach.

## Spike Lockstep Co-Simulation

For correctness verification, SimRV can run Spike as a co-simulator and compare
architectural state after every committed instruction:

```bash
# RV32
./build/rv32-release/SimRV -m path/to/hello.bin --lockstep

# RV64 with custom spike path
./build/rv64-release/SimRV -m path/to/hello64.bin --lockstep --spike-bin /opt/riscv/bin/spike
```

The ISA string passed to Spike is derived automatically from the active MISA profile
(e.g. `rv32gc`, `rv64imac`).  Divergences are printed as a coloured diff to stderr.

## Documentation

- Architecture overview: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- Originality & unique design aspects: [docs/ORIGINALITY.md](docs/ORIGINALITY.md)
- Baremetal programming and MMIO guide: [docs/BAREMETAL_GUIDE.md](docs/BAREMETAL_GUIDE.md)

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

Since SimRV is written using C++23 features, the recommended compiler versions are:
- `clang++` 17+ (default in preset)
- `gcc/g++` 13+

## Development Flow

Main branch roles in this repository:

- `main`: stable/release-oriented branch
- `dev`: integration branch for ongoing development
