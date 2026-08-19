# SimRV

[![C/C++ CI](https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml/badge.svg?branch=dev)](https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml)

SimRV is a C++23 research simulator for studying RISC-V functional behavior, in-order pipeline
timing, memory-system behavior, and interactive architecture education. It provides functional and
cycle-accurate modes for the **RV64GCBV** and **RV32GCBV** implementation targets, plus a TUI,
guest stack analysis, cache inspectors, configurable MISA state, and a small virtual platform.

The intended audience is computer-architecture researchers, students, and simulator developers.
SimRV is suitable for reproducible experiments and teaching when the documented model matches the
research question. It is not RISC-V certified, a production hypervisor, or a security boundary.
The `GCBV` names are implementation targets; floating-point RMM arithmetic and parts of RVV 1.0
remain qualification gaps documented in [the compliance scope](docs/RISCV_COMPLIANCE.md).

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

Run a baremetal binary in interactive TUI mode (Default):
```bash
./build/rv64-release/SimRV -b -m img/hello.bin
```

Run headless in CLI-only mode:
```bash
./build/rv64-release/SimRV -b -m img/hello.bin -c
```

Run Linux OS image with disk & devicetree:
```bash
./build/rv64-release/SimRV --os -m linux-images/rv64/fw_payload.bin -D linux-images/rv64/root.bin -f linux-images/rv64/devicetree.dtb
```

Override MISA profile or Vector register length (VLEN):
```bash
# Select the explicit RV64GCBV target profile and a 512-bit VLEN
./build/rv64-release/SimRV -m img/vector.bin --misa rv64gcbv --vlen 512
```

---

## Interactive TUI Split-Screen Monitor

SimRV includes a rich terminal user interface (TUI) for hardware inspection, step-by-step instruction execution, and educational visualization.

### Key Shortcuts

| Hotkey | Action |
| --- | --- |
| `[s]` / `[Space]` | Single instruction step |
| `[c]` / `[Ctrl-P]` | Run / Pause simulation loop |
| `[Click Label]` / `[Click Badge]` | Click active running badge to pause |
| `[b]` | Step back 1 instruction (Rollback tracking) |
| `[o]` / `[Alt-O]` | Open Binary / Disk image loader modal |
| `[,]` / `[Alt-S]` | Simulator Settings modal (CA/IA mode, rollback, logging) |
| `[Alt-M]` | Configure MISA CSR modal (Extensions A/B/C/D/F/M/V/S/U & VLEN) |
| `[y]` | Cycle-Accurate System Config modal |
| `[i]` | Memory inspector modal |
| `[m]` | Manage breakpoints and watchpoints |
| `[l]` / `[Alt-L]` | Cycle tool inspector tab (Pipe / Cache / BP / Hazard / TLB / Bus) |
| `[r]` / `[Alt-R]` | Cycle register tab (GPR / FPR / VEC) |
| `[g]` | Toggle guided inspection hints while paused |
| `[Tab]` | Cycle TUI layout |
| `[F1]` / `[h]` / `[?]` | Display online help shortcuts |
| `[Esc]` | Close active modal |

---

## Supported RISC-V Extensions

Both RV32GCBV and RV64GCBV instruction sets are supported.

See [RISC-V compliance scope](docs/RISCV_COMPLIANCE.md) for the precise architectural boundary,
SBI/OpenSBI distinction, and the evidence required before treating a feature as verified. The
profile names are implementation targets and do not by themselves claim RISC-V certification.
The cross-subsystem qualification status is summarized in the [2.0 support matrix](docs/SUPPORT_MATRIX.md).

| Extension | Status | Description & Features |
| --- | --- | --- |
| **I** | ✅ Supported | Base integer instruction set (RV32I / RV64I) |
| **M** | ✅ Supported | Integer multiplication, division, and remainder |
| **A** | ✅ Supported | Atomic memory operations (LR/SC, AMO word/doubleword) |
| **C** | ✅ Supported | Compressed instruction decode and execution |
| **F / D** | ⚠️ Qualification ongoing | Single/double precision and FP CSRs; RMM arithmetic remains a documented gap |
| **V** | ⚠️ Partial | Substantial RVV 1.0 subset with configurable VLEN (32–1024 bits); see compliance scope |
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

### Reproducing research evidence

Quick local validation uses installed dependencies and takes minutes after a build:

```bash
python3 scripts/reproduce.py --mode quick --output repro/results
```

The full RV32/RV64 correctness, Linux, vector, sanitizer, and performance workflow can take hours
and requires substantial build storage. It downloads pinned upstream sources into `.cache/repro`
but does not redistribute them. Exact preparation commands, schemas, and output contents are in
[the research companion guide](repro/README.md).

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
- `docs/RELEASE.md`: 2.0 support contract, validation matrix, and publishing checklist
- `docs/TUI.md`: TUI input focus, rendering layers, and test coverage
- `repro/`: Versioned experiment manifest and research-companion instructions
- `release/schemas/`: Machine-readable release and experiment interfaces

---

## License

SimRV is licensed under the [MIT License](LICENSE).
