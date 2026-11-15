# SimRV: Dual-Width Explainable RISC-V System Simulator

<p align="center">
  <strong>An explainable, dual-width (RV32 / RV64) RISC-V simulator with an interactive terminal workbench (TUI), cycle-accurate microarchitectural modeling, hardware RTL parity verification, runtime plugin APIs, and full-system SMP Linux emulation.</strong>
</p>

<p align="center">
  <a href="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/build-and-test.yml"><img src="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/build-and-test.yml/badge.svg?branch=dev" alt="Build and Test"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/docs.yml"><img src="https://github.com/archlab-sciencetokyo/SimRV/actions/workflows/docs.yml/badge.svg?branch=dev" alt="Docs CI"/></a>
  <a href="https://archlab-sciencetokyo.github.io/SimRV/stable/"><img src="https://img.shields.io/badge/docs-GitHub_Pages-blue.svg" alt="Documentation"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/releases"><img src="https://img.shields.io/github/v/release/archlab-sciencetokyo/SimRV?include_prereleases" alt="Latest SimRV release"/></a>
  <a href="https://github.com/archlab-sciencetokyo/SimRV/blob/main/LICENSE"><img src="https://img.shields.io/badge/license-MIT-green.svg" alt="License"/></a>
  <img src="https://img.shields.io/badge/C%2B%2B-23-purple.svg" alt="C++23"/>
  <a href="docs/architecture/compliance.md"><img src="https://img.shields.io/badge/profile-RVA23%20Target-blue.svg" alt="Profile RVA23 Target"/></a>
  <img src="https://img.shields.io/badge/SMP-1%20to%2016%20cores-brightgreen.svg" alt="SMP"/>
</p>

SimRV is an explainable, dual-width (RV32 / RV64) RISC-V research and education simulator featuring an interactive terminal workbench (TUI), cycle-accurate in-order pipeline modeling, cache hierarchy inspection, and full-system Linux OS emulation. It provides functional and cycle-accurate modes targeting the **RVA23 profile** (`rva23s64` default), with RV32 and narrower compatibility presets available for workloads and verification. The distributed RV64-capable binary also runs RV32 guests; the native RV32 target remains a strict-width verification build.

