# SimRV 3.0.0 release-candidate handoff

Branch: `release/3.0.0-rc.10`
Target: `dev`

## Candidate scope for v3.0.0-rc.10

- Modernize device tree handling: replace synthetic `FdtGenerator` with lightweight `FdtPatcher`.
- Standardize Linux boot on user-supplied or environment-configured base DTB with dynamic RAM/SMP patching.
- Synchronize ratified Privileged 1.13 and SBI v2.0/v3.0 specification alignment.
- Preserve cycle-accurate behavior and validate RV32/RV64 release gates.

Before publication:

1. Run RV64 and RV32 builds and gate suites; run hosted-RV32 on the RV64 build.
2. Validate JSON schemas, release metadata, packages, and generated artifact names against the
   release workflow.
3. Create the RC.10 PR targeting `dev`, and merge only after required CI is green.
4. Tag the merged `dev` commit and publish the GitHub prerelease only after release-binaries CI
   completes and package assets are validated.

The public artifact is the RV64-capable `simrv` binary; native RV32 remains a strict-width CI
oracle. Keep generated packages and qualification output under `/tmp` or `/scratch`.
