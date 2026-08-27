# SimRV Architecture

SimRV is a C++23 RISC-V simulator for RV32 and RV64 targets. It runs baremetal
images and full Linux OS configurations through fast functional instruction execution,
detailed state simulation, or cycle-accurate microarchitectural modeling. ISA qualification
claims and known gaps are maintained in [RISC-V compliance scope](RISCV_COMPLIANCE.md).

## Runtime Structure

`Machine` orchestrates the virtual platform: CPU harts, memory subsystem, TileLink-C
interconnect, PCIe root complex, platform interrupt controllers, and device endpoints.
Each `CPU` hart maintains its architectural register state (`ArchState`), translation/TLB
structures, L1 I/D-caches, branch predictor, and execution pipeline model.

| Area | Primary Components | Architectural Responsibility |
| --- | --- | --- |
| **Core** | `Machine`, `CPU`, `ArchState` | Hart lifecycle, architectural registers, CSRs, traps, performance counters |
| **Execution** | `Decoder`, `ExecuteUnit`, `PipelineSim` | Decode caching, integer/FP/vector execution, pipeline hazards, retirement |
| **Memory & Coherence** | `Mmu`, `DCache`, `ICache`, `TileLinkBus` | Address translation, L1 caches, TileLink-C directory coherence with MESI protocol |
| **Platform Devices** | `PcieRootComplex`, `VirtioDevice`, `Uart`, `AIA`, `Aclint` | PCIe ECAM/BARs, VirtIO MMIO/PCI, 16550A UART, CLINT/PLIC, ACLINT, and AIA (APLIC/IMSIC) |
| **Debug & Tracing** | `Tracer`, `GdbServer`, `SpikeLockstep` | Structured architectural traces, GDB RSP remote debugging, Spike co-simulation |
| **Presentation** | `Tui`, `TuiFrameRenderer`, `VirtualTerminal` | Multi-hart state visualizer, terminal console PTY, educational glossary & inspector |

## Execution Policies & Microarchitectures

Execution is configured via `--mode <name>` and resolved into runtime profiles:

| Mode Flag | Policy Class | Description |
| --- | --- | --- |
| `--mode fast` | `InstructionFast` | High-throughput functional execution with decode caching and quantum batching |
| `--mode detailed` | `InstructionObservable` | Instruction-by-instruction execution retaining telemetry for TUI inspection |
| `--mode cycle-accurate` | `CycleFast` / `CycleObservable` | Cycle-stepped 4-stage pipeline with branch predictor and memory latency modeling |
| `--mode three-stage` | `CycleObservable` | 3-stage Fetch / Decode+Execute / Memory+Writeback educational pipeline model |
| `--mode dual-issue` | `CycleObservable` | Dual-issue superscalar pipeline modeling parallel ALU dispatch and structural hazards |
| `--mode five-stage` | `CycleObservable` | Classic 5-stage Fetch / Decode / Execute / Memory / Writeback pipeline |

In cycle-accurate and pipeline modes, instruction slots traverse stages with explicit data forwarding,
structural hazard stalls, and branch-prediction redirection. Architectural effects commit strictly
at the retirement boundary.

## Multi-Hart SMP & TileLink-C Coherence

Multi-hart configurations (`--smp <N>`) simulate symmetric multiprocessing:
1. **Directory Coherence**: L1 caches participate in directory-based cache coherence implementing the MESI (Modified, Exclusive, Shared, Invalid) protocol over TileLink-C channels.
2. **Interrupt Routing**: Core local interrupts (software/timer) are managed via CLINT or ACLINT (MTIME/MSWI). External platform interrupts are handled by PLIC or AIA (APLIC wire interrupts and IMSIC message-signaled interrupts).
3. **Synchronization**: Atomic operations (LR/SC and AMOs) use a global reservation table and coherent bus transactions across harts.

## CA timing and ordering

On each global CA cycle SimRV:

1. Advances hart 0, then started secondary harts in ascending order.
2. Advances the shared TileLink/coherence fabric once.
3. Updates shared timer and interrupt-pending state.
4. Performs termination and presentation work outside the architectural step.

The CPU model controls pipeline, cache, and interconnect timing. Cache hit/miss
latencies and TileLink request/response latencies are positive whole-cycle
values. A transport latency of one is the default baseline: a request completes
on the next bus step and its response is visible in that completion cycle.
Squashed fetch and data requests are cancelled by hart/port source identity.

CA page walks are resumable transactions. PTE reads and accessed/dirty updates
share the bus ordering model; A/D updates use an atomic OR. This preserves a
single translation behavior while allowing CA to expose latency.

## Source layout

```text
include/simrv/  Interfaces and inline implementation
src/            Implementation units mirroring include/
tests/          Native semantic, pipeline, UI, platform, and CLI tests
scripts/        Build, benchmark, release, and reproduction helpers
docs/           User, contributor, compliance, and release documentation
```

Useful entry points are `src/Main.cpp`, `src/core/Machine.cpp`,
`src/core/Cpu.cpp`, and `src/pipeline/CycleKernel.cpp`. See
[TUI architecture](TUI.md), [bare-metal development](BAREMETAL_GUIDE.md),
[Linux image building](LINUX_IMAGE_BUILD.md), and
[extension development](EXTENSION_GUIDE.md) for focused workflows.

## Build and validation

Use the `rv32-release` or `rv64-release` CMake preset. The focused native gate
is `ctest --test-dir build/rv64-release --output-on-failure -L gate`; some gate
tests require external images or tools. Record CA performance with
`scripts/benchmark-ca.sh`. Compare reports only on equivalent hosts, compilers,
guest binaries, and instruction limits. On Linux laptops, set
`SIMRV_CA_BENCH_AFFINITY=<cpu>` to pin the benchmark and reduce scheduling and
thermal-frequency noise; allow a cooldown between long comparison runs.

## Local performance reference host

The current development measurements are recorded on an Intel Core Ultra X7 358H
(16 logical CPUs, 48 MiB L2, 18 MiB L3), with 31.1 GiB available memory, under
Linux 6.18.33.2-microsoft-standard-WSL2. Benchmarks pin to logical CPU 0. WSL2
does not expose the host governor or frequency telemetry here, so these results
are local regression evidence only—not portable performance claims. Use the
`rv64-native-release` preset for local tuning; release artifacts continue to
use the portable `rv64-release` x86-64-v3 configuration.