SimRV is not RISC-V certified. RVA23 is an implementation and verification target, not a formal certification claim; see the [compliance scope](https://archlab-sciencetokyo.github.io/SimRV/stable/architecture/compliance/) for verified coverage, ACT testing, and known gaps.

---

## Documentation

Browse the **[SimRV stable documentation](https://archlab-sciencetokyo.github.io/SimRV/stable/)** (or the [development docs](https://archlab-sciencetokyo.github.io/SimRV/dev/) for unreleased changes).

<table>
  <tr>
    <td width="50%" valign="top">
      <h3><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/user/install/">User Guide</a></h3>
      <p>Installation, CLI flags, interactive TUI workbench, bare-metal, Linux SMP, and classroom missions.</p>
      <ul>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/user/install/">Installation (TGZ, RPM, DEB, Source)</a></li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/user/">Quickstart &amp; CLI Reference</a></li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/user/tui/">Interactive TUI Workbench</a></li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/user/classroom/">Student Reference Guide</a></li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/architecture/overview/">Architecture &amp; Compliance</a></h3>
      <p>Core execution units, 3/5-stage pipelines, TileLink-C cache coherence, and ISA compliance scope.</p>
      <ul>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/architecture/overview/">System &amp; Pipeline Overview</a></li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/architecture/tilelink/">TileLink-C Coherence</a></li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/architecture/compliance/">ISA Compliance &amp; Boundaries</a></li>
      </ul>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/user/linux/">Bare-Metal &amp; Full-System Linux</a></h3>
      <p>Bare-metal firmware, memory maps, MMIO peripherals (16550A UART, CLINT/ACLINT, PLIC/AIA, VirtIO), and SMP Linux.</p>
      <ul>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/user/baremetal/">Bare-Metal &amp; Embedded</a></li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/user/linux/">Full-System Linux &amp; SMP</a></li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/user/ga-workflow/">Release Validation</a></li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/hardware/extensions/">Extensions, Models &amp; SDK</a></h3>
      <p>Custom Function Unit (CFU) plugins, MMIO device plugins, CPU/SoC presets, RTL parity, and C++ SDK.</p>
      <ul>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/hardware/extensions/">Custom Extensions &amp; CFU Plugins</a></li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/hardware/extending-platforms/">Extending CPU Presets &amp; MMIO Platforms</a></li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/hardware/models/">Microarchitectural Models &amp; Tuning</a></li>
        <li><a href="https://archlab-sciencetokyo.github.io/SimRV/stable/development/migration/">Migrating from 2.x to 3.0</a></li>
      </ul>
    </td>
  </tr>
</table>

---

## Installation

Pre-built Linux release packages (`x86_64` RPM, DEB, and portable `x86_64`/`aarch64` musl archives) are published on [GitHub Releases](https://github.com/archlab-sciencetokyo/SimRV/releases). Follow the **[installation guide](https://archlab-sciencetokyo.github.io/SimRV/stable/user/install/)** for package installation, upgrades, and source builds.

### Supported Host Platforms

- **Linux (`x86_64` and `aarch64`)**: Full support including portable static musl archives, RPM/DEB packages, VirtIO-FS, and `libslirp` user-mode networking.
- **macOS (`arm64` and `x86_64`)**: Source builds supported (with `libslirp` networking; VirtIO-FS is Linux-only).
- **Windows**: Supported via **WSL2** (native Windows is not supported due to POSIX terminal and process APIs).

### Building from Source

Requires **Clang 22+** or **GCC 16+** (C++23), **CMake 3.31+**, and **Ninja**:

```bash
cmake --preset rv64-release
cmake --build --preset rv64-release
```

The `rv64-release` build runs both RV64 and RV32 guest workloads. A strict-width `rv32-release` preset is also available for native 32-bit verification.

---

## Quick Start

Launch `simrv` normally (or pass `--tui`) to explore an image with the interactive TUI, or pass `--cli` for scripted and headless runs:

```bash
# Check host environment and qualified ISA summary
simrv --doctor
simrv --isa-info

# Interactive TUI workbench (default)
simrv -b -m hello.elf

# Headless execution with an instruction limit
simrv --cli -b -m hello.elf --steps 200000

# Cycle-accurate 5-stage or 3-stage pipeline execution
simrv --cli -b -m hello.elf --mode cycle-accurate --pipeline 5stage
simrv --cli -b -m hello.elf --mode cycle-accurate --pipeline 3stage

# Boot a full-system Linux image with disk and device tree
simrv --os --cli -m opensbi-linux-payload.elf -D rootfs.img -f devicetree.dtb

# Override ISA preset and Vector register length (VLEN)
simrv -m vector.elf --isa rv64gcv_zba_zbb_zbc_zbs --vlen 512
```

---

## Extensions, Plugins & SDK

SimRV can be extended at runtime without modifying or recompiling the simulator:

| Extension Mechanism | Interface | Use Case |
| --- | --- | --- |
| **CFU Plugins** (`--cfu-plugin`) | `SimRvCfuPlugin` C ABI (`simrv_cfu_init`) | Tightly-coupled `custom-0` accelerators with cycle-accurate latency |
| **Device Plugins** (`--plugin`) | `SimRvExternalDevice` C ABI (`simrv_device_plugin_init`) | Loosely-coupled MMIO peripherals on the TileLink interconnect |
| **CPU & SoC Models** (`--config`) | INI model & platform files | Custom pipeline widths, cache hierarchies, branch predictors, and memory maps |
| **C++ Runtime SDK** (`SimRV::runtime`) | Installed CMake package + headers | Embedding SimRV in external C++ harnesses and co-simulation tools |

Scaffold starter projects directly from the CLI:

```bash
# Starter CPU/SoC model configuration
simrv --scaffold-cpu-model my_core.cfg

# Starter Custom Function Unit (CFU) plugin C project
simrv --scaffold-cfu-plugin my_cfu ./my_cfu_plugin/

# Starter external MMIO device plugin C project
simrv --scaffold-device-plugin my_device ./my_device_plugin/
```

See [Custom Extensions & CFU Plugins](https://archlab-sciencetokyo.github.io/SimRV/stable/hardware/extensions/) and [Extending CPU Presets & MMIO Platforms](https://archlab-sciencetokyo.github.io/SimRV/stable/hardware/extending-platforms/) for the full C ABI and configuration reference.

---

## Interactive TUI & Classroom Mode

The TUI supports interactive stepping, breakpoints, live register/pipeline/cache/TLB inspection, instruction explanations (`e`), and an integrated guest terminal. Instructors can attach optional `.mission` files (`simrv mission <path.mission>`) for guided labs with named checkpoints and schema-versioned inspection exports (`x`).

| Hotkey | Action |
| --- | --- |
| `[s]` / `[Space]` | Single instruction step |
| `[c]` / `[Ctrl-P]` | Run / Pause simulation loop |
| `[o]` / `[Alt-O]` | Open Binary / Disk image loader modal |
| `[,]` / `[Alt-S]` | Simulator Settings modal (mode, SMP, scheduler, diagnostics) |
| `[Alt-M]` | Configure `misa` CSR modal (extensions & VLEN) |
| `[y]` | Cycle-Accurate Microarchitecture & Cache Config modal |
| `[i]` | Memory inspector modal |
| `[m]` | Manage breakpoints and watchpoints |
| `[l]` / `[r]` | Cycle subsystem inspector tab / register bank tab |
| `[e]` / `[?]` | Toggle instruction explanation / open contextual glossary |
| `[v]` / `[x]` | Toggle trace capture / export inspection report while paused |
| `[Tab]` | Switch right pane (Guest Terminal / Log Buffer) |
| `[F1]` / `[h]` / `[Esc]` | Help shortcuts / close active modal |

---

## Supported RISC-V Extensions

`rva23s64` is the primary architectural target; RV32 and narrower ISA presets are available for compatibility and verification workloads.

| Extension | Status | Description & Features |
| --- | --- | --- |
| **I** | ✅ Supported | Base integer instruction set (RV32I / RV64I) |
| **M** | ✅ Supported | Integer multiplication, division, and remainder |
| **A** | ✅ Supported | Atomic memory operations (`LR`/`SC`, `AMO` word/doubleword) |
| **C** | ✅ Supported | Compressed 16-bit instruction decode and execution |
| **F / D** | ⚠️ Qualification ongoing | Single/double precision and FP CSRs; RMM rounding remains a documented gap |
| **V** | ⚠️ Partial | Substantial RVV 1.0 subset with configurable VLEN (32–1024 bits); see compliance scope |
| **B** | ✅ Supported | Bit manipulation (`Zba`, `Zbb`, `Zbc`, `Zbs`) |
| **Privileged** | ✅ Supported | Machine, Supervisor, User modes (`M`/`S`/`U`), CSRs, traps, PMP |
| **Sv32 / Sv39 / Sv48** | ✅ Supported | Hardware MMU page table walker & TLB translation |

---

## Debugging & Co-Simulation

### GDB Remote Debugging

```bash
# Start SimRV with GDB RSP server on port 1234 (or --gdb-port <PORT>)
simrv -m hello.elf --gdb

# Connect from GDB
riscv64-unknown-elf-gdb hello.elf -ex "target remote :1234"
```

### Spike Lockstep Co-Simulation

Verify execution against Spike instruction-by-instruction:

```bash
simrv -m hello.elf --lockstep
```

Use `--spike-bin` and `--spike-elf` to override the Spike binary or comparison ELF. Lockstep and GDB are mutually exclusive.

---

## License

SimRV is licensed under the [MIT License](LICENSE).

## Citation

If you use SimRV in academic work, please cite the metadata in [`CITATION.cff`](CITATION.cff).
