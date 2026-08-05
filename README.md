# SimRV

[![C/C++ CI](https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml/badge.svg?branch=dev)](https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml)

SimRV is a high-performance RISC-V functional and cycle-accurate simulator written in modern C++23, supporting both **RV64GCBV** and **RV32GCBV**. It features interactive TUI split-screen visualizers, guest stack frame analysis, interactive cache set/way inspectors, configurable MISA CSRs, and VirtIO-style console and disk models for booting Linux or running baremetal binaries.

---

## Quick Start

### Build Prerequisites
- **Clang 20+** (default in CMake presets) or **GCC 14+** (required for full C++23 feature support)
- **CMake 3.20+** & **Ninja**

### Building SimRV

```bash
# RV64 build (Default)
cmake --preset rv64-release
cmake --build --preset rv64-release

# RV32 build
cmake --preset rv32-release
cmake --build --preset rv32-release
```

### Running Applications

Run a baremetal binary:
```bash
./build/rv64-release/SimRV -m img/hello.bin
```

Run in interactive TUI mode:
```bash
./build/rv64-release/SimRV -m img/hello.bin -u
```

Run Linux OS image with disk & devicetree:
```bash
./build/rv64-release/SimRV -m linux-images/rv64/fw_payload.bin -D linux-images/rv64/root.bin -c linux-images/rv64/devicetree.dtb
```

Override MISA profile or Vector register length (VLEN):
```bash
# Set MISA profile to RV64GC and VLEN to 512 bits
./build/rv64-release/SimRV -m img/vector.bin --misa rv64gcv --vector-len 512
```

---

## Interactive TUI Split-Screen Monitor

SimRV includes a rich terminal user interface (TUI) for hardware inspection, step-by-step instruction execution, and educational visualization.

### Key Shortcuts

| Hotkey | Action |
| --- | --- |
| `[s]` / `[Space]` | Single instruction step |
| `[n]` | Step N instructions |
| `[c]` / `[Ctrl-P]` | Run / Pause simulation loop |
| `[Click Label]` / `[Click Badge]` | Click active running badge to pause |
| `[b]` | Step back 1 instruction (Rollback tracking) |
| `[o]` / `[Alt-O]` | Open Binary / Disk image loader modal |
| `[,]` / `[Alt-S]` | Simulator Settings modal (CA/IA mode, rollback, logging) |
| `[Alt-M]` | Configure MISA CSR modal (Extensions A/B/C/D/F/M/V/S/U & VLEN) |
| `[y]` | Cycle-Accurate System Config modal |
| `[m]` | Memory / Stack inspector modal |
| `[l]` / `[Alt-L]` | Cycle tool inspector tab (Pipe / Cache / BP / Hazard / TLB / Bus) |
| `[r]` / `[Alt-R]` | Cycle register tab (GPR / FPR / VEC) |
| `[Tab]` | Cycle TUI layout |
| `[F1]` / `[h]` / `[?]` | Display online help shortcuts |
| `[Esc]` | Close active modal |

---

## Supported RISC-V Extensions

Both RV32GCBV and RV64GCBV instruction sets are supported.

| Extension | Status | Description & Features |
| --- | --- | --- |
| **I** | ✅ Supported | Base integer instruction set (RV32I / RV64I) |
| **M** | ✅ Supported | Integer multiplication, division, and remainder |
| **A** | ✅ Supported | Atomic memory operations (LR/SC, AMO word/doubleword) |
| **C** | ✅ Supported | Compressed instruction decode and execution |
| **F / D** | ✅ Supported | Single- and double-precision floating-point (FP CSRs included) |
| **V** | ✅ Supported | Vector Extension (RVV 1.0) with configurable VLEN (32–1024 bits) |
| **B** | ✅ Supported | Bit manipulation extension (Zba, Zbb, Zbc, Zbs) |
| **Privileged** | ✅ Supported | Machine, Supervisor, User modes (M/S/U), CSR access, traps |
| **SV32 / SV39 / SV48** | ✅ Supported | Hardware MMU page table walker & TLB translation |

---

## Testing & Validation

Always run gate test coverage on both 64-bit and 32-bit build presets:

```bash
# RV64 Full Gate Check (Default)
ctest --test-dir build/rv64-release --output-on-failure -L gate

# RV32 Full Gate Check
ctest --test-dir build/rv32-release --output-on-failure -L gate
```

### ISA Test Suite
For running the `riscv-tests` suite, set `RISCV_TESTS_DIR`:

```bash
RISCV_TESTS_DIR=/path/to/riscv-tests ctest --test-dir build/rv64-release --output-on-failure -L rv64gc
RISCV_TESTS_DIR=/path/to/riscv-tests ctest --test-dir build/rv32-release --output-on-failure -L rv32gc
```

---

## Co-Simulation & Debugging

### GDB Remote Debugging
SimRV includes a built-in GDB RSP server:

```bash
# Start SimRV with GDB server on port 1234
./build/rv64-release/SimRV -m path/to/hello.bin --gdb

# Connect from GDB in another terminal
riscv64-unknown-elf-gdb hello.elf -ex "target remote :1234"
```

### Spike Lockstep Co-Simulation
Verify execution against Spike instruction-by-instruction:

```bash
./build/rv64-release/SimRV -m path/to/hello.bin --lockstep
```

---

## Release Assets & Pre-built Binaries

Pre-compiled standalone binaries (`SimRV`) are available under GitHub Releases for Linux (`x86_64`).

> [!NOTE]
> Pre-built release assets package the standalone simulator binary only. They do **not** bundle the complementary build scripts (`scripts/`), benchmark tooling, or sample guest disk images. For the full suite of scripts and development tools, clone the repository.

---

## Project Structure

- `src/`: Core implementation C++ units
- `include/simrv/`: Simulator headers & public API
- `scripts/`: Regression, ISA testing, and Linux image build helpers
- `docs/`: Architecture and design notes (`docs/ARCHITECTURE.md`, `docs/BAREMETAL_GUIDE.md`)
- `CHANGELOG.md`: Version release log

---

## License

SimRV is licensed under the [MIT License](file:///home/archlab/ltrunk/workspace/tools/SimRV/LICENSE).

### Third-Party Component Licenses

- **TinySoundFont** (`include/simrv/util/tsf.h`): MIT License (Copyright (C) Bernhard Schelling & Steve Folta)
- **TinyMidiLoader** (`include/simrv/util/tml.h`): zlib License (Copyright (C) Bernhard Schelling)


