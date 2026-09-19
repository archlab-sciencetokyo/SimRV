# SimRV 2.0 User Guide

SimRV is an explainable, dual-width (RV32 / RV64) RISC-V research simulator with an interactive terminal workbench (TUI), cycle-accurate in-order pipeline modeling, cache hierarchy inspection, and full-system Linux OS emulation.

---

## 1. Architecture & Features Overview

SimRV simulates standard 32-bit and 64-bit RISC-V architectures:

- **ISA Support**: RV32GCBV / RV64GCBV (Base Integer `I`/`E`, Standard Multiply/Divide `M`, Atomic `A`, Single/Double Floating Point `F`/`D`, Compressed `C`, Bit Manipulation `B`, and Vector 1.0 `V`).
- **Privilege & System Architecture**: Machine (M), Supervisor (S), and User (U) privilege modes with Physical Memory Protection (PMP), Sv32 / Sv39 MMU page translation, CLINT timer and software interrupts, and PLIC interrupt routing.
- **Microarchitectural Modeling**: Cycle-accurate 5-stage in-order pipeline execution kernel modeling instruction latency, data hazard stalls, register forwarding, branch prediction (Bimodal, 2-level adaptive, RAS, BTB), and multi-way L1 instruction and data cache hierarchies.
- **Interactive TUI Workbench**: Educational split-screen monitor displaying live register files, pipeline stages, cache lines, hazard graphs, disassembly explainers, and an interactive Linux PTY console.
- **Hardware Peripherals**: 16550A UART serial console, VirtIO block storage, Real-Time Clock (RTC), CLINT, and PLIC.

---

## 2. Installation & Quickstart

### Prerequisites

- Modern C++23 compiler: **Clang 20+** or **GCC 14+**
- Build tools: **CMake 3.20+**, **Ninja**
- Python: **Python 3.10+** (for utility tools and test suites)

### Building from Source

SimRV requires configuring and building with standard CMake presets:

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
# Install SimRV binary, headers, and documentation
sudo cmake --install build/rv64-release --prefix /usr/local
```

Installed files include:

- Executable: `/usr/local/bin/SimRV`
- C++ Headers: `/usr/local/include/simrv/`
- License Notices: `/usr/local/share/licenses/SimRV/`

---

## 3. Command-Line Interface (CLI)

By default, launching `SimRV` launches the interactive TUI workbench. For non-interactive batch scripts or automated testing, provide `--cli`.

### Common Invocations

```bash
# Run a bare-metal binary in interactive TUI mode (Default)
./build/rv64-release/SimRV -b -m img/hello.bin

# Run in fast headless CLI mode
./build/rv64-release/SimRV -b -m img/hello.bin --cli

# Run in cycle-accurate (CA) mode with a step limit
./build/rv64-release/SimRV -b -m img/hello.bin --ca --cli -s 1000000

# Boot Linux OS with root filesystem and device tree
./build/rv64-release/SimRV --os \
  -m linux-images/rv64/fw_payload.bin \
  -D linux-images/rv64/root.bin \
  -f linux-images/rv64/devicetree.dtb
