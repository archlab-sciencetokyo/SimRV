# SimRV 3.0.0 release-candidate handoff

Branch: `feature/rc7-checkpoint-register-traces`
Target: `dev`

## Candidate scope for v3.0.0-rc.7

- Document architectural checkpoint completeness and GDB reconnect semantics. Checkpoints restore
  guest architectural state and RAM, but do not preserve debugger sessions, breakpoints, or RSP
  negotiation state.
- Add opt-in retirement-time FP/vector register-write events to `registers.jsonl`, preserving
  architectural register indices and raw bits with instruction and vector/FP context. Retirement
  records remain compatible.
- Rework terminal presentation: avoid raw mode unless CLI owns the foreground terminal; respect
  standard color environment settings; keep TUI startup/shutdown quiet while surfacing errors; add
  capability-gated OSC 22 pointer affordances over clickable TUI controls; align and wrap CLI help,
  standardize parser diagnostics, and install Bash, Zsh, and Fish shell completions with the runtime
  component.

These changes are in the current feature worktree. Before release qualification:

1. Run RV64 and RV32 builds and gate suites; run hosted-RV32 on the RV64 build.
2. Validate JSON schemas, release metadata, packages, and generated artifact names against the
   release workflow.
3. Create the RC.7 release-metadata branch/PR, and merge only after required CI is green.
4. Tag the merged `dev` commit and publish the GitHub prerelease only after release-binaries CI
   completes and package assets are validated.

The public artifact is the RV64-capable `simrv` binary; native RV32 remains a strict-width CI
oracle. Keep generated packages and qualification output under `/tmp` or `/scratch`.
