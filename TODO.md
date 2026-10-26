# SimRV 3.0.0 release-candidate handoff

Branch: `release/3.0.0-rc.17`
Target: `dev`

## Candidate scope for v3.0.0-rc.17

- [ ] Integrate RVA23 profile qualification through ACT (`riscv-arch-test`) into CI.
- [ ] Implement architectural test signature dumping (`--signature <file>`).
- [ ] Clean up redundant tests and streamline CI execution matrix.
- [x] Optimize GCC link and test build performance by disabling LTO on native test targets.
- [ ] Complete RV64 and RV32 release builds and gate suites.
- [ ] Validate release metadata, native package contents and dependencies, and SDK install behavior.
- [x] Run release-tool, schema, and RV32/RV64 binary qualification checks.
- [ ] Hosted RV32 workflow qualification remains for CI. `lintian` and `rpmlint` were unavailable
      locally; DEB metadata and RPM package digests were checked with native package tools.

This candidate is being qualified locally. Do not tag or publish it as part of this handoff.
The distributed artifact remains the RV64-capable `simrv` binary; native RV32 is the strict-width
validation target. Packages include source examples and model configurations, not cross compilers,
generated guest binaries, Linux images, or external test suites.
