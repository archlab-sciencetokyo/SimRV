# SimRV 3.0.0 release-candidate handoff

Branch: `release/3.0.0-rc.13`
Target: `dev`

## Candidate scope for v3.0.0-rc.13

- [ ] Standard RISC-V Sdtrig extension hardware watchpoints (`check_triggers_mem`) for load and store data access matching.
- [ ] TUI hardware trigger management and status integration in BreakpointModal.
- [ ] RVV 1.0 single-page strided vector memory fast-paths (`vlse` / `vsse`).
- [ ] Host-vectorized SIMD reduction trees for vector integer operations (`vredsum`, `vredmax`, `vredmin`).
- [ ] Microarchitecture L1/L2 hardware data stream/stride prefetcher for detailed and cycle-accurate pipeline modes.
- [ ] TUI `vtype`-aware dynamic vector register file grid inspector with active versus tail element differentiation.
- [ ] Preserved cycle-accurate behavior and validated RV32/RV64 release gates.

Before publication:

1. [ ] Run RV64 and RV32 builds and gate suites (100% passing).
2. [ ] Validate JSON schemas, release metadata, packages, and generated artifact names against the release workflow.
3. [ ] Create the RC.13 PR targeting `dev`, and merge only after required CI is green.
4. [ ] Tag the merged `dev` commit (`v3.0.0-rc.13`) and publish the GitHub prerelease only after release-binaries CI completes and package assets are validated.

The public artifact is the RV64-capable `simrv` binary; native RV32 remains a strict-width CI
oracle. Keep generated packages and qualification output under `/tmp` or `/scratch`.
