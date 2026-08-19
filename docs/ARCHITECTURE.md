# SimRV Architecture

**Version:** 2.0.0 · **Targets:** RV32GC, RV64GC, qualified Vector subset, and Bitmanip

## Scope

This document describes the current high-level structure of SimRV. The simulator
is a cycle-oriented RISC-V functional simulator written in C++23, supporting bare-metal
applications, real-time operating systems (e.g., μT-Kernel 3.0), and full Linux OS execution.

---

## Core Runtime Components

### Top-Level Orchestration

- **`Machine`** (`include/simrv/core/Machine.hpp`): Root simulator object. Owns
  and wires the CPU, memory subsystem, and all MMIO devices. Drives the pipeline
  cycle loop, handles image loading, device tickling, tohost termination checks,
  and simulation shutdown/reboot.

- **`CPU`** (`include/simrv/core/Cpu.hpp`): Architectural state and all pipeline
  stage methods. Owns `ArchState` (GPRs, FPRs, CSRs, privilege level, PC), the
  soft TLB, instruction/data caches, decode cache, pipeline context, SBI handler,
  and execution metrics.

### Peripheral Devices

All devices are MMIO-backed and attached through `Machine`:

| Component | Header | Description |
|:---|:---|:---|
| `Console` | `device/Console.hpp` | VirtIO-style console (MMIO ring buffer) |
| `Disk` | `device/Disk.hpp` | VirtIO block device for root filesystem |
| `Uart` | `device/Uart.hpp` | 16550-compatible UART serial port |
| `Rtc` | `device/Rtc.hpp` | Real-time clock MMIO |
| `PowerMmio` | `device/Power.hpp` | Power/reset control MMIO |

Interrupt routing is handled by embedded `PlicMmio` and `ClintMmio` inside `CPU`,
plus `InterruptController` helpers for PLIC line bookkeeping.

### Memory Subsystem

- **`MemorySubsystem`** (`memory/MemorySubsystem.hpp`): Central DRAM access and
  MMIO dispatch hub. Configurable size via `SIMRV_DRAM_SIZE_MB` (default: 256 MB).
- **`Mmu`** / **`TileLinkBus`**: SV32/SV39 page-table walk helpers and the
  TileLink-style interconnect fabric.
- **`ICache`** / **`DCache`**: Instruction and data cache models, configurable at
  build time.

---

## Execution Helpers (Thin Wrappers)

| Class | Source | Role |
|:---|:---|:---|
| `CsrFile` | `core/CsrFile.cpp` | CSR read/write, access control, mstatus side effects |
| `Tlb` | `core/Tlb.cpp` | Hardware TLB state and flush behavior |
| `Sbi` | `core/Sbi.cpp` | Optional direct supervisor-ECALL SBI environment when guest M-mode firmware is absent |
| `ExecuteUnit` | `execute/ExecuteUnit.cpp` | Integer ALU, branch, AMO, CSR value helpers |
| `ExecuteUnitInt` | `execute/ExecuteUnitInt.cpp` | Integer arithmetic execute |
| `ExecuteUnitFloat` | `execute/ExecuteUnitFloat.cpp` | Floating-point execute |
| `DecodeCache` | `core/DecodeCache.hpp` | Fast-path decode result caching |

These helpers are intentionally thin — they preserve behavioral parity while
making class boundaries explicit.

---

## Pipeline Stages

The CPU pipeline is a 6-stage in-order functional model:

| # | Stage | Method | Description |
|:---|:---|:---|:---|
| 1 | **IF** | `run_fetch_stage` | Address translation (SV32/SV39), I-cache lookup, decompression (RVC) |
| 2 | **ID** | `run_decode_stage` | Instruction field decode (opcode, rd/rs, funct, imm), operand capture |
| 3 | **EX** | `run_execute_stage` | ALU, branch, CSR, FP operations |
| 4 | **MEM** | `run_memory_stage` | Load/store/AMO memory access, D-cache lookup |
| 5 | **WB** | `run_writeback_stage` | Integer and FP register file writeback |
| 6 | **Commit** | `run_commit_stage` | Control-flow updates, trap/interrupt handling, tohost checks |

**Execution paths:**
- **Standard path**: Full pipeline via `CPU::run_cycle()`.
- **Baremetal path**: Optimized hot path (`run_cycle_baremetal`) bypassing TUI
  overhead, used when TUI is inactive.
