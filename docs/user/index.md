# SimRV 3.0 User Guide

SimRV is an explainable, dual-width (RV32 / RV64) RISC-V architectural simulator with an interactive terminal workbench (TUI), cycle-accurate microarchitectural modeling, hardware RTL parity verification, and full-system SMP Linux emulation.

---

## Table of Contents

1. [Architecture & Features Overview](#1-architecture-features-overview)
2. [Installation & Quickstart](#2-installation-quickstart)
3. [Command-Line Interface (CLI)](#3-command-line-interface-cli)
4. [Interactive TUI Workbench](#4-interactive-tui-workbench)
5. [Bare-Metal & Embedded Simulation](#5-bare-metal-embedded-simulation)
6. [Full-System Linux Emulation](#6-full-system-linux-emulation)
7. [CPU Model Presets & Microarchitecture Tuning](#7-cpu-model-presets-microarchitecture-tuning)
8. [RTL Parity Verification](#8-rtl-parity-verification)
9. [Benchmarking Suite](#9-benchmarking-suite)
10. [Troubleshooting & Reference](#10-troubleshooting-reference)

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

For the portable archive and native RPM or DEB packages, start with the dedicated
[installation guide](install.md). The instructions below cover source builds.

### Prerequisites

- Modern C++23 compiler: **Clang 22+** or **GCC 16+**
- Build tools: **CMake 3.31+**, **Ninja**, **mold** (recommended for fast linking)
- Python: **Python 3.10+** (for utility tools and test suites)

### Building from Source

SimRV requires using standard CMake presets:

```bash
# Clone repository
git clone https://github.com/archlab-sciencetokyo/SimRV.git
cd SimRV

# Build the normal RV64-capable release target
cmake --preset rv64-release
cmake --build --preset rv64-release -j$(nproc)

# Optional: build the strict-width RV32 verification target
cmake --preset rv32-release
cmake --build --preset rv32-release -j$(nproc)
```

The normal executable is `build/rv64-release/simrv`. It runs RV64 guests and RV32 guests; the
separate `build/rv32-release/simrv` target is primarily retained for strict-width validation.

### Installing System-Wide

```bash
# Install SimRV binary, tools, models, schemas, and documentation
sudo cmake --install build/rv64-release --prefix /usr/local
```

Installed files include:

- Executables: `/usr/local/bin/simrv`, `/usr/local/bin/simrv-cpu-wizard`, `/usr/local/bin/simrv-tune`, `/usr/local/bin/simrv-parity`, `/usr/local/bin/simrv-benchmark`
- CPU Model Templates: `/usr/local/share/SimRV/models/`
- JSON Schemas: `/usr/local/share/SimRV/schemas/`
- Documentation & Man Page: `/usr/local/share/doc/SimRV/USER_GUIDE.md`, `/usr/local/share/man/man1/simrv.1`

---

## 3. Command-Line Interface (CLI)

By default, launching `simrv` without arguments launches the interactive TUI workbench. For non-interactive batch scripts or automated testing, provide `--cli`.

### Common Invocations

```bash
# Run a bare-metal ELF binary in fast headless CLI mode
simrv --cli -m program.elf

# Run in cycle-accurate (CA) mode with step limit
simrv --cli --ca -m program.elf -s 1000000

# Load a custom CPU model preset
simrv --cli --ca --cpu-preset configs/models/rvcomp.cfg -m program.elf

# Launch TUI workbench with loaded binary
simrv -m program.elf
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
| `--cpu-preset <path.cfg>` | Load human-editable CPU microarchitecture preset. |
| `-p, --pipeline <type>` | Pipeline microarchitecture target (`three-stage`, `five-stage`, `dual-issue`). |
| `-H, --tohost <addr>` | Specify physical address of `tohost` communication symbol for tests. |
| `--summary <file>` | Write a machine-readable JSON execution summary when the run ends. |
| `--events <file>` | Write newline-delimited lifecycle events for automation. |
| `--arch-trace <file>` | Write a versioned JSONL retirement trace for RTL/Spike comparison. |
| `--trace-level <0-4>` | Limit architectural event detail: devices, calls, retirement, or detailed state. |
| `--trace` | Write an aligned, labeled architectural instruction trace to `<trace-dir>/trace.txt`. |
| `--tracepc` | Write PC stream trace to `<trace-dir>/tracepc.txt`. |
| `--gdb` | Start GDB Remote Serial Protocol (RSP) server. |
| `--gdb-port <port>` | Set GDB RSP TCP listener port (default: 1234). |
| `--isa-info` | Show the qualified ISA, vector, privilege, and debugger capability contract. |
| `--doctor` | Diagnose terminal presentation and portable-runtime conditions. |
| `-v, --version` | Display version and build information. |
| `-h, --help` | Show full command-line help message. |

### Automation summary

Use `--summary` when a script needs architectural results without parsing human-readable logs:

```bash
simrv --cli --baremetal -m program.elf --summary results/run.json
python3 -m json.tool results/run.json
```

The JSON document has schema version `1` and includes the simulator version, XLEN, hart count,
execution engine, stop reason, exit status, final PC, retired instructions, cycles, CPI, IPC,
per-hart retirement counts, cache hit/miss counts, branch prediction outcomes, and bus traffic.
The summary is written after execution; a failure to write it
causes a nonzero simulator exit status. `--summary -` is rejected so guest UART output remains
unambiguous on stdout.

For streaming automation, `--events` writes one JSON object per lifecycle transition. Events
include `started`, `stopped`, `reboot_requested`, and `exit_requested`, with schema version,
timestamp, status, stop reason, hart, PC, retired instructions, and cycles. Like summaries,
`--events -` is rejected because stdout belongs to guest UART output.

For instruction-by-instruction architectural comparison, use the opt-in JSONL trace:

```bash
simrv --cli -m program.elf --arch-trace results/retire.jsonl --steps 10000
```

The first record identifies the trace schema, XLEN, VLEN, and hart count. Each subsequent
`retire` record contains the hart, cycle, cumulative retired count, instruction PC and encoding,
decoded operation, next PC, and privilege mode. The option selects detailed execution so every
committed instruction is represented; it is intended for RTL/Spike parity work and is not a
throughput mode.

Architectural tracing keeps `retire.jsonl` compact and compatibility-oriented. It does not repeat
the full CSR/vector snapshot on every line. Additional streams are written beside it:
`calls.jsonl`, `devices.jsonl`, and `metadata.json`. Lifecycle events remain in the separate file
selected by `--events`.

The default trace level is 3. Level 1 enables trap, exception, and device events; level 2 adds
calls and returns; level 3 adds instruction retirement; level 4 is reserved for detailed register
and memory events. New streams use a schema-2 envelope with RISC-V privilege-mode names (`U`,
`S`, and `M`):

```json
{"schema_version":2,"event":"mmio_write","timestamp":"2026-10-05T12:34:56.123Z",
 "cycle":18420,"hart":0,"pc":"0x800125bc","mode":"M","component":"uart0",
 "payload":{"address":"0x40001000","value":"0x1","width":1,"access":"mmio"}}
```

Calls are direct or indirect `jal`/`jalr` instructions writing `ra`; returns are `jalr x0, ra, 0`.
The simulator records PCs and dynamic depth, leaving symbol and source resolution to downstream
ELF-aware tools:

```json
{"schema_version":2,"event":"call","timestamp":"2026-10-05T12:34:56.123Z",
 "cycle":42,"hart":0,"mode":"M","payload":{"source_pc":"0x80000000",
 "target_pc":"0x80000120","return_pc":"0x80000004","call_depth":1}}
{"schema_version":2,"event":"return","timestamp":"2026-10-05T12:34:56.124Z",
 "cycle":57,"hart":0,"mode":"M","payload":{"source_pc":"0x80000124",
 "target_pc":"0x80000004","return_pc":"0x80000004","call_depth":0}}
```

### Checkpoint and resume

Save a versioned architectural snapshot when a run ends and resume it with the same machine
configuration:

```bash
simrv --cli -m program.elf --save-checkpoint run.ckpt --steps 1000000
simrv --cli -m program.elf --load-checkpoint run.ckpt --steps 1000000
```

Snapshots contain guest DRAM, hart registers and CSRs, retirement counters, and `mcycle`. The
XLEN, VLEN, hart count, and DRAM geometry must match. Device queues, host sockets, and external
time are intentionally not serialized; use this for deterministic bare-metal and architectural
experiments, not transparent VM migration.

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
simrv --cli -m build/my_program.elf -H 0x80001000
```

### Hardware Peripherals Emulated

- **16550A UART**: Serial I/O mapped at base address `0x10000000`.
- **CLINT**: Core Local Interruptor at base address `0x02000000` providing `mtime` and
  `mtimecmp` registers (10 MHz timebase). In cycle-accurate mode each timer tick corresponds to 16
  cycles of the advertised 160 MHz CPU clock; instruction-accurate modes expose deterministic virtual
  time rather than predicted hardware time.
- **PLIC**: Platform-Level Interrupt Controller at base address `0x0c000000` supporting priority thresholds and interrupt claims.

---

## 6. Full-System Linux Emulation

SimRV boots full Linux distributions with OpenSBI and dynamic device-tree generation.

```bash
# Launch Linux with OpenSBI and root filesystem image
simrv --kernel linux-images/Image \
      --disk linux-images/root.img \
      --smp 2
```

### Full-System CLI Options

- `--kernel <Image>`: Linux kernel image.
- `--disk <root.img>`: Ext4 root filesystem image (attached as `/dev/vda` via VirtIO Block).
- `--smp <2..16>`: Number of active SMP harts (default is 1 for single-hart execution).
- `--dtb <virt.dtb>`: Optional custom Device Tree Blob (SimRV automatically synthesizes a device tree if omitted).
- `--dram-size <SIZE>`, `--ram-size <SIZE>`: Guest DRAM size with an optional
  scaled suffix such as `128M` or `2G` (official release default: 256 MiB).

---

## 7. CPU Model Presets & Microarchitecture Tuning

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

For more details on registering new hardware RTL targets, refer to [RTL Parity Verification](../hardware/rtl_parity.md).

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
  *A:* The `--cli` flag enforces non-interactive headless operation. To see the graphical TUI workbench, run `simrv` without `--cli`.

- **Q: Where are instruction traces written when passing `--trace`?**
  *A:* Traces are saved in the `trace/` directory relative to your working directory by default.
  Use `--trace-dir DIR` to select another root. The `trace.txt` file presents labeled, aligned
  cycle, instruction, register, and CSR fields; periodic PC samples remain in `tracepc.txt`.

- **Q: How can I connect GDB to debug a running binary?**
  *A:* Launch SimRV with `--gdb --cli -m program.elf`, then in another terminal run:
  `riscv64-unknown-elf-gdb program.elf -ex "target remote :1234"`

### Detailed Architecture & Extension Guides

- [System Architecture](../architecture/overview.md)
- [Bare-Metal Guide](baremetal.md)
- [CPU Model Configuration Reference](../hardware/models.md)
- [RTL Parity Verification Guide](../hardware/rtl_parity.md)
- [TileLink-C Profile & Cache Coherence](../architecture/tilelink.md)
- [RISC-V Compliance Scope](../architecture/compliance.md)
