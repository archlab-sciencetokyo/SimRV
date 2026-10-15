# SimRV 3.0.0 release-candidate handoff

Branch: `release/3.0.0-rc.11`
Target: `dev`

## Candidate scope for v3.0.0-rc.11

- [ ] Soft TLB direct host-page fast-path translation and redundant PMP bypass.
- [ ] PCIe MSI and MSI-X message-signaled interrupt capability and routing to AIA.
- [ ] Basic-block straight-line instruction chaining in instruction-fast mode.
- [ ] Preserved cycle-accurate behavior and validated RV32/RV64 release gates.

Before publication:

1. [ ] Run RV64 and RV32 builds and gate suites (100% passing).
2. [ ] Validate JSON schemas, release metadata, packages, and generated artifact names against the release workflow.
3. [ ] Create the RC.11 PR targeting `dev`, and merge only after required CI is green.
4. [ ] Tag the merged `dev` commit (`v3.0.0-rc.11`) and publish the GitHub prerelease only after release-binaries CI completes and package assets are validated.

The public artifact is the RV64-capable `simrv` binary; native RV32 remains a strict-width CI
oracle. Keep generated packages and qualification output under `/tmp` or `/scratch`.