- **Coroutine path**: C++20 coroutine-based `PipelineTask` for persistent
  zero-allocation pipeline simulation (`PipelineSim`).
- **Cached op path**: `execute_cached_op_fast` for decode-cache hits in IA mode.

---

## Debug and Co-Simulation

| Component | Source | Description |
|:---|:---|:---|
| `GdbStub` | `debug/GdbStub.cpp` | GDB RSP server (TCP, port 1234 default) |
| `SpikeLockstep` | `debug/SpikeLockstep.cpp` | Instruction-level lockstep co-simulation vs. Spike |
| `SymbolTable` | `debug/SymbolTable.cpp` | ELF symbol lookup for PC→symbol name resolution |
| `Tracer` | `core/Tracer.cpp` | Instruction mix, branch prediction, and cycle tracing |

---

## TUI (Terminal User Interface)

The TUI provides an ANSI-based split-screen monitor during simulation.

**Component files** (`src/tui/`, `include/simrv/tui/`):

| File | Role |
|:---|:---|
| `Tui.cpp` | Top-level TUI orchestrator, layout management, keybindings, rendering loop |
| `LeftPane.cpp` | Left panel container managing registers, stack, pipeline state, stats, and explainer (EXPLAIN page) |
| `LeftPanePipeline.cpp` | Cycle-accurate & functional pipeline stages rendering |
| `LeftPaneRegs.cpp` | GPR/FPR/Vector register file rendering |
| `RightPane.cpp` | Right panel: Virtual terminal (VT) output passthrough & console |
| `TuiModal.cpp` | Interactive modal windows (Speed Hz config, Breakpoints, Help) |
| `StatusBar.cpp` | Bottom status bar: cycle/IPS counters, badges, mode indicators |
| `TuiTheme.cpp` | Theme system: Adaptive (ANSI default), Sakura, HighContrast palettes |
| `VirtualTerminal.hpp` | Full VT100/VT220 escape sequence parser and screen buffer |

**Layouts:** Split (default), FullConsole, FullRegister — cycled with `L`.

**Register pages:** GPR → FPR → PIPELINE, cycled with `R`; `E` toggles EXPLAIN directly.

**Themes** (toggled with `H`/`T`):
- `Adaptive` (default): Standard ANSI SGR codes, adapts to terminal theme.
- `Sakura`: 256-color pastel palette.
- `HighContrast`: Bold ANSI primary colors.

---

## ISA Support

| Extension | Status |
|:---|:---|
| RV32I / RV64I | ✅ Full |
| M (Multiply/Divide) | ✅ Full |
| A (Atomics) | ✅ Full |
| F (Single FP) | ✅ Full |
| D (Double FP) | ✅ Full |
| C (Compressed) | ✅ Full |
| V (Vector) | ✅ Full |
| B (Bitmanip) | ✅ Full |
| Zicsr / Zifencei | ✅ Full |
| SV32 / SV39 MMU | ✅ Full |
| M/S/U privilege | ✅ Full |

XLEN is a **compile-time** selection via `SIMRV_XLEN` (32 or 64). There is no runtime XLEN switching — each build targets a single XLEN.

---

## Build and Validation

**CMake presets** (`CMakePresets.json`):

| Preset | Target | Build Dir |
|:---|:---|:---|
| `rv32-release` | RV32GC Release | `build/rv32-release/` |
| `rv64-release` | RV64GC Release | `build/rv64-release/` |
| `rv32-debug` | RV32GC Debug | `build/rv32-debug/` |
| `rv64-debug` | RV64GC Debug | `build/rv64-debug/` |

**Validation gates** (CMake targets):

| Target | Covers |
|:---|:---|
| `isa-gate` | Full RV32/64GC ISA test suite |
| `integration-gate` | All gate-labeled tests (regression, app, linux, ISA) |
| `lockstep-gate` | Spike instruction-level lockstep (requires `spike` in PATH) |

---

## Repository Layout

```
include/simrv/         Public/internal headers
  core/                Machine, CPU, CSR, registers, tracer, build info
  cache/               ICache, DCache
  memory/              MMU, TileLink, memory subsystem
  device/              Console, Disk, UART, RTC, Power, VirtIO
  pipeline/            PipelineContext, PipelineTask, PipelineSim, Decoder
  execute/             ExecuteUnit, ExecuteUnitInt, ExecuteUnitFloat
  debug/               GdbStub, SpikeLockstep, SymbolTable
  tui/                 Tui, LeftPane, LeftPanePipeline, LeftPaneRegs, LeftPaneStack,
                       LeftPaneStats, LeftPaneExplain, RightPane, TuiModal,
                       StatusBar, TuiTheme, TuiKey, VirtualTerminal
  util/                InstructionExplainer
  xlen/                XLEN traits and type aliases

src/                   Implementation units (mirrors include layout)
scripts/               Build, ISA test, and benchmark helpers
docs/                  Architecture, extension, build, and education guides
```

