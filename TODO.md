# SimRV 3.0.0 release-candidate handoff

Branch: `release/3.0.0-rc.8`
Target: `dev`

## Candidate scope for v3.0.0-rc.8

- Remove deprecated CLI/config section aliases and unused compatibility wrappers.
- Simplify internal code and improve instruction-fast performance based on measured profiles.
- Keep performance evidence non-blocking; preserve guest-visible architectural behavior.

Before publication:

1. Run RV64 and RV32 builds and gate suites; run hosted-RV32 on the RV64 build.
2. Validate JSON schemas, release metadata, packages, and generated artifact names against the
   release workflow.
3. Create the RC.8 PR targeting `dev`, and merge only after required CI is green.
4. Tag the merged `dev` commit and publish the GitHub prerelease only after release-binaries CI
   completes and package assets are validated.

The public artifact is the RV64-capable `simrv` binary; native RV32 remains a strict-width CI
oracle. Keep generated packages and qualification output under `/tmp` or `/scratch`.
