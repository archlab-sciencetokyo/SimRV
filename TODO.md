# SimRV 3.0.0 release-candidate handoff

Branch: `release/3.0.0-rc.5`
Target: `dev`

This RC consolidates the public RV64-capable runtime, native package bundles, static musl portable
archive, supported-distro checks, attachable Unix-socket TUI, capability diagnostics, versioned
SoC manifests, and the profile-to-preset terminology cleanup. Do not create the tag until release
CI is green.

Release qualification:

1. Run the RV32 and RV64 gate suites and `scripts/release_check.py`.
2. Validate the static portable TGZ, RPM repository bundle, and DEB repository bundle.
3. Confirm clean package installs and `simrv --version` across the supported distro matrix.
4. Soak attach/detach, reconnect, reboot lifecycle notifications, and non-TTY output.
5. Run the GA smoke workflow on both native binaries and validate the exported SoC manifest schema.
6. Merge this release branch into `dev`, then create and push `v3.0.0-rc.5` as a prerelease.

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