```

### Key CLI Options

| Option | Description |
| :--- | :--- |
| `-c, --cli` | Force headless command-line execution (disables TUI). |
| `-u, --tui` | Force interactive TUI workbench (default). |
| `-m, --image <file>` | Load binary or ELF executable into guest memory. |
| `-b, --baremetal` | Run in freestanding bare-metal application mode. |
| `--os` | Run in full-system OS mode (Linux / OpenSBI). |
| `-D, --disk <file>` | Attach virtual block storage disk image. |
| `-f, --fdt <file>` | Provide Device Tree Blob (DTB) path. |
| `--ca, -C` | Select cycle-accurate pipeline simulation kernel. |
| `--ia` | Select fast instruction-accurate simulation mode. |
| `-s, -e, --steps <N>` | Evaluate instruction limit before stopping simulation. |
| `-H, --tohost-addr <addr>` | Specify physical address of `tohost` communication symbol. |
| `--misa <string>` | Override MISA extension string (e.g. `rv64gcbv`). |
| `--vlen <bits>` | Specify Vector register bit width (e.g. `128`, `256`, `512`). |
| `--trace` | Write architectural instruction trace to `trace/trace.txt`. |
| `--tracepc` | Write PC stream trace to `trace/tracepc.txt`. |
| `--gdb` | Start GDB Remote Serial Protocol (RSP) debug server. |
| `--gdb-port <port>` | Set GDB RSP TCP listener port (default: 1234). |
| `--lockstep` | Enable Spike lockstep co-simulation verification. |
| `-v, --version` | Display version and build information. |
| `-h, --help` | Show full command-line help message. |

---

## 4. Interactive TUI Workbench

The SimRV TUI provides an educational visual inspection environment for architecture researchers and students.

```text
┌─ SimRV 2.0 ─────────────────────────────────────────── [RUNNING] ─┐
│ [Regs] [Cache] [TLB] [Pipeline] [Hazards] [Explainer] │ Guest Terminal PTY │
│                                                      │                    │
│ PC: 0x80000000   ra: 0x00000000   sp: 0x80010000     │ Linux version 6.6  │
│ IF: [0x80000020] addi a0, a0, 1                      │ buildroot login:   │
│ ID: [0x8000001c] lw   a1, 0(sp)                      │                    │
│ EX: [0x80000018] mul  a2, a1, a0                     │                    │
│ MEM:[0x80000014] sw   s0, 4(sp)                      │                    │
│ WB: [0x80000010] addi sp, sp, -16                    │                    │
├──────────────────────────────────────────────────────┴────────────────────┤
│ [s:Step] [c:Run] [b:Back] [o:Load] [,:Config] [Alt-M:MISA] [?:Help]
└───────────────────────────────────────────────────────────────────────────┘
```

### Key Shortcuts

| Hotkey | Action |
| :--- | :--- |
| `[s]` / `[Space]` | Single instruction step |
| `[c]` / `[Ctrl-P]` | Run / Pause simulation loop |
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

## 5. Bare-Metal & Embedded Simulation

SimRV executes freestanding bare-metal programs compiled with standard RISC-V GCC or Clang toolchains.

```bash
# Run baremetal binary with standard tohost exit reporting
./build/rv64-release/SimRV -b -m img/hello.bin -H 0x80001000 --cli
```

For complete linker scripts, startup assembly, and MMIO peripheral maps, refer to the [Bare-Metal Guide](baremetal.md).

---

## 6. Full-System Linux Emulation

SimRV boots full Linux distributions with OpenSBI and device-tree hardware descriptions:

```bash
# Launch Linux with root filesystem and device tree
./build/rv64-release/SimRV --os \
  -m linux-images/rv64/fw_payload.bin \
  -D linux-images/rv64/root.bin \
  -f linux-images/rv64/devicetree.dtb
```

For image building instructions and prebuilt configurations, refer to the [Linux Build Guide](linux.md).

---

## 7. Co-Simulation & Debugging

### Spike Lockstep Co-Simulation

Validate execution correctness instruction-by-instruction against the official Spike reference simulator:

```bash
./build/rv64-release/SimRV -b -m img/hello.bin --lockstep --cli
```

### GDB Remote Serial Protocol (RSP)

Connect standard GDB to debug guest execution:

```bash
# Terminal 1: Launch SimRV with GDB RSP listener
./build/rv64-release/SimRV -b -m program.elf --gdb --gdb-port 1234 --cli

# Terminal 2: Connect GDB
riscv64-unknown-elf-gdb program.elf -ex "target remote :1234"
```

---

## 8. Benchmarking & Reproducibility

Evaluate local simulation performance and verify clean-checkout reproducibility:

```bash
# Run release benchmark suite
python3 scripts/benchmark.py --binary build/rv64-release/SimRV

# Run full reproducibility validation workflow
python3 scripts/reproduce.py --mode quick
```

---

## 9. Detailed Reference Guides

- [System Architecture](../architecture/overview.md)
- [Bare-Metal Programming Guide](baremetal.md)
- [Terminal UI Architecture](tui.md)
- [Linux Image Building](linux.md)
- [Student Educational Reference](classroom.md)
- [RISC-V Compliance Scope](../architecture/compliance.md)
- [Custom ISA Extensions](../hardware/extensions.md)
- [Release Qualification & Contract](../evaluation/release.md)
