# SimRV 3.0 alpha handoff

Branch: `release/3.0.0-alpha.1`  
PR: https://github.com/archlab-sciencetokyo/SimRV/pull/26 (target: `dev`)  
Latest commit: `52d5650 fix(runtime): quiesce SMP execution for TUI control`

Do not create a tag or GitHub release. Keep the PR as the delivery vehicle until all
required qualification checks are green.

## Immediate release blocker: TSan snapshot race

The current GitHub Actions run is `33397053244`. The `thread-sanitizer` job failed;
the RV32/RV64 build and quality jobs were still pending when this handoff was written.

TSan reports a race between:

- `PipelineSim::get_stats()` read from
  `Machine::publish_tui_execution_snapshot_for_hart()`
- `PipelineSim::update_stats()` write from an MT-SMP CA worker's
  `CPU::advance_ca_cycle()`

The issue is architectural: a TUI snapshot must never read a hart's live pipeline,
cache, or CPU counters from another execution thread. The existing atomic snapshot
slot only protects TUI readers; it does not make the source-state copy safe.

Implemented locally (pending TSan certification):

1. Each CPU now owns `tui_snapshot_mutex`, acquired only while TUI telemetry is
   enabled.
2. `CPU::run_cycle()` and `CPU::run_cycle_baremetal()` hold their hart's lock for
   the architectural/pipeline transition.
3. `publish_tui_execution_snapshot_for_hart()` holds that same hart's lock while
   copying live state to the atomic display slot.

This preserves parallel execution across harts and keeps headless execution lock-free,
while preventing a snapshot from observing a concurrent transition of the same hart.
The patch passed the RV64 strict release focused suite. Rebuild `build/tsan` with GCC
and run:

`TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1 ctest --test-dir build/tsan --output-on-failure -L thread`.

Files to inspect:

- `src/core/Machine.cpp`: `publish_tui_execution_snapshot*`, runner activity and
  worker loops.
- `src/pipeline/PipelineSim.cpp` / `include/simrv/pipeline/PipelineSim.hpp`:
  `get_stats()` and `update_stats()`.
- `tests/ModernPlatformTests.cpp`: `test_ca_mt_smp_pause_and_snapshots`.

## Completed in the current branch

- SimRV is versioned as `3.0.0-alpha.1`; the branch and draft PR target `dev`.
- CA/SMP TUI pause waits for primary and secondary runner quiescence.
- TUI step now uses `Machine::step()` and the CA runner, so it steps all harts once.
- Vector and FMA decoder dispatch uses checked dense decode tables; the remaining
  vector whole-register move path is an explicit secondary-field legality decoder.
- Core decoder regressions cover legal and reserved FMA formats and whole-register
  vector group encodings.
- Fixed the Clang unused-constant warning in `BranchPredictor`.
- Fixed `test_global_cycle_timer_phase_ordering` so it checks relative rather than
  assumed-reset cycle counts in every build type.
- Corrected VirtIO sound's device type to 25, yielding modern PCI device ID `0x1059`.
- Hardened SMP test startup/resume waits with two-second deadlines; ASan/UBSan
  modern-platform passed five consecutive runs with leak detection disabled.

## Local qualification already completed

- RV64 strict Clang release build and focused gate tests pass:
  `core-semantics`, `modern-platform`, `pipeline-models`, `tui-framework`.
- ASan/UBSan focused suites pass with `ASAN_OPTIONS=detect_leaks=0`.
- This workspace cannot run LeakSanitizer with `detect_leaks=1` because its process
  wrapper uses ptrace; GitHub CI remains the authoritative leak-enabled run.

## Next refactor after alpha qualification is green

Implement decoded-instruction metadata before changing pipeline behavior.

1. Add a compact trait record adjacent to `OperationInfo`: source operands used
   (`rs1`, `rs2`, `rs3`), source/destination register banks, destination presence,
   memory access kind, and control-flow/CSR side effects.
2. Replace duplicated opcode classification switches in the hazard, retirement,
   trace, and instruction-explainer paths with those traits.
3. Build a register-bank-aware scoreboard from the traits. Preserve current integer
   forwarding; begin with conservative FP availability through retirement.
4. Add three- and five-stage regressions for FP load/use, FP-to-integer conversion,
   FP stores, and FMA `rs3` dependencies before relaxing any stalls.

Avoid a broad execution rewrite until the metadata is proven by those regressions.

## Before declaring alpha qualification complete

- All PR checks must be green, including TSan and ASan/UBSan.
- Re-run RV32 and RV64 release gates and ISA suites after the TSan fix.
- Keep no compatibility aliases for removed 2.x rollback/reverse stepping or pipeline
  modes.
- Do not tag or publish; advance to an RC only after alpha feedback and a clean
  required-suite record.
