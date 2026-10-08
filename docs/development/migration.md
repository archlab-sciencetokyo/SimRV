# Migrating from SimRV 2.x to 3.0

SimRV 3.0 replaces the execution architecture and intentionally removes 2.x compatibility-only
interfaces. Guest-visible RISC-V behavior remains the goal, but host configuration, automation,
and SDK consumers must update as described below.

## Command line

- Select instruction-accurate execution with `--ia` and cycle-accurate execution with `--ca`.
- Select cycle structure with `--pipeline 3stage` or `--pipeline 5stage`.
- Removed pipeline presets and aliases are rejected; no compatibility aliases are provided.
- Rollback, snapshots used for rollback, and reverse-stepping commands/APIs are removed.
- Use `--soc` instead of the removed `--platform` alias. Use `--trace-pc-range START-END` (or
  `--trace-pc START-END`) for architectural PC filters and `--trace-pc-period N` for PC sampling.

## Configuration and platform

- `MachineConfig` is the single configuration value. Hart count is
  `MachineConfig::execution.num_harts`.
- UI worker threading is `execution.ui_worker_threaded`; parallel hart scheduling is
  `execution.smp_multithreaded`.
- Platform profiles are `Pcie` and `Mmio`. The mixed `Hybrid` profile was removed.
- `load_cpu_config` now accepts `CpuModelConfig`; read pipeline timing through its `pipeline` member.
- Pipeline `mul_latency`, `div_latency`, `fp_alu_latency`, and `fp_div_latency` now mean additional
  stall cycles after issue. When updating an older model file, subtract one from each former
  issue-inclusive value; zero means the operation adds no stall cycles. CFU plugin `latency_cycles`
  and `[cfu].default_latency` remain total cycles including issue.

## SMP timing

Non-multithreaded cycle-accurate SMP uses deterministic quantum scheduling and deterministic hart
ordering. `--smp-multithreaded` opts into best-effort parallel timing, so precise inter-hart timing
is intentionally nondeterministic. Worker quiescence is deterministic for pause, step, reboot, and
shutdown: those operations wait until every worker has reached the requested boundary.

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

Logging and persisted schemas do not receive compatibility shims. Regenerate configuration rather
than translating removed fields at runtime.

Combined CPU/SoC model files use `[soc]` and `[device.uart]`; the old `[platform]` and `[uart]`
section spellings are rejected with migration guidance.