---

## Current Development Status

- **Stable:** RV32GC and RV64GC full feature sets (including Vector and Bitmanip extensions), with support for Linux boot on both targets.
- **Compile-time XLEN:** Select the target with the appropriate CMake preset
  (`rv32-release` / `rv64-release`). XLEN cannot be changed at runtime.
- **Planned:** Further ISA extension coverage (e.g., Cryptographic extensions).

---

## Codebase Statistics and Complexity

SimRV codebase metrics are monitored using static analysis tools (`lizard`). The following statistics represent the current snapshot of the C++23 source code (extracted via `scripts/code_metrics.py`):

### Global Metrics
- **Total C++ Source & Header Files:** 168
- **Total Non-Comment Code Lines (NLOC):** 35,429
- **Average Function NLOC:** 26.56
- **Average Cyclomatic Complexity (CCN):** 7.14
- **Function Count:** 1,334

### Subsystem Breakdown
| Subsystem / Directory | Description | Files | NLOC | Functions | Avg CCN | Avg NLOC |
|:---|:---|:---|:---|:---|:---|:---|
| `src/tui` / `include/simrv/tui` | Modular TUI panes, modals, Sixel rendering & Virtual Terminal | 50 | 10,207 | 350 | 7.91 | 29.2 |
| `src/execute` / `include/simrv/execute` | Vector, floating-point, integer execution units & ISA headers | 21 | 6,355 | 213 | 8.65 | 29.8 |
| `src/core` / `include/simrv/core` | Architectural state, CPU, SBI, CSRs & Machine orchestration | 30 | 5,825 | 239 | 6.35 | 24.4 |
| `src/pipeline` / `include/simrv/pipeline` | Instruction fetch/decode stages, decoder dispatch & pipeline logic | 11 | 4,049 | 166 | 8.37 | 24.4 |
| `src/util` / `include/simrv/util` | Instruction explainer routines, CLI parser & system helpers | 5 | 3,391 | 86 | 7.49 | 39.4 |
| `src/device` / `include/simrv/device` | VirtIO Console, Disk, Framebuffer, Audio, UART, RTC & Power models | 21 | 2,080 | 118 | 4.12 | 17.6 |
| `src/memory` / `include/simrv/memory` | Sv32/Sv39/Sv48 MMU, TileLink bus interconnect & memory hierarchy | 18 | 1,584 | 79 | 4.97 | 20.1 |
| `src/debug` / `include/simrv/debug` | GDB stub, Lockstep comparison, SymbolTable, BreakpointManager | 8 | 1,508 | 73 | 5.38 | 20.7 |
| Top-Level Entrypoint | Main simulation runner and CLI interface (`src/Main.cpp`) | 4 | 430 | 10 | 9.00 | 43.0 |

### Top Complexity Hotspots
The cyclomatic complexity (CCN) hot-spots in the simulator correspond to flat declarative dispatch tables:
1. `simrv::pipeline::decode_ext_v_range2` (in `Decoder.cpp`) - **CCN: 124**, NLOC: 161 (Vector range 2 opcode decoder)
2. `simrv::execute::ExecuteUnit::execute_vector` (in `ExecuteUnitVector.cpp`) - **CCN: 118**, NLOC: 159 (Vector operation execution dispatcher)
3. `simrv::util::get_operand_hazard_info` (in `InstructionExplainer.cpp`) - **CCN: 108**, NLOC: 145 (Instruction operand dependency analysis)
4. `simrv::pipeline::decode_ext_v_range1` (in `Decoder.cpp`) - **CCN: 105**, NLOC: 140 (Vector range 1 opcode decoder)
5. `simrv::tui::VirtualTerminal::execute_csi_command` (in `VirtualTerminal.hpp`) - **CCN: 92**, NLOC: 146 (TUI virtual terminal CSI escape sequence parser)

For academic presentation details, complexity tier distributions, and LaTeX table export, see [`docs/PAPER_COMPANION.md`](PAPER_COMPANION.md).
