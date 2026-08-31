# Migrating from SimRV 2.x to 3.0

SimRV 3.0 replaces the execution architecture and intentionally removes 2.x compatibility-only
interfaces. Guest-visible RISC-V behavior remains the goal, but host configuration, automation,
and SDK consumers must update as described below.

## Command line

- Select instruction-accurate execution with `--ia` and cycle-accurate execution with `--ca`.
- Select cycle structure with `--pipeline 3stage` or `--pipeline 5stage`.
- Removed pipeline presets and aliases are rejected; no compatibility aliases are provided.
- Rollback, snapshots used for rollback, and reverse-stepping commands/APIs are removed.

## Configuration and platform

- `MachineConfig` is the single configuration value. Hart count is
  `MachineConfig::execution.num_harts`.
- UI worker threading is `execution.ui_worker_threaded`; parallel hart scheduling is
  `execution.smp_multithreaded`.
- Platform profiles are `Pcie` and `Mmio`. The mixed `Hybrid` profile was removed.

## SMP timing

Non-multithreaded cycle-accurate SMP uses deterministic quantum scheduling and deterministic hart
ordering. `--smp-multithreaded` opts into best-effort parallel timing, so precise inter-hart timing
is intentionally nondeterministic. Worker quiescence is deterministic for pause, step, reboot, and
shutdown: those operations wait until every worker has reached the requested boundary.

## SDK and protocol boundary

`SimRV::runtime` remains the supported CMake target. Cache, pipeline, device, and TileLink classes
are implementation details and are not wire-interoperability APIs. Bus responses now distinguish
TileLink `denied` and `corrupt`; coherence uses `MesiState`, while TileLink Grow, Cap, and Report
parameters remain transport types.

`TuiExecutionSnapshot` is now a stable per-hart snapshot containing cycle, instruction, timer,
cache, and cycle-accurate statistics. `tui_execution_snapshot(hart)` selects a hart and defaults to
hart 0 when no argument is provided.

Logging and persisted schemas do not receive compatibility shims. Regenerate configuration rather
than translating removed fields at runtime.
