# SimRV: Dual-Width Explainable RISC-V System Simulator



<p align="center">
  <a href="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml"><img src="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/c-cpp.yml/badge.svg?branch=main" alt="C/C++ CI"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/docs.yml"><img src="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/docs.yml/badge.svg?branch=main" alt="Docs CI"/></a>
  <a href="https://archlab-sciencetokyo.github.io/SimRV/"><img src="https://img.shields.io/badge/docs-GitHub_Pages-blue.svg" alt="Documentation"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/releases"><img src="https://img.shields.io/badge/version-2.0.2-blue.svg" alt="SimRV Version"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-green.svg" alt="License"/></a>
  <img src="https://img.shields.io/badge/C%2B%2B-23-purple.svg" alt="C++23"/>
  <img src="https://img.shields.io/badge/architecture-RV32%20%7C%20RV64-orange.svg" alt="Architecture"/>
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
      <p>CLI flags, interactive TUI controls, hotkeys, execution modes, and educational classroom missions.</p>
      <ul>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/user/">Quickstart & CLI Flags</a> (<a href="docs/user/index.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/user/tui/">TUI Controls & Keybindings</a> (<a href="docs/user/tui.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/user/classroom/">Classroom Missions</a> (<a href="docs/user/classroom.md">source</a>)</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>⚙️ <a href="https://archlab-sciencetokyo.github.io/SimRV/architecture/overview/">Architecture & Compliance</a></h3>
      <p>Core execution units, 6-stage in-order pipeline, cache hierarchy, MMU translation, and verified ISA scope.</p>
      <ul>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/architecture/overview/">Architectural Overview</a> (<a href="docs/architecture/overview.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/architecture/compliance/">RISC-V Compliance Scope</a> (<a href="docs/architecture/compliance.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/hardware/extensions/">Extension Implementation</a> (<a href="docs/hardware/extensions.md">source</a>)</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>🐧 <a href="https://archlab-sciencetokyo.github.io/SimRV/user/linux/">Bare-Metal & Linux</a></h3>
      <p>Bare-metal firmware, memory maps, MMIO peripherals (16550A UART, CLINT, PLIC, VirtIO block), and Linux OS boot.</p>
      <ul>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/user/baremetal/">Bare-Metal Development</a> (<a href="docs/user/baremetal.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/user/linux/">Booting Full-System Linux</a> (<a href="docs/user/linux.md">source</a>)</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>🛠️ <a href="https://archlab-sciencetokyo.github.io/SimRV/dev/contributing/">Development & Verification</a></h3>
      <p>Developer standards, dual-architecture CTest validation gates, Spike lockstep co-simulation, and release qualification.</p>
      <ul>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/dev/contributing/">Contributing Standards</a> (<a href="docs/dev/contributing.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/evaluation/reviewer/">Artifact Evaluation Guide</a> (<a href="docs/evaluation/reviewer.md">source</a>)</li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/evaluation/release/">Release Governance</a> (<a href="docs/evaluation/release.md">source</a>)</li>
      </ul>
    </td>
  </tr>
</table>

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
# Fast functional execution
./build/rv64-release/SimRV -b -m img/hello.bin --cli

# Cycle-accurate five-stage pipeline execution
./build/rv64-release/SimRV -b -m img/hello.bin --ca --cli
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

SimRV includes a rich terminal user interface (TUI) for hardware inspection, step-by-step instruction execution, and educational visualization. See the [educational reference](docs/user/classroom.md), [bare-metal guide](docs/user/baremetal.md), and [TUI guide](docs/user/tui.md).

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
Spike revision, image hash, and SimRV revision with experimental evidence. Lockstep
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
- `docs/`: Architecture and design notes (`docs/architecture/overview.md`, `docs/user/baremetal.md`)
- `CHANGELOG.md`: Version release log
- `docs/evaluation/release.md`: 2.0 support contract, validation matrix, and publishing checklist
- `docs/user/tui.md`: TUI input focus, rendering layers, and test coverage
- `repro/`: Versioned experiment manifest and research-companion instructions
- `release/schemas/`: Machine-readable release and experiment interfaces

---

## License

SimRV is licensed under the [MIT License](LICENSE).

## Citation

If you use SimRV in academic work, please cite the metadata in
[`CITATION.cff`](CITATION.cff).
