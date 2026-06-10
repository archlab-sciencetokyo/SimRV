# SimRV Project Instructions

## Project Overview
SimRV is a cycle-oriented RISC-V functional simulator primarily focused on RV32 development (specifically RV32GC), with a planned migration to RV64GC. It is written in C++23. It supports Linux and application execution, and includes VirtIO-style console and disk devices.

The architecture consists of:
- `Machine`: Top-level pipeline orchestration, memory/device integration.
- `CPU`: Architectural state (GPRs, CSRs, TLB, privilege/exceptions).
- `Disk` & `Console`: MMIO-backed peripheral models.
- Core pipeline stages: IF, ID, EX, MEM, WB, and Commit.

## Building and Running

SimRV uses CMake and Ninja for its build system.
- **Recommended Compilers:** Clang 17+ (default in preset) or GCC 13+.

**Build Commands:**
```bash
cmake --preset rv32-release
cmake --build --preset rv32-release
```

**Run Commands:**
```bash
# Run an application image
./build/rv32-release/SimRV -m img/hello.bin

# Run a Linux image
./build/rv32-release/SimRV -m linux-images/rv32/fw_payload.bin -d linux-images/rv32/root.bin -c linux-images/rv32/devicetree.dtb

# Run command-line help
./build/rv32-release/SimRV -h
```

## Testing and Validation

Testing involves CTest and requires the `RISCV_TESTS_DIR` environment variable to point to `riscv-tests` for ISA subset tests.

**Test Commands:**
```bash
# CTest gate suite (regression + rv32gc smoke)
ctest --test-dir build/rv32-release --output-on-failure -L gate

# Run Smoke Validation Gate (pre-PR check)
cmake --build --preset rv32-release --target smoke-gate
```

## Development Conventions

- **Branching Model:**
  - `main`: stable branch, kept releasable.
  - `dev`: integration branch for day-to-day development.
  - `feature/*` and `fix/*`: short-lived branches created from `dev`.
- **Workflow:**
  - Branch from `dev`.
  - Keep commit messages short and imperative. One concern per commit.
  - Open Pull Requests into `dev`. Include build/test context in the description.
  - For releases, open PR from `dev` to `main`.
- **Validation:** Always run at least the build and `smoke-gate` checks before opening a PR:
  ```bash
  cmake --build --preset rv32-release
  cmake --build --preset rv32-release --target smoke-gate
  ```

## Directory Structure
- `src/`: Simulator C++ implementation units.
- `include/simrv/`: Simulator public/internal headers.
- `scripts/`: Regression and ISA testing helper scripts.
- `docs/`: Architecture and design notes.
