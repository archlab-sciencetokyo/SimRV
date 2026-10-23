# SimRV 3.0.0 release-candidate handoff

Branch: `release/3.0.0-rc.16`
Target: `dev`

## Candidate scope for v3.0.0-rc.16

- [x] TUI keybinding and inspector page metadata as shared sources of truth.
- [x] CLI option metadata and shared SoC platform-map descriptors.
- [x] Classroom mission workflow, source examples, and TileLink contention display.
- [x] Configuration restart confirmation, forwarding status, and slow-mode detail capture.
- [ ] Complete RV64 and RV32 builds and gate suites.
- [ ] Validate release metadata, package contents, package dependencies, and install behavior.
- [ ] Run release-tool, schema, hosted-RV32, and binary qualification checks.

This candidate is being qualified locally. Do not tag or publish it as part of this handoff.
The distributed artifact remains the RV64-capable `simrv` binary; native RV32 is the strict-width
validation target. Packages include source examples and model configurations, not cross compilers,
generated guest binaries, Linux images, or external test suites.
