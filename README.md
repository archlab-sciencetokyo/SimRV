# SimRV

SimRV is a cycle-oriented RISC-V functional simulator focused on RV32 development.
It supports Linux and application execution, includes VirtIO-style console/disk devices,
and uses a CMake/Ninja-based validation workflow.

## Quick Start

Build:

```bash
cmake --preset ninja-clang-release
cmake --build --preset ninja-clang-release
```

Run an application image:

```bash
./build/ninja-clang-release/SimRV -m img/hello.bin
```

Run Linux image:

```bash
./build/ninja-clang-release/SimRV -m img/bbl.bin -d img/root.bin -c img/devicetree.dtb
```

Show command-line help:

```bash
./build/ninja-clang-release/SimRV
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

Useful ISA/regression targets:

```bash
cmake --build --preset ninja-clang-release --target regress
cmake --build --preset ninja-clang-release --target isa-tests
cmake --build --preset ninja-clang-release --target isa-smoke-rv32gc
```

For ISA targets, the `RISCV_TESTS_DIR` environment variable should be set to your `riscv-tests` path:

```bash
RISCV_TESTS_DIR=/path/to/riscv-tests cmake --build --preset ninja-clang-release --target isa-tests
```

## Documentation

- Architecture overview: `docs/ARCHITECTURE.md`
- Refactor concept: `REFACTOR_CLASS_CONCEPT.md`
- RV64 migration roadmap: `ROADMAP_RV32GC_to_RV64GC.md`

Generate API docs (if Doxygen is installed):

```bash
cmake --build --preset ninja-clang-release --target docs
```

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
