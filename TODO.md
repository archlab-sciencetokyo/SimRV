# SimRV 3.0.0 release-candidate handoff

Branch: `release/3.0.0-rc.15`
Target: `dev`

## Candidate scope for v3.0.0-rc.15

- [x] Structured logging architecture (`LogRecord`, `LogBuffer`, `TuiRegPage::LOG`, warning/error tallying).
- [x] Full-screen interactive Log Viewer modal (`[L]`) with follow/tail mode, level filters, live search, and copy.
- [x] Keybinding conflict resolution: `[d]` reserved for HexDump, removed `[d]` from log scrolling.
- [x] Interactive cell clicking in `MemoryModal` across 16-byte Hex and ASCII columns.
- [x] Double-click word selection, triple-click line selection, and automatic OSC 52 clipboard export.
- [x] Fixed tool menu dropdown disappearing when `PIPELINE` was selected in IA mode.
- [x] Collapsible bottom log drawer (`Ctrl-L`), responsive guidance reflow, and mission complete status indicator.
- [x] Dual-architecture validation on RV64 and native RV32 (48/48 gate tests passed).

Before publication:

1. [x] Run RV64 and RV32 builds and gate suites (100% passing).
2. [x] Validate JSON schemas, release metadata, packages, and generated artifact names against the release workflow.
3. [ ] Create the RC.15 PR targeting `dev`, and merge only after required CI is green.
4. [ ] Tag the merged `dev` commit (`v3.0.0-rc.15`) and publish the GitHub prerelease only after release-binaries CI completes and package assets are validated.

The public artifact is the RV64-capable `simrv` binary; native RV32 remains a strict-width CI
oracle. Keep generated packages and qualification output under `/tmp` or `/scratch`.
