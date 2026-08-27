# SimRV

[![C/C++ CI](https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml/badge.svg?branch=dev)](https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml)

SimRV is a C++23 RISC-V simulator for computer-architecture research and education. It provides
instruction and cycle execution, RV32/RV64 targets, Linux support, and an interactive TUI for
inspecting pipelines, caches, memory, and architectural state.

SimRV is not RISC-V certified. `RV32GCBV` and `RV64GCBV` are implementation targets; see the
[compliance scope](docs/RISCV_COMPLIANCE.md) for verified coverage and known gaps.

---

## Quick Start

### Interactive versus reproducible runs

The interactive TUI remains the quickest way to explore SimRV: launch `SimRV` normally (or pass
`--tui`) and load an image from the UI.  Use a versioned manifest for headless, automated, or
publication runs so the full configuration and structured results are recorded:

```bash
# Validate and inspect a reproducible run definition.
./build/rv64-release/SimRV validate experiment.toml
./build/rv64-release/SimRV inspect experiment.toml

# Headless execution writes manifest.resolved.toml, events.jsonl, and result.json.
./build/rv64-release/SimRV run experiment.toml

# A manifest can also launch the TUI when a reproducible interactive session is needed.
./build/rv64-release/SimRV tui experiment.toml
```

The legacy command-line flags remain available for the lightweight interactive workflow during the
2.x transition; manifests are the canonical interface for CLI automation.

### Prerequisites
- **Clang 20+** or **GCC 15+** (required for the C++23 baseline). For current validation,
  prefer stable Clang 21+ and GCC 15+; use GCC 16+ once supplied by the host distribution.
- **CMake 3.31+** & **Ninja**

### Build

```bash
# RV64 build (Default)
cmake --preset rv64-release
cmake --build --preset rv64-release

# RV32 build
cmake --preset rv32-release
cmake --build --preset rv32-release
```

Native-host and compiler-specific presets are also available:
```bash
cmake --preset rv64-native-release && cmake --build --preset rv64-native-release
cmake --preset rv64-clang-release && cmake --build --preset rv64-clang-release
cmake --preset rv64-gcc-release && cmake --build --preset rv64-gcc-release
```

Repeatable analysis presets are also available:
```bash
cmake --preset rv64-asan && cmake --build --preset rv64-asan
cmake --preset rv64-tidy && cmake --build --preset rv64-tidy
```
If a host ccache wrapper has no writable cache, prefix configure and build commands with
`CCACHE_DISABLE=1`.

### Run

Run a baremetal binary in interactive TUI mode (Default):
```bash
./build/rv64-release/SimRV -b -m img/hello.bin
```

Run headless in CLI-only mode:
```bash
./build/rv64-release/SimRV -b -m img/hello.bin --cli
```

Select execution mode across fast, detailed, or cycle-accurate microarchitectures:
```bash
# Fast functional execution
./build/rv64-release/SimRV -b -m img/hello.bin --mode fast --cli

# Cycle-accurate 4-stage pipeline execution
./build/rv64-release/SimRV -b -m img/hello.bin --mode cycle-accurate --cli

# Microarchitecture targets (3-stage, dual-issue, 5-stage)
./build/rv64-release/SimRV -b -m img/hello.bin --mode dual-issue --smp 2 --cli
```

Mirror configuration, diagnostics, termination, cache, bus, and performance summaries to a log:

```bash
./build/rv64-release/SimRV -b -m img/hello.bin --mode cycle-accurate --cli --log-file run.log
```

Run Linux OS image with disk & devicetree:
```bash
./build/rv64-release/SimRV --os -m linux-images/rv64/fw_payload.bin -D linux-images/rv64/root.img -c linux-images/rv64/devicetree.dtb --cli
```

Override MISA profile or Vector register length (VLEN):
```bash
# Select the explicit RV64GCBV target profile and a 512-bit VLEN
./build/rv64-release/SimRV -m img/vector.bin --misa rv64gcbv --vlen 512
```

---

## Interactive TUI

The TUI supports interactive stepping, breakpoints, and live hardware-state inspection.

### Key Shortcuts

| Hotkey | Action |
| --- | --- |
| `[s]` / `[Space]` | Single instruction step |
| `[c]` / `[Ctrl-P]` | Run / Pause simulation loop |
| `[Click Label]` / `[Click Badge]` | Click active running badge to pause |
| `[o]` / `[Alt-O]` | Open Binary / Disk image loader modal |
| `[,]` / `[Alt-S]` | Simulator Settings modal (Execution mode, SMP, scheduler, diagnostics) |
| `[Alt-M]` | Configure MISA CSR modal (Extensions A/B/C/D/F/M/V/S/U & VLEN) |
| `[y]` | Cycle-Accurate Microarchitecture & Cache Config modal |
| `[i]` | Memory inspector modal |
| `[m]` | Manage breakpoints and watchpoints |
| `[l]` / `[Alt-L]` | Cycle tool inspector tab (Pipe / Cache / BP / Hazard / TLB / Bus / IO / Stats) |
| `[r]` / `[Alt-R]` | Cycle register tab (GPR / FPR / VEC / CSR) |
| `[g]` | Toggle Educational Glossary modal |
| `[Ctrl-A]` | Toggle input focus between guest UART/PTY and TUI navigation |
| `[Tab]` | Cycle right pane view (Guest Terminal / Log Buffer) |
| `[F1]` / `[h]` / `[?]` | Display online help shortcuts |
| `[Esc]` | Close active modal |

---

## Supported RISC-V Extensions

RV32GCBV and RV64GCBV are implementation-target names, not complete conformance claims.

See [RISC-V compliance scope](docs/RISCV_COMPLIANCE.md) for the precise architectural boundary,
SBI/OpenSBI distinction, and the evidence required before treating a feature as verified. The
profile names are implementation targets and do not by themselves claim RISC-V certification.
The cross-subsystem qualification status is summarized in the
[release support boundary](docs/RELEASE.md#support-and-qualification-boundary).

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
- `docs/`: Focused architecture, user, contributor, compliance, and release guides
- `CHANGELOG.md`: Version release log
- `docs/RELEASE.md`: 2.0 support contract, validation matrix, and publishing checklist
- `docs/TUI.md`: TUI input focus, rendering layers, and test coverage
- `repro/`: Versioned experiment manifest and research-companion instructions
- `release/schemas/`: Machine-readable release and experiment interfaces

---

## License

SimRV is licensed under the [MIT License](LICENSE).
