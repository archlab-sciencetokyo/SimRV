# SimRV

[![C/C++ CI](https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml/badge.svg?branch=dev)](https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml)

SimRV is a C++23 research simulator for studying RISC-V functional behavior, in-order pipeline
timing, memory-system behavior, and interactive architecture education. It provides functional and
cycle-accurate modes for the **RV64GCBV** and **RV32GCBV** implementation targets, plus a TUI,
guest stack analysis, cache inspectors, configurable MISA state, and a small virtual platform.

SimRV is not RISC-V certified. `RV32GCBV` and `RV64GCBV` are implementation targets; see the
[compliance scope](docs/architecture/compliance.md) for verified coverage and known gaps.

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

Select execution mode across fast, detailed, or cycle-accurate microarchitectures:

```bash
# Fast functional execution
./build/rv64-release/SimRV -b -m img/hello.bin --mode fast --cli

# Cycle-accurate five-stage pipeline execution
./build/rv64-release/SimRV -b -m img/hello.bin --mode cycle-accurate --cli

# Choose the three-stage educational pipeline.
./build/rv64-release/SimRV -b -m img/hello.bin --mode cycle-accurate --pipeline 3stage --cli
```

Mirror configuration, diagnostics, termination, cache, bus, and performance summaries to a log:

```bash
./build/rv64-release/SimRV -b -m img/hello.bin --mode cycle-accurate --cli --log-file run.log
```

Load custom or preset CPU microarchitecture models (see [CPU Models Guide](docs/hardware/models.md)):

```bash
# Load a predefined CPU model (searches configs/models/ or custom path)
./build/rv64-release/SimRV -b -m img/hello.bin --mode cycle-accurate --cpu-profile rvcomp --cli

# Generate a scaffold model configuration with the wizard
python3 scripts/cpu_model_wizard.py --name my_core --template five-stage
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

### Classroom integration

SimRV accepts ordinary RISC-V ELF files and needs no course-specific lesson format. Instructors can
use the same command across exercises; `--class` starts the TUI paused with the interactive Student
Guide visible, while students remain free to inspect any subsystem.

```bash
./build/rv64-release/SimRV --tui --baremetal -m exercise.elf --class \
  --inspection-output inspection.json
```

The optional external `control-flow-calls.mission` lesson turns the Student Guide into a local
sequence of branch, loop, call, return, and ABI observations. Build the supplied example first,
then pass the lesson path to `--class --mission`. Missions do not collect identity or grading data,
and students remain free to use the normal TUI controls.

Students can load (`o`), step (`s`), inspect (`r`/`l`), explain (`e`), open the relevant glossary
topic (`?`), and trace (`v`) without changing the workload. The Student Guide proposes a
context-sensitive next action; `Enter` performs it and `g` shows or hides the guide. Pressing `x`
while paused writes the configured, schema-versioned inspection report; existing files require a
second explicit export action. See the [educational reference](docs/user/classroom.md),
[bare-metal guide](docs/user/baremetal.md), and [source-first ISA examples](examples/isa/).

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

See [RISC-V compliance scope](docs/architecture/compliance.md) for the precise architectural boundary,
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

Lockstep is a verification workflow for reproducible experiments, not an
interactive TUI feature. Use a caller-supplied Spike built for the same XLEN
and ISA profile as the image under test; `--spike-bin` selects a non-default
binary and `--spike-elf` selects its comparison image. Keep the command line,
Spike revision, image hash, and SimRV revision with paper evidence. Lockstep
and GDB are intentionally mutually exclusive.

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
- `docs/hardware/models.md`: CPU model configuration framework, parameters, wizard, and RTL calibration
- `docs/evaluation/release.md`: 2.0 support contract, validation matrix, and publishing checklist
- `docs/user/tui.md`: TUI input focus, rendering layers, and test coverage
- `repro/`: Versioned experiment manifest and research-companion instructions
- `release/schemas/`: Machine-readable release and experiment interfaces

---

## License

SimRV is licensed under the [MIT License](LICENSE).

## Citation

If you use SimRV in academic work, please cite the metadata in
[`CITATION.cff`](CITATION.cff), which credits Lennart Trunk and Prof. Kenji Kise.
