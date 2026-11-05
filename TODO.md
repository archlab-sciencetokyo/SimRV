# SimRV 3.0.0 release-candidate handoff

Branch: `release/3.0.0-rc.19`
Target: `dev`

## Candidate scope for v3.0.0-rc.19

- [x] Flatten ALU opcode dispatch in fast execution path into dense compiler jump table.
- [x] Unify partitioned execution switches into a single contiguous fast dispatch jump table.
- [x] Remove legacy `-fno-jump-tables` compile override on `CpuFastExecution.cpp`.
- [x] Measure and verify execution throughput improvements across CoreMark benchmarks (+26.7% throughput gain, 450+ MIPS).
- [x] Fix plugin package component placement (`COMPONENT Tools`) to prevent dynamic runtime shared library leaks into `simrv.deb`.
- [x] Add `gcc-toolset-15-libstdc++-devel` to UBI9 RPM release workflow for static C++ runtime linkage.
- [x] Validate dual-architecture release gates on RV64 (57/57 passed) and RV32 (56/56 passed).
- [x] Validate release metadata, schema consistency, and code formatting across codebase.
- [ ] Hosted RV32 workflow qualification remains for CI. `lintian` and `rpmlint` were unavailable
      locally; DEB metadata and RPM package digests were checked with native package tools.

This candidate is being qualified locally. Do not tag or publish it as part of this handoff.
The distributed artifact remains the RV64-capable `simrv` binary; native RV32 is the strict-width
validation target. Packages include source examples and model configurations, not cross compilers,
generated guest binaries, Linux images, or external test suites.
