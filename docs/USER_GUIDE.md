# SimRV 3.0 User Guide

SimRV is an explainable, dual-width (RV32 / RV64) RISC-V architectural simulator with an interactive terminal workbench (TUI), cycle-accurate microarchitectural modeling, hardware RTL parity verification, and full-system SMP Linux emulation.

---

## Table of Contents

1. [Architecture & Features Overview](#1-architecture--features-overview)
2. [Installation & Quickstart](#2-installation--quickstart)
3. [Command-Line Interface (CLI)](#3-command-line-interface-cli)
4. [Interactive TUI Workbench](#4-interactive-tui-workbench)
5. [Bare-Metal & Embedded Simulation](#5-bare-metal--embedded-simulation)
6. [Full-System Linux Emulation](#6-full-system-linux-emulation)
7. [CPU Model Profiles & Microarchitecture Tuning](#7-cpu-model-profiles--microarchitecture-tuning)
8. [RTL Parity Verification](#8-rtl-parity-verification)
9. [Benchmarking Suite](#9-benchmarking-suite)
10. [Troubleshooting & Reference](#10-troubleshooting--reference)

---

## 1. Architecture & Features Overview

SimRV simulates standard 32-bit and 64-bit RISC-V systems:

- **ISA Support**: RV32GCBV / RV64GCBV (Base Integer `I`/`E`, Standard Multiply/Divide `M`, Atomic `A`, Single/Double Floating Point `F`/`D`, Compressed `C`, Bit Manipulation `B`, and Vector 1.0 `V`).
- **Privilege & System Architecture**: Machine (M), Supervisor (S), and User (U) privilege modes with PMP (Physical Memory Protection), Sv32 / Sv39 MMU page translation, CLINT / ACLINT timer and software interrupts, and PLIC / AIA (APLIC/IMSIC) interrupt routing.
- **Microarchitectural Modeling**: Per-cycle transition kernels for three-stage, five-stage, and dual-issue pipelines with configurable branch prediction (static, bimodal, 2-level adaptive, RAS, BTB) and multi-level L1/L2/L3 cache hierarchies.
- **Multi-Hart Coherence**: TileLink-C directory-based coherence hubs modeling MESI protocols for SMP configurations (2 to 16 harts).
- **RTL Parity Framework**: Cycle-accurate equivalence validation against physical Verilog / Verilator RTL designs.
- **Interactive TUI**: Educational split-screen monitor displaying register banks, pipeline slots, cache lines, hazard graphs, disassembly explainers, and an interactive Linux PTY terminal.

---

## 2. Installation & Quickstart

### Prerequisites

- Modern C++23 compiler: **Clang 20+** or **GCC 15+**
- Build tools: **CMake 3.31+**, **Ninja**, **mold** (recommended for ultra-fast linking)
- Python: **Python 3.10+** (for utility tools and test suites)

### Building from Source

SimRV requires using standard CMake presets:

```bash
# Clone repository
git clone https://github.com/archlab-sciencetokyo/SimRV.git
cd SimRV

# Build RV64 Release (default target)
cmake --preset rv64-release
cmake --build --preset rv64-release -j$(nproc)

# Build RV32 Release
cmake --preset rv32-release
cmake --build --preset rv32-release -j$(nproc)
```

The resulting simulator executable is located at `build/rv64-release/SimRV` (or `build/rv32-release/SimRV`).

### Installing System-Wide

```bash
# Install SimRV binary, tools, models, schemas, and documentation
sudo cmake --install build/rv64-release --prefix /usr/local
```

Installed files include:

- Executables: `/usr/local/bin/SimRV`, `/usr/local/bin/simrv-cpu-wizard`, `/usr/local/bin/simrv-tune`, `/usr/local/bin/simrv-parity`, `/usr/local/bin/simrv-benchmark`
- CPU Model Templates: `/usr/local/share/SimRV/models/`
- JSON Schemas: `/usr/local/share/SimRV/schemas/`
- Documentation & Man Page: `/usr/local/share/doc/SimRV/USER_GUIDE.md`, `/usr/local/share/man/man1/simrv.1`

---

## 3. Command-Line Interface (CLI)

By default, launching `SimRV` without arguments launches the interactive TUI workbench. For non-interactive batch scripts or automated testing, provide `--cli`.

### Common Invocations

```bash
# Run a bare-metal ELF binary in fast headless CLI mode
SimRV --cli -m program.elf

# Run in cycle-accurate (CA) mode with step limit
SimRV --cli --ca -m program.elf -s 1000000

# Load a custom CPU model profile
SimRV --cli --ca --cpu-profile configs/models/rvcomp.cfg -m program.elf

# Launch TUI workbench with loaded binary
SimRV -m program.elf
```

### Key CLI Options

| Option | Description |
| :--- | :--- |
| `-c, --cli` | Force headless command-line execution (disables TUI). |
| `--tui` | Force interactive TUI workbench (default). |
| `-m, --memory <file.elf>` | Load ELF executable into guest memory. |
| `--ca` | Select cycle-accurate pipeline simulation kernel. |
| `--ia` | Select fast instruction-accurate simulation mode. |
| `-s, -e, --steps <N>` | Evaluate machine-wide instruction limit across all harts before stopping. |
| `--cpu-profile <path.cfg>` | Load human-editable CPU microarchitecture profile. |
| `-p, --pipeline <type>` | Pipeline microarchitecture target (`three-stage`, `five-stage`, `dual-issue`). |
| `-H, --tohost <addr>` | Specify physical address of `tohost` communication symbol for tests. |
| `--trace` | Write architectural instruction trace to `trace/trace.txt`. |
| `--tracepc` | Write PC stream trace to `trace/tracepc.txt`. |
| `--gdb` | Start GDB Remote Serial Protocol (RSP) server. |
| `--gdb-port <port>` | Set GDB RSP TCP listener port (default: 1234). |
| `-v, --version` | Display version and build information. |
| `-h, --help` | Show full command-line help message. |

---

## 4. Interactive TUI Workbench

The SimRV TUI provides an educational visual inspection environment for architecture students and hardware engineers.

```
┌─ SimRV 3.0 ─────────────────────────────────────────── [Hart 0: RUNNING] ─┐
│ [Regs] [Cache] [TLB] [Pipeline] [Hazards] [Explainer] │ Guest Terminal PTY │
│                                                      │                    │
│ PC: 0x80000000   ra: 0x00000000   sp: 0x80010000     │ Linux version 7.2  │
│ IF: [0x80000020] addi a0, a0, 1                      │ buildroot login:   │
│ ID: [0x8000001c] lw   a1, 0(sp)                      │                    │
│ EX: [0x80000018] mul  a2, a1, a0                     │                    │
│ MEM:[0x80000014] sw   s0, 4(sp)                      │                    │
│ WB: [0x80000010] addi sp, sp, -16                    │                    │
├──────────────────────────────────────────────────────┴────────────────────┤
│ [F1:Help] [F2:Load] [F5:Run] [F6:Step] [F8:Break] [Alt-M:MISA] [F4:Presets]
└───────────────────────────────────────────────────────────────────────────┘
```

### Focus & Input Navigation

- **Input Focus**: Keyboard input is automatically linked to simulation state: when running, input routes directly to the guest terminal (PTY / UART); when paused, keystrokes control TUI navigation and inspector panels.
- **`[Tab]` / `[Shift-Tab]`**: Switch active sub-views within the left inspector pane.
- **`[F5]` / `[c]` / `[Ctrl-P]`**: Run / Pause simulation execution.
- **`[F6]` / `[s]`**: Step one cycle machine-wide across all harts.
- **`[q]` / `[Ctrl-C]`**: Quit simulator.

### Visualizer Panes

1. **Registers (`Regs`)**: Real-time display of integer registers (`x0`–`x31`), floating-point registers (`f0`–`f31`), and vector registers (`v0`–`v31`).
2. **Pipeline View**: In-flight stage slots (`IF`, `ID`, `EX`, `MEM`, `WB`) annotated with instruction mnemonic, data hazard stalls, and branch mispredict bubbles.
3. **Cache Inspector**: Multi-way cache visualization for ICache and DCache, showing set indices, tags, hit/miss markers, and MESI line state flags (`[M]`, `[E]`, `[S]`, `[I]`).
4. **Instruction Explainer**: Architectural breakdown of the current instruction, explaining bitfields, immediate decoding, register operands, IEEE-754 FP flags, or vector group configurations.
5. **Memory & Stack**: Guest memory hex viewer and call stack frame resolution.

---

## 5. Bare-Metal & Embedded Simulation

SimRV can execute freestanding bare-metal ELFs compiled with standard RISC-V GCC or Clang toolchains.

```bash
# Run baremetal ELF with standard tohost exit reporting
SimRV --cli -m build/my_program.elf -H 0x80001000
```

### Hardware Peripherals Emulated

- **16550A UART**: Serial I/O mapped at base address `0x10000000`.
- **CLINT**: Core Local Interruptor at base address `0x02000000` providing `mtime` and `mtimecmp` registers (10 MHz timebase).
- **PLIC**: Platform-Level Interrupt Controller at base address `0x0c000000` supporting priority thresholds and interrupt claims.

---

## 6. Full-System Linux Emulation

SimRV boots full Linux distributions with OpenSBI and dynamic device-tree generation.

```bash
# Launch Linux with OpenSBI and root filesystem image
SimRV --kernel linux-images/Image \
      --disk linux-images/root.img \
      --smp 2
```

### Full-System CLI Options

- `--kernel <Image>`: Linux kernel image.
- `--disk <root.img>`: Ext4 root filesystem image (attached as `/dev/vda` via VirtIO Block).
- `--smp <2..16>`: Number of active SMP harts (default is 1 for single-hart execution).
- `--dtb <virt.dtb>`: Optional custom Device Tree Blob (SimRV automatically synthesizes a device tree if omitted).
- `--ram <MB>`: Guest RAM capacity in megabytes (default: 2048 MB).

---

## 7. CPU Model Profiles & Microarchitecture Tuning

SimRV allows defining custom processor pipelines and timing parameters in human-editable `.cfg` files.

### Interactive Model Wizard (`simrv-cpu-wizard`)

Create and customize microarchitectural models interactively:

```bash
# Run interactive CLI wizard
simrv-cpu-wizard

# Generate from a template directly
simrv-cpu-wizard --template rvcomp --name my_core --output configs/models/my_core.cfg

# Validate configuration syntax against schema
simrv-cpu-wizard --validate configs/models/my_core.cfg
```

### Automatic Model Calibration (`simrv-tune`)

Automatically calibrate pipeline execution latencies, branch penalties, and cache configurations against RTL or hardware execution traces:

```bash
simrv-tune --base-config configs/models/rvcomp.cfg --apply
```

---

## 8. RTL Parity Verification

The RTL parity framework (`simrv-parity`) validates that SimRV cycle-accurate pipeline models produce identical cycle counts and execution behavior compared to physical RTL designs.

```bash
# List available RTL targets
simrv-parity --list-targets

# Run parity checks against CFU Proving Ground RVProc
simrv-parity cfu-pg --benchmark all

# Run parity checks against RVComp core
simrv-parity rvcomp --quick
```

For more details on registering new hardware RTL targets, refer to [docs/RTL_PARITY.md](RTL_PARITY.md).

---

## 9. Benchmarking Suite

The `simrv-benchmark` suite measures simulation throughput (KIPS/MIPS), memory footprint (RSS), and compares results against Spike or previous SimRV versions.

```bash
# Run standard realworld benchmarks
simrv-benchmark --suite realworld

# Compare performance against Spike
simrv-benchmark --suite realworld --spike $(which spike) --output results.json

# Generate LaTeX performance comparison table
simrv-benchmark --suite realworld --latex-table
```

---

## 10. Troubleshooting & Reference

### Common Questions

- **Q: How does keyboard input routing work between Linux and the TUI?**
  *A:* Keyboard input routes automatically based on execution state: while running, keystrokes are delivered directly to the guest terminal; while paused (via `F5` or `Ctrl-P`), keystrokes control simulator inspection and navigation.

- **Q: Why does headless CLI mode finish without displaying the TUI?**
  *A:* The `--cli` flag enforces non-interactive headless operation. To see the graphical TUI workbench, run `SimRV` without `--cli`.

- **Q: Where are instruction traces written when passing `--trace`?**
  *A:* Traces are saved in the `trace/` directory relative to your working directory (`trace/trace.txt`, `trace/tracepc.txt`).

- **Q: How can I connect GDB to debug a running binary?**
  *A:* Launch SimRV with `--gdb --cli -m program.elf`, then in another terminal run:
  `riscv64-unknown-elf-gdb program.elf -ex "target remote :1234"`

### Detailed Architecture & Extension Guides

- [System Architecture](ARCHITECTURE.md)
- [Bare-Metal Guide](BAREMETAL_GUIDE.md)
- [CPU Model Configuration Reference](CPU_MODELS.md)
- [RTL Parity Verification Guide](RTL_PARITY.md)
- [TileLink-C Profile & Cache Coherence](TILELINK_C_PROFILE.md)
- [RISC-V Compliance Scope](RISCV_COMPLIANCE.md)
