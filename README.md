# SimRV: Dual-Width Explainable RISC-V System Simulator

<p align="center">
  <strong>An explainable, dual-width (RV32 / RV64) RISC-V research simulator with an interactive terminal workbench (TUI), cycle-accurate microarchitectural modeling, hardware RTL parity verification, and full-system SMP Linux emulation.</strong>
</p>

<p align="center">
  <a href="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml"><img src="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml/badge.svg?branch=dev" alt="C/C++ CI"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/docs.yml"><img src="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/docs.yml/badge.svg?branch=dev" alt="Docs CI"/></a>
  <a href="https://archlab-sciencetokyo.github.io/SimRV/"><img src="https://img.shields.io/badge/docs-GitHub_Pages-blue.svg" alt="Documentation"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/releases"><img src="https://img.shields.io/badge/version-3.0.0--alpha.4-blue.svg" alt="SimRV Version"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-green.svg" alt="License"/></a>
  <img src="https://img.shields.io/badge/C%2B%2B-23-purple.svg" alt="C++23"/>
  <img src="https://img.shields.io/badge/architecture-RV32GCBV%20%7C%20RV64GCBV-orange.svg" alt="Architecture"/>
  <img src="https://img.shields.io/badge/SMP-2%20to%2016%20cores-brightgreen.svg" alt="SMP"/>
</p>

SimRV is an explainable, dual-width (RV32 / RV64) RISC-V research simulator featuring an interactive terminal workbench (TUI), cycle-accurate in-order pipeline modeling, cache hierarchy inspection, and full-system Linux OS emulation. It provides functional and cycle-accurate modes for compile-time fixed **RV64GCBV** and **RV32GCBV** implementation targets.

SimRV is not RISC-V certified. `RV32GCBV` and `RV64GCBV` are implementation targets; see the [compliance scope](docs/architecture/compliance.md) for verified coverage and known gaps.

---

## Documentation

