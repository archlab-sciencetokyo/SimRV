# SimRV 3.0.0 GA release

Branch: `release/3.0.0`
Target: `dev`

## Completed scope for v3.0.0

- [x] Fix core, ISA, CSR, pipeline, memory, FDT, explainer, trace, schema, and tooling audit findings ahead of GA.
- [x] Align CI workflows, man pages, user/architecture documentation, and `.agents` workflows with 3.0.0 CLI and 4-package layout.
- [x] Synchronize version `3.0.0` across `CMakeLists.txt`, `release/release-manifest.json`, `CITATION.cff`, and `CHANGELOG.md`.
- [x] Add regression tests covering audit fixes across `CoreSemanticsTests`, `RiscvProfileTests`, `TraceTests`, `FdtGeneratorTests`, and `ReleaseTests`.
- [ ] Run remote CI qualification matrix across PR to `dev` before creating git tags or GitHub release assets.

- [x] Replace fixed ISA/pipeline presets with dynamic `IsaSpec`, `discover_cpu_models()`, and `PipelineTopology` NTTP kernels.
- [x] Implement protocol-neutral `Bus` and `TemplatedBus<BusProtocolSpec>` for TileLink-C/UH/UL and AXI4/AXI4-Lite.
- [x] Specialize memory fast paths (`MemoryTopologySpec`), MMU/PTW walks (`SvModeSpec`), and FP/vector traits (`FloatTraits`, `dispatch_sew`).
- [x] Add `rvb23u64` / `rvb23s64` profile coverage and `profile_default_isa_string` mapping.
- [x] Verify RV64 & RV32 gate suites and CoreMark throughput vs. Spike (637.70 MIPS median, 1.067x Spike).

## Completed scope for v3.0.0-rc.19

- [x] Flatten ALU opcode dispatch in fast execution path into dense compiler jump table.
- [x] Unify partitioned execution switches into a single contiguous fast dispatch jump table.
- [x] Remove legacy `-fno-jump-tables` compile override on `CpuFastExecution.cpp`.
- [x] Measure and verify execution throughput improvements across CoreMark benchmarks (+26.7% throughput gain, 450+ MIPS).
- [x] Fix plugin package component placement (`COMPONENT Tools`) to prevent dynamic runtime shared library leaks into `simrv.deb`.
- [x] Add `gcc-toolset-15-libstdc++-devel` to UBI9 RPM release workflow for static C++ runtime linkage.
- [x] Validate RV64 and native RV32 integration gates (34/34 passed on each build).
- [x] Validate hosted RV32 ISA smoke coverage on the RV64 build (37/37 passed).
- [x] Allow vector memory instructions when VS is enabled and scalar FS is disabled.
- [x] Modernize CLI execution summary with adaptive durations, visual progress bar, peak/average MIPS throughput, and conditional RVC share display.
- [x] Unbuffer UART TX in CLI mode to ensure guest prompts display without delay.
- [x] Eliminate legacy Python harnesses and rewrite compliance/benchmarking in native C++.
- [x] Validate release metadata, schema consistency, and code formatting across codebase.
- [ ] Required CI qualification remains pending. The native package-consumer test passed for both
      builds. Local linting of Fedora-generated rc.19 packages found issues to triage: rpmlint 2.9.0
      reported 1 error and 17 warnings across four RPMs; lintian 2.122.0 reported 21 errors and 16
      warnings across four DEBs.

This candidate is being qualified locally. Do not tag or publish it as part of this handoff.
The distributed artifact remains the RV64-capable `simrv` binary; native RV32 is the strict-width
validation target. Packages include source examples and model configurations, not cross compilers,
generated guest binaries, Linux images, or external test suites.
