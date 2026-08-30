# SimRV 2.1 migration

This release intentionally removes compatibility-only interfaces. Guest-visible RISC-V behavior is
unchanged; host configuration and SDK users must update the following names.

## Command line

- Select execution with `--mode fast`, `--mode detailed`, or `--mode cycle-accurate`.
- Select cycle structure with `--pipeline 3stage` or `--pipeline 5stage`.
- `--ca`, `-C`, `--ia`, and their compatibility-only conflict handling were removed.

## Configuration and platform

- `MachineConfig` is the single configuration value. Hart count is
  `MachineConfig::execution.num_harts`.
- UI worker threading is `execution.ui_worker_threaded`; parallel hart scheduling is
  `execution.smp_multithreaded`.
- Platform profiles are `Pcie` and `Mmio`. The mixed `Hybrid` profile was removed.

## SDK and protocol boundary

`SimRV::runtime` remains the supported CMake target. Cache, pipeline, device, and TileLink classes
are implementation details and are not wire-interoperability APIs. Bus responses now distinguish
TileLink `denied` and `corrupt`; coherence uses `MesiState`, while TileLink Grow, Cap, and Report
parameters remain transport types.

Logging and persisted schemas do not receive compatibility shims. Consumers should regenerate
configuration rather than translating removed fields at runtime.
