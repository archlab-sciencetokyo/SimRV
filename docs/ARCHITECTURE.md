# SimRV Architecture

SimRV is a C++23 RISC-V simulator for RV32 and RV64 builds. It runs bare-metal
images and supported OS configurations through either a functional instruction
engine (IA) or a cycle-stepped in-order engine (CA). ISA qualification claims and
known gaps are maintained in [RISC-V compliance scope](RISCV_COMPLIANCE.md).

## Runtime structure

`Machine` owns the CPUs, memory subsystem, devices, loader, and top-level
scheduling. Each `CPU` owns architectural state, translation/cache state,
decode cache, pipeline context, branch predictor, and CA state. Devices are
MMIO endpoints connected through the memory subsystem and TileLink-style bus.

| Area | Primary components | Responsibility |
| --- | --- | --- |
| Core | `Machine`, `CPU`, `ArchState` | Scheduling, architectural state, traps, counters |
| Execution | decoder and execute units | Decode, integer/FP/vector execution, retirement |
| Memory | MMU, TLB, caches, TileLink | Translation, coherent access, timed CA transactions |
| Platform | UART, timers, interrupts, VirtIO | Guest-visible devices and interrupt delivery |
| Debug | tracer, GDB stub, Spike lockstep | Inspection and optional co-simulation |
| Presentation | TUI and virtual terminal | Interactive state inspection and guest console |

## Execution policies

`RuntimeProfile` resolves the command-line execution choice and interaction
mode into one policy:

| Interface | IA | CA |
| --- | --- | --- |
| CLI | `InstructionFast` | `CycleFast` |
| TUI | `InstructionObservable` | `CycleObservable` |

IA executes the architectural stages without a timed pipeline. Its fast policy
uses decode caching and batching where debugging, tracing, lockstep, and GDB do
not require per-instruction observation. TUI IA retains the context required for
presentation but does not model CA timing.

CA has one persistent transition kernel per hart. It supports 5-stage
Fetch/Decode/Execute/Memory/Writeback and 3-stage Fetch/Decode+Execute/
Memory+Writeback organizations. Slots carry instruction context, latency,
exceptions, forwarding state, and branch-prediction state. Architectural effects
become visible at retirement; traps and interrupts are taken only at retirement
boundaries.

`CycleFast` records counters and event bits without snapshot allocation.
`CycleObservable` uses the same kernel and adds a bounded history for the TUI.
The platform tests require identical guest state, memory effects, retirement
counts, and cycle counts for both policies.

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
