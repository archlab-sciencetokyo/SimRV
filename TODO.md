# SimRV 3.0.0 release-candidate handoff

Branch: `release/3.0.0-rc.10`
Target: `dev`

## Candidate scope for v3.0.0-rc.10 (Completed)

- [x] Modernize device tree handling: replaced synthetic `FdtGenerator` with lightweight `FdtPatcher`.
- [x] Standardize Linux boot on user-supplied or environment-configured base DTB with dynamic RAM/SMP patching.
- [x] Synchronize ratified Privileged 1.13 and SBI v2.0/v3.0 specification alignment.
- [x] Unified platform configuration `--config` and hardened memory mapping.
- [x] Hardened Unix CLI behaviors, signals, terminal restoration, job control, and `--log-stderr`.
- [x] TUI Sixel integer scaling and default 800x600 framebuffer with dynamic guest DTB adaptation.
- [x] Simulation hot-path performance speedups (decode cache, inlined branch predictor, bus lock bypass, zero-virtual MMIO router).
- [x] PCIe and MMIO modernization (domain-specific typing, fast active BAR routing, 64-bit BAR probe sizing, INTx deassertion, centralized DMA engine).
- [x] Preserved cycle-accurate behavior and validated RV32/RV64 release gates (48/48 tests on both widths).

Before publication:

1. [x] Run RV64 and RV32 builds and gate suites (100% passing).
2. [x] Validate JSON schemas, release metadata, packages, and generated artifact names against the release workflow.
3. [ ] Create the RC.10 PR targeting `dev`, and merge only after required CI is green.
4. [ ] Tag the merged `dev` commit (`v3.0.0-rc.10`) and publish the GitHub prerelease only after release-binaries CI completes and package assets are validated.

The public artifact is the RV64-capable `simrv` binary; native RV32 remains a strict-width CI
oracle. Keep generated packages and qualification output under `/tmp` or `/scratch`.
