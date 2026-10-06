# SimRV 3.0.0 release-candidate handoff

Branch: `release/3.0.0-rc.6`
Target: `dev`

This RC adds split architectural event streams and schemas, trace levels and filters, execution
markers, call/return and interrupt tracing, richer device/bus/memory/pipeline events, sparse indexes,
compression, deterministic metadata, standard G ISA spellings, and isolated source installs. The
feature PR has merged into `dev`. Do not create the tag until the release metadata PR is merged,
release CI is green, and package assets have been validated.

Release qualification:

1. Validate `scripts/release_check.py`, schemas, package names, and the changelog/CITATION version.
2. Merge this release metadata branch into `dev` only after required checks pass.
3. Create and push the annotated `v3.0.0-rc.6` tag from the merged `dev` commit.
4. Wait for release-binaries CI; validate the static portable TGZ, RPM, and DEB bundles, then
   publish the GitHub prerelease with the changelog and generated release assets.

The public artifact is the RV64-capable `simrv` binary; native RV32 remains a strict-width CI
oracle. Keep generated packages and qualification output under `/tmp` or `/scratch`.

## Candidate for v3.0.0-rc.7

- Define and implement GDB-aware checkpoint/restore semantics. Current checkpoints save hart
  architectural state and RAM only; they do not preserve debugger protocol/session state,
  breakpoints, or device state. Specify which debugger state should be restored and how an active
  GDB connection behaves before extending the checkpoint format.
- Add opt-in retirement-time FP/vector register-write events for downstream reports. Preserve
  architectural `f0`–`f31` and `v0`–`v31` indices and exact before/after bits; include hart, cycle,
  PC/instruction, and relevant `fcsr`/FLEN or `VLEN`/`vl`/`vtype` context. ABI aliases and decoded
  FP/lane views should be supplemental, and event filters should keep trace volume bounded.
- Diagnose and eliminate the occasional `[WARN] Terminal raw mode setup failed; continuing in
  current mode` message during CLI execution; verify CLI operation does not attempt terminal
  raw-mode setup unless it actually owns an interactive terminal.
