# Migrating from SimRV 2.x to 3.0

SimRV 3.0 replaces the execution architecture and intentionally removes 2.x compatibility-only
interfaces. Guest-visible RISC-V behavior remains the goal, but host configuration, automation,
and SDK consumers must update as described below.

## Command line

- Select instruction-accurate execution with `--ia` and cycle-accurate execution with `--ca`.
- Select cycle structure with `--pipeline 3stage` or `--pipeline 5stage`.
- Removed pipeline presets and aliases are rejected; no compatibility aliases are provided.
- Rollback, snapshots used for rollback, and reverse-stepping commands/APIs are removed.
- Use `--config` to select a platform preset (`virt-pcie`, `virt-mmio`, `rvcomp`) or load a platform
  configuration file. The deprecated `--platform`, `--soc`, `--cpu-config`, and `--model-file` flags
  have been removed. Use `--trace-pc-range START-END` for filters on `--arch-trace`. The legacy
  `--trace-pc-period` and `-I, --dump-init` bringup options have been removed.
- `--trace-range BEGIN END` bounds text snapshots by the inclusive CLINT `mtime` counter.

## Configuration and platform

- `MachineConfig` is the single configuration value. Hart count is
  `MachineConfig::execution.num_harts`.
- UI worker threading is `execution.ui_worker_threaded`; select hart scheduling through
  `execution.smp_mode` (`SingleThreaded` or `MultiThreaded`).
- Platform profiles are `Pcie` and `Mmio`. The mixed `Hybrid` profile was removed.
- `load_cpu_config` now accepts `CpuModelConfig`; read pipeline timing through its `pipeline` member.
- Pipeline `mul_latency`, `div_latency`, `fp_alu_latency`, and `fp_div_latency` now mean additional
  stall cycles after issue. When updating an older model file, subtract one from each former
  issue-inclusive value; zero means the operation adds no stall cycles. CFU plugin `latency_cycles`
  and `[cfu].default_latency` remain total cycles including issue.

## SMP timing

Cycle-accurate SMP uses deterministic global-cycle scheduling and deterministic hart ordering.
Multi-threaded SMP (`mode = multi-threaded` in SoC profiles) executes each global cycle on hart worker
threads, then synchronizes before advancing the shared fabric and timers. Requests are ordered by
logical cycle, hart ID, and per-hart issue order. The synchronization quantum groups global cycles into
host-side runner turns; ordering remains synchronized at every cycle boundary. Pause, step, reboot,
and shutdown wait for workers to reach a boundary.

The CLI, typed configuration, and TUI all support 1 through 16 harts. Changing the hart count in
the TUI stages the configuration and takes effect after reboot.

## SDK and protocol boundary

`SimRV::runtime` remains the supported CMake target. Cache, pipeline, device, and TileLink classes
are implementation details and are not wire-interoperability APIs. Bus responses now distinguish
TileLink `denied` and `corrupt`; coherence uses `MesiState`, while TileLink Grow, Cap, and Report
parameters remain transport types.

`TuiExecutionSnapshot` is now a stable per-hart snapshot containing cycle, instruction, timer,
cache, and cycle-accurate statistics. `tui_execution_snapshot(hart)` selects a hart and defaults to
hart 0 when no argument is provided.

The local socket TUI attachment workflow and its installed `simrv/net` client, server, and protocol
headers have been removed. `Machine::set_persistent_control()`, `Machine::persistent_control()`, and
`HostExitCode::IpcError` were specific to that workflow and are removed as well. Use the in-process
TUI for interactive inspection or GDB RSP for remote debugger control.

Logging and persisted schemas do not receive compatibility shims. Regenerate configuration rather
than translating removed fields at runtime.

Combined CPU/SoC model files use `[soc]` and `[device.uart]`; the old `[platform]` and `[uart]`
section spellings are rejected with migration guidance.

## C++ address-domain API

First-party C++ interfaces distinguish virtual PCs and effective addresses (`simrv::xlen::VirtAddr`)
from guest physical addresses used by RAM, MMIO, DMA, SoC regions, and PCIe BARs
(`simrv::xlen::PhysAddr`). Constructors are explicit; callers must name the domain when importing a
scalar:

```cpp
const simrv::xlen::VirtAddr reset_pc{raw_elf_entry};
const simrv::xlen::PhysAddr uart_base{platform_uart_base};
```

Use `.raw()` only when crossing an architectural, serialized, or C ABI boundary whose representation
is scalar. Validate the external width before narrowing to the configured guest `Word`. Sizes and
arithmetic displacements remain scalar values; MMIO register offsets use
`simrv::memory::MmioOffset` and are distinct from physical addresses. Construct them explicitly at a
device-register boundary, and use `.raw()` when indexing host storage or crossing a scalar ABI.
PCI bus, device, and function numbers and PCI configuration offsets also have distinct wrappers;
construct them explicitly when decoding ECAM addresses and extract `.raw()` when packing PCI
configuration bytes. PCI vendor, device, subsystem, revision, and class-code values are distinct
too; construct them explicitly at endpoint definitions and extract `.raw()` only when serializing
the PCI configuration space.
Hart identifiers use `simrv::xlen::HartId`; construct them explicitly when importing hart numbers
from configuration, logs, or a plugin boundary, and use `.raw()` when formatting or serializing
them. SMP cluster membership, TUI backend requests, trace contexts and filters, telemetry events,
and per-hart statistics carry `HartId`; an absent hart in machine events or bus trace records is
represented with `std::nullopt`. Dynamic CSR indices use `simrv::xlen::CsrNumber`; use the `Csr` enum
overload when the CSR is known at compile time, and construct `CsrNumber` explicitly for decoded or
externally supplied indices. Neither identifier converts implicitly to or from an integer, and the
C plugin ABI keeps its scalar representation through the existing adapters.
`TLB::lookup` takes a virtual
address and `TLB::insert` receives a virtual tag and physical result. Device constructors and
`MmioDevice::base_address()` use `PhysAddr`; address-based TUI backend requests carry a virtual or
physical alternative selected by their command. The C plugin structures and callbacks remain
unchanged, with explicit conversions in the C++ adapter.

VirtIO descriptor fields and queue-address registers retain their specified 64-bit wire
representation. `IVirtioTransport::dma_read_bytes` and `dma_write_bytes` take `PhysAddr`; transport
code that consumes a wire address must use the named `dma_*_wire_address` adapters, which reject
values the configured `Word` cannot represent before conversion. Do not change the packed descriptor
layout to use an internal C++ address type.

Breakpoint and watchpoint APIs use virtual addresses. MMIO routing, DMA, cache tags, and reservation
tracking use physical addresses. An address read from an ELF or device-tree file is not assigned a
domain by its integer type: use the format's semantics, then construct the appropriate type at the
parser boundary. A guest physical value wider than the simulator's configured XLEN must be rejected
before constructing `PhysAddr`. Spike lockstep and RTL retirement records carry architectural PCs as
`VirtAddr`; their parsers reject PCs that cannot be represented by the build's `Word` type.
