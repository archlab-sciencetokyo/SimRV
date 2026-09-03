# SimRV 3.0 alpha handoff

Branch: `release/3.0.0-alpha.2`
PR: https://github.com/archlab-sciencetokyo/SimRV/pull/26 (target: `dev`)
Latest functional commit: `8a24c2c refactor(decoder): index fused operations by opcode`

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

- SimRV is versioned as `3.0.0-alpha.2`; the branch and draft PR target `dev`.
- CA/SMP TUI pause waits for primary and secondary runner quiescence.
- TUI step now uses `Machine::step()` and the CA runner, so it steps all harts once.
- Vector and FMA decoder dispatch uses checked dense decode tables; the remaining
  vector whole-register move path is an explicit secondary-field legality decoder.
- FMA dispatch indexes its checked table directly by the encoded opcode and format,
  with no separate opcode-to-row selection switch.
- Core decoder regressions cover legal and reserved FMA formats and whole-register
  vector group encodings.
- Fixed the Clang unused-constant warning in `BranchPredictor`.
- Fixed `test_global_cycle_timer_phase_ordering` so it checks relative rather than
  assumed-reset cycle counts in every build type.
- Corrected VirtIO sound's device type to 25, yielding modern PCI device ID `0x1059`.
- Hardened SMP test startup/resume waits with two-second deadlines; ASan/UBSan
  modern-platform passed five consecutive runs with leak detection disabled.
- Consolidated the identical Baremetal/OS worker-quiescence wait loops into one
  self-worker-aware helper, so pause/shutdown semantics cannot drift between runners.
- Added an RAII activity-counter guard for primary runner cycles and fast batches, ensuring every
  exit path decrements and wakes quiescence waiters while retaining the established runner-flag
  ordering.

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

## Performance follow-up handover (September 2026)

### Landed and qualified locally

- The fast decode cache is now a compact, two-way 2,048-set cache with a maximum
  RV64 footprint of 256 KiB per hart.  It uses invalid-way-first insertion and
  insertion-only round-robin replacement; lookups no longer write cache metadata.
- Integrated the micro-op decode cache into the cycle-accurate (CA) pipeline's
  fetch stage (`CPU::run_fetch_stage`). Cache hits skip full RVC decompression
  (`decompressInstruction`) and decoder dispatch, reconstructing the per-slot
  `PipelineContext` while preserving mode-specific legality checks.
- Precomputed pipeline dependency traits: integrated compile-time metadata traits
  from `OperationTraits.hpp` (`is_rs1_int`, `is_rs2_int`, `is_rs1_fp`, `is_rs2_fp`,
  `is_rs3_fp`, `writes_integer`, `writes_float`, `is_serializing`) into
  `CycleKernel.cpp` hazard detection, source readiness checks, and slot classification.
  Added bit-for-bit cycle and data-hazard stall counter tests in `PipelineHazardTests.cpp`
  verifying identical behavior across 3-stage and 5-stage models.
- Hardened cache flush safety on CSR writes: `mstatus` writes affecting `FS`,
  `VS`, or effective `XLEN` (`SXL`/`UXL`) now trigger a `CPU::TLB_flush()`
  and decode cache invalidation, preventing stale execution privileges.
- Compressed instruction definitions and decompressor modernized: introduced
  strongly-typed `Q0Op`, `Q1Op`, and `Q2Op` enums in `include/simrv/isa/Compressed.hpp`
  matching RISC-V Compressed ISA specification tables, added `c_rs1_p()` accessor,
  and replaced raw literals in `Decoder.cpp`.
- Added regression tests in `CoreSemanticsTests.cpp` covering RVC decompression
  legality, canonical `C.NOP` / `C.ADDI` expansion, `c_rs1_p` consistency, and
  `mstatus.FS` decode cache invalidation.
- Host-memory fast loads and stores use fixed-width unaligned helpers.  RV32
  `funct3 == 3` is correctly a four-byte operation rather than an unsafe eight-byte
  access through a four-byte word.
- The presentation-only CPU scoreboard moved into the published TUI execution
  snapshot.  Headless execution does not construct scoreboard presentation data.
- `rv64-native-release` is explicitly Clang-backed and benchmark command generation
  emits exactly one execution selector (`--os` or `--baremetal`).
- RV32 and RV64 CTest gate suites both passed locally (19/19 each), as did the
  Clang native-release preset configuration and build.

### Controlled benchmark record

Two warmups and seven measured runs were used for each sample.  The following
measurements are useful baselines for follow-up changes; retain only changes that
preserve architectural and cycle-model counters and do not regress any mode by more
than 3%.

| Workload / mode | Before median wall time | After median wall time |
| --- | ---: | ---: |
| CoreMark, fast, 20M instructions | 0.11358 s | 0.08658 s |
| aha-mont, fast | 0.00501 s | 0.00467 s |
| aha-mont, detailed | 0.02794 s | 0.02439 s |
| aha-mont, CA 3-stage | 1.09867 s | 1.07198 s |
| aha-mont, CA 5-stage | 1.32372 s | 1.29341 s |

CoreMark therefore improved by about 31% in median wall-throughput (176.1 to
231.0 M instructions/s).  Do not compare these values across different hosts or
toolchains; repeat the same controlled workflow instead.

### Next performance work, in priority order

1. **L1 Instruction Stream Prefetching.** Implement configurable hardware next-line / stream
   prefetching for `ICache` in CA mode using TileLink non-blocking prefetch intents (`TlIntent::PrefetchRead`).
2. **Slim branch-predictor update without changing statistics.** Predictor update
   is about 8% of the sampled CA host CPU time.  Separate unavoidable predictor state
   mutation from optional accounting only if every existing visible counter, decision,
   and training event stays unchanged.  Benchmark both branch-heavy and straight-line
   programs; do not trade trace/TUI observability for speed by default.
3. **Further cached fast-memory specialization.** Fast-mode cached loads are about
   11% and stores about 3% of sampled host CPU time.  Investigate a direct-RAM path
   only after proving its guards cover alignment, MMIO/tohost, address translation,
   privilege, traps, and dynamically changed machine configuration.  Prefer a
   per-operation precomputed access class over adding another broad execution switch.

### Useful reproduction commands

Use `simrv-benchmark` skill guidance for the current official measurement procedure.
For a focused local profile, build `rv64-release` and run:

```bash
perf record -q -o /tmp/simrv-cycle-perf.data -e cpu-clock --call-graph dwarf -- \
  ./build/rv64-release/SimRV --cli --baremetal \
  -m ../../tests/riscv-tests/benchmarks/aha-mont64.riscv -e 10000000 \
  --mode cycle-accurate --pipeline 5stage
perf report -i /tmp/simrv-cycle-perf.data --no-children
```

The profile that informed this handover placed `CPU::run_ca_pipeline_cycle` first,
followed by branch-predictor update, fetch/decompression/decode, and dependency
resolution.  Repeat it after each isolated change; profile shape is evidence, not a
substitute for the controlled benchmark gate above.
