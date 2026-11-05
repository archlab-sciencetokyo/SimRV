# SimRV 3.0.0 release-candidate handoff

Branch: `release/3.0.0-rc.18`
Target: `dev`

## Candidate scope for v3.0.0-rc.18

- [x] Formalize PLIC level-line tracking and dynamic arbitration re-evaluation on MMIO writes.
- [x] Formalize APLIC direct-mode interrupt de-assertion and delivery lifecycle.
- [x] Implement ACLINT 32-bit MMIO timer word writes and multi-hart timer interrupt re-evaluation.
- [x] Unify fast-execution multi-hart platform `mtime` synchronization and UART cycle advance pacing.
- [x] Enforce read-only CSR write illegal instruction exceptions and G-stage PTE fetch PMP checks.
- [x] Complete RV64 and RV32 release builds and gate suites.
- [x] Validate release metadata, native package contents and dependencies, and SDK install behavior.
- [x] Run release-tool, schema, and RV32/RV64 binary qualification checks.
- [x] Implement dynamic External Device Plugin C ABI and decouple model-specific MMIO devices into plugins.
- [x] Formalize CFU and External Device Plugin C ABI and decouple Tokyo Tech hardware models.
- [x] Resolve 32-bit architectural personality MISA XLEN CSR bit 30 representation on RV64 builds.
- [ ] Hosted RV32 workflow qualification remains for CI. `lintian` and `rpmlint` were unavailable
      locally; DEB metadata and RPM package digests were checked with native package tools.

This candidate is being qualified locally. Do not tag or publish it as part of this handoff.
The distributed artifact remains the RV64-capable `simrv` binary; native RV32 is the strict-width
validation target. Packages include source examples and model configurations, not cross compilers,
generated guest binaries, Linux images, or external test suites.
