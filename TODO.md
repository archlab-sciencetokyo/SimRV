# SimRV 3.0.0 release-candidate handoff

Branch: `release/3.0.0-rc.16`
Target: `dev`

## Candidate scope for v3.0.0-rc.16

- [x] Standard RVA23 profile as default architecture configuration (Zicond, CMO, Svinval).
- [x] Privileged specification alignment, exception priority ordering, and SBI v3.0 interfaces.
- [x] Page Directory Cache (PDC) address translation acceleration and PPN masking fix.
- [x] Parallel NOR Flash (PFlash) emulation for UEFI firmware and NVRAM variable storage.
- [x] Standardized FDT generation and UEFI GPT disk image creation utility (`scripts/create_uefi_disk.py`).
- [x] Zihpm hardware performance counters and interconnect telemetry integration.
- [x] TUI keybinding and inspector page metadata as shared sources of truth.
- [x] CLI option metadata and shared SoC platform-map descriptors.
- [x] Classroom mission workflow, source examples, and TileLink contention display.
- [x] Configuration restart confirmation, forwarding status, and slow-mode detail capture.
- [x] Complete RV64 and RV32 release builds and gate suites.
- [x] Validate release metadata, native package contents and dependencies, and SDK install behavior.
- [x] Run release-tool, schema, and RV32/RV64 binary qualification checks.
- [ ] Hosted RV32 workflow qualification remains for CI. `lintian` and `rpmlint` were unavailable
      locally; DEB metadata and RPM package digests were checked with native package tools.

This candidate is being qualified locally. Do not tag or publish it as part of this handoff.
The distributed artifact remains the RV64-capable `simrv` binary; native RV32 is the strict-width
validation target. Packages include source examples and model configurations, not cross compilers,
generated guest binaries, Linux images, or external test suites.