Explore the full documentation, guides, and specifications online at **[archlab-sciencetokyo.github.io/SimRV](https://archlab-sciencetokyo.github.io/SimRV/)**:

<table>
  <tr>
    <td width="50%" valign="top">
      <h3>📖 <a href="https://archlab-sciencetokyo.github.io/SimRV/user/">User Guide</a></h3>
      <p>CLI flags, interactive TUI controls, hotkeys, execution modes, and student architecture guide.</p>
      <ul>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/user/">Quickstart & CLI Flags</a> (<a href="docs/user/index.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/user/tui/">TUI Controls & Keybindings</a> (<a href="docs/user/tui.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/user/classroom/">Student Reference Guide</a> (<a href="docs/user/classroom.md">source</a>)</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>⚙️ <a href="https://archlab-sciencetokyo.github.io/SimRV/architecture/overview/">Architecture & Models</a></h3>
      <p>Core execution units, 3/5-stage pipelines, TileLink-C cache coherence, CPU models, and RTL parity.</p>
      <ul>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/architecture/overview/">Architectural Overview</a> (<a href="docs/architecture/overview.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/architecture/tilelink/">TileLink-C Coherence</a> (<a href="docs/architecture/tilelink.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/hardware/models/">CPU Models & Calibration</a> (<a href="docs/hardware/models.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/hardware/rtl_parity/">RTL Parity Verification</a> (<a href="docs/hardware/rtl_parity.md">source</a>)</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🐧 <a href="https://archlab-sciencetokyo.github.io/SimRV/user/linux/">Bare-Metal & Linux</a></h3>
      <p>Bare-metal firmware, memory maps, MMIO peripherals (16550A UART, CLINT, PLIC, VirtIO block), and multi-hart SMP Linux.</p>
      <ul>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/user/baremetal/">Bare-Metal Development</a> (<a href="docs/user/baremetal.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/user/linux/">Booting SMP Linux</a> (<a href="docs/user/linux.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/architecture/compliance/">RISC-V Compliance Scope</a> (<a href="docs/architecture/compliance.md">source</a>)</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>🛠️ <a href="https://archlab-sciencetokyo.github.io/SimRV/dev/contributing/">Development & Verification</a></h3>
      <p>Developer standards, dual-architecture CTest validation gates, Spike lockstep co-simulation, and release qualification.</p>
      <ul>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/dev/contributing/">Contributing Standards</a> (<a href="docs/dev/contributing.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/dev/migration/">2.x to 3.0 Migration</a> (<a href="docs/dev/migration.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/evaluation/reviewer/">Artifact Evaluation Guide</a> (<a href="docs/evaluation/reviewer.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/evaluation/release/">Release Governance</a> (<a href="docs/evaluation/release.md">source</a>)</li>
      </ul>
    </td>
  </tr>
</table>

---

## Quick Start

### Interactive and headless runs

Launch `SimRV` normally (or pass `--tui`) to explore an image with the interactive TUI. Use
`--cli` for scripted and headless runs; command-line options are the supported run interface.

```bash
# Interactive image loading and inspection.
./build/rv64-release/SimRV -b -m img/hello.bin --tui

# Headless execution with an explicit instruction limit.
./build/rv64-release/SimRV -b -m img/hello.bin --cli --steps 200000
```

### Prerequisites

- **Clang 22+** or **GCC 16+** (required for the C++23 baseline).
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

Native-host and compiler-specific presets are also available. The native-host preset pins Clang
and enables host-specific code generation:

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
./build/rv64-release/SimRV --os -m linux-images/rv64/fw_payload.bin -D linux-images/rv64/root.img -f linux-images/rv64/devicetree.dtb --cli
```

Override MISA profile or Vector register length (VLEN):

```bash
# Select the explicit RV64GCBV target profile and a 512-bit VLEN
./build/rv64-release/SimRV -m img/vector.bin --misa rv64gcbv --vlen 512
```

---

## Interactive TUI

The TUI supports interactive stepping, breakpoints, and live hardware-state inspection.

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
| `[o]` / `[Alt-O]` | Open Binary / Disk image loader modal |
| `[,]` / `[Alt-S]` | Simulator Settings modal (Execution mode, SMP, scheduler, diagnostics) |
| `[Alt-M]` | Configure MISA CSR modal (Extensions A/B/C/D/F/M/V/S/U & VLEN) |
| `[y]` | Cycle-Accurate Microarchitecture & Cache Config modal |
| `[i]` | Memory inspector modal |
| `[m]` | Manage breakpoints and watchpoints |
| `[l]` / `[Alt-L]` | Cycle tool inspector tab (Pipe / Cache / BP / Hazard / TLB / Bus / IO / Stats) |
| `[r]` / `[Alt-R]` | Cycle register tab (GPR / FPR / VEC / CSR) |
| `[g]` | Show / hide the interactive Student Guide |
| `[Enter]` | Perform the Student Guide's suggested action when visible |
| `[e]` | Toggle instruction explanation |
| `[?]` | Open glossary at the active inspector topic |
| `[v]` | Toggle execution trace capture |
| `[x]` | Export configured inspection report while paused |
| `[Tab]` | Cycle right pane view (Guest Terminal / Log Buffer) |
| `[F1]` / `[h]` | Display online help shortcuts |
| `[Esc]` | Close active modal |

---

## Supported RISC-V Extensions

RV32GCBV and RV64GCBV are implementation-target names, not complete conformance claims.

See [RISC-V compliance scope](docs/architecture/compliance.md) for the precise architectural boundary,
the [TileLink-C profile](docs/architecture/tilelink.md) for protocol/coherence scope, and the
[3.0 migration guide](docs/dev/migration.md) for intentional host-interface breakage.
SBI/OpenSBI distinction, and the evidence required before treating a feature as verified. The
profile names are implementation targets and do not by themselves claim RISC-V certification.
The cross-subsystem qualification status is summarized in the
[release support boundary](docs/evaluation/release.md#support-and-qualification-boundary).

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

SimRV includes an event-driven GDB RSP server. `--gdb` selects CLI mode, starts the target paused,
and listens on port 1234 by default. Use `--gdb-port <PORT>` to choose another port. GDB and the
interactive TUI are separate debugging frontends, so explicit `--tui --gdb` is rejected; GDB is
also incompatible with Spike lockstep.

```bash
# Start SimRV with GDB server on port 1234
./build/rv64-release/SimRV -m path/to/hello.bin --gdb

# Connect from GDB in another terminal
riscv64-unknown-elf-gdb hello.elf -ex "target remote :1234"
```

The server uses all-stop multi-hart semantics. GDB breakpoints are logical (guest memory is not
patched), disconnects leave the target paused and restart the listener, and `detach` removes
GDB-owned breakpoints/watchpoints before resuming. In the TUI, breakpoints, watchpoints, stepping,
and inspection are always available while paused—there is no separate debug-mode toggle.

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
- `docs/`: Focused architecture, user, contributor, compliance, and release guides
- `CHANGELOG.md`: Version release log
- `docs/hardware/models.md`: CPU model configuration framework, parameters, wizard, and RTL calibration
- `docs/evaluation/release.md`: 2.0/3.0 support contract, validation matrix, and publishing checklist
- `docs/user/tui.md`: TUI input focus, rendering layers, and test coverage
- `repro/`: Research-companion scripts and reproducibility instructions
- `release/`: Release metadata, evidence schemas, and publishing inputs

---

## License

SimRV is licensed under the [MIT License](LICENSE).

## Citation

If you use SimRV in academic work, please cite the metadata in
[`CITATION.cff`](CITATION.cff).
