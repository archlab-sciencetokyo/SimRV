# SimRV Roadmap: Preserve RV32, Verify RV32GC, Refactor for OOP, Then RV64GC

## Goals
1. Preserve existing 32-bit functionality and prevent regressions.
2. Expand and verify RV32 support toward RV32GC confidence.
3. Refactor into clearer OOP components without reducing performance.
4. Prepare architecture for RV64GC and future extension growth.

## Guiding Principles
1. Behavior first: no semantic changes without tests and trace comparison.
2. Small steps: each milestone should be mergeable and independently validated.
3. Performance parity: keep cycle counts and runtime within agreed guardrails.
4. Extension-ready design: isolate XLEN-dependent logic from instruction semantics.

## Current Hotspots (from code structure)
1. ISA decode/decompress/ALU helpers are centralized in module files.
2. Core execution pipeline and machine loop are centralized in machine files.
3. CPU architectural state and CSR/TLB state are centralized in state files.
4. Device paths and MMIO width assumptions are concentrated in disk/console files.

## Phase 0: Baseline and Regression Harness
Objective: lock in current behavior before feature work.

Tasks
1. Define a small canonical workload suite:
- app mode hello run
- linux boot smoke (short timeout)
- compressed instruction smoke path
- M-extension arithmetic smoke path
2. Add scripted regression entry points in Makefile targets (or simple scripts).
3. Capture baseline artifacts:
- instruction count
- optional trace snippets
- pass/fail signatures (tohost, exit condition, or expected logs)
4. Record baseline runtime for parity checks.

Exit Criteria
1. One-command local regression run exists.
2. Baseline outputs are stored and reproducible.
3. Baseline performance numbers recorded.

## Phase 1: RV32 Stability and RV32GC Verification
Objective: strengthen confidence in current RV32 behavior before major refactor.

Tasks
1. Audit and validate C extension path:
- instruction decompression correctness
- PC increment behavior for 16-bit vs 32-bit instructions
2. Audit and validate M/A/C behavior currently implemented.
3. Verify CSR behavior and exception paths for RV32 assumptions.
4. Add targeted edge tests:
- sign extension boundaries
- branch compare signed/unsigned corner cases
- load/store width and alignment behavior

Exit Criteria
1. RV32 regression passes consistently.
2. RV32GC-oriented smoke tests pass for implemented subset.
3. No baseline regressions in trace signatures.

## Phase 1.5: riscv-isa-tests Integration (RV32 focus)
Objective: add standardized ISA-level checks to gate refactors and RV64 migration prep.

Tasks
1. Integrate a runner for `riscv-tests/isa` RV32 test subset.
2. Support `tohost` pass/fail detection in simulator ISA-test mode.
3. Start with stable RV32UI subset and expand incrementally to M/A/C-related tests.
4. Keep logs and per-test status artifacts for CI and local debugging.

Exit Criteria
1. `make isa-tests` runs selected RV32 tests from a configured `RISCV_TESTS_DIR`.
2. Test pass/fail status is machine-readable from logs.
3. ISA-test subset can be used as a required pre-refactor gate.

Current Implementation Status
1. `make isa-smoke` provides a curated RV32IM pass gate.
2. `make phase1-gate` runs baseline regression and ISA smoke together.
3. `rv32uc-p-rvc` is currently excluded because no `tohost` pass/fail marker is emitted yet.

## Phase 2: OOP Refactor Without Performance Loss
Objective: improve structure while preserving behavior and speed.

Current status (2026-04-01)
1. Slice 1 complete: `DecodeUnit` wrapper introduced and wired in `Machine` path.
2. Slice 2 complete: `ExecuteUnit` wrapper introduced and wired in `Machine` path.
3. Documentation baseline added: architecture doc, ADR entry, and Doxygen config/target.

Reference blueprint: `REFACTOR_CLASS_CONCEPT.md`

Refactor Strategy (incremental)
1. Introduce interfaces/classes without changing call patterns first:
- Decoder (decode + decomp)
- ExecUnit (ALU/branch/CSR op helpers)
- MemoryAccess (typed load/store helpers)
- CsrFile (CSR read/write/masks)
2. Keep methods inline/static where performance-critical.
3. Move from free functions and broad shared state access to narrower APIs in small commits.
4. Keep data layout cache-friendly and avoid virtual dispatch in hot paths.
5. Measure runtime after each structural change.

Suggested Commit Slices
1. Extract decode/decompress wrapper class while preserving exact outputs.
2. Extract ALU helper class with unchanged signatures at call sites.
3. Extract CSR utility class and route CPU CSR accesses through it.
4. Extract memory width helper layer and reuse in machine/device paths.
5. Cleanup and rename fields for readability after parity is proven.

Performance Guardrails
1. Runtime regression threshold: <= 3% on baseline workloads.
2. Instruction-count parity required unless behavior change is intentional and documented.
3. If guardrail is exceeded, rollback last refactor slice and optimize before proceeding.

Exit Criteria
1. Pipeline behavior unchanged on baseline suite.
2. Performance remains within threshold.
3. Core modules have clear class boundaries and lower coupling.

## Phase 3: XLEN Abstraction Layer (Prep for RV64)
Objective: make width-dependent logic explicit before enabling RV64 behavior.

Tasks
1. Introduce central width utilities:
- xlen_t, uxlen_t aliases
- sign/zero extension helpers
- shift-mask helpers (RV32: 0x1f, RV64: 0x3f)
2. Separate semantic operation selection from XLEN-specific arithmetic details.
3. Isolate register file width assumptions.
4. Isolate address-width assumptions for MMU/TLB and memory helpers.

Typed Data Expansion Plan (next slices)
1. Add explicit semantic aliases in `Define.hpp` for architectural categories:
- `Address`
- `Instruction`
- `CSRValue`
- `ImmValue`
- `TrapCause`
2. Apply aliases in low-risk header-first passes:
- `State.hpp`: CSR/TLB/trap-related scalar fields
- `PipelineContext.hpp`: decode/execute scalar fields not yet alias-specialized
3. Apply aliases in implementation passes with gate after each pass:
- `machine.cpp` main pipeline path first
- `module.cpp` decode/ALU helper signatures second
- `Microcn` path last to avoid mixing concerns during Slice 3 closure
4. Add helper conversion utilities for signed/unsigned transitions:
- explicit sign-extension helpers by bit width
- explicit cast helpers for `Register <-> Word` and immediate widths
5. Keep behavior-neutral constraints during alias migration:
- no arithmetic rewrites in alias-only commits
- no control-flow changes in alias-only commits
- mandatory `phase1-gate` pass for each alias batch

Exit Criteria
1. Build still defaults to RV32 mode and passes full regression.
2. Width-specific operations are centralized and testable.

## Phase 4: RV64I Bring-up (First Functional 64-bit)
Objective: enable RV64I baseline while keeping RV32 mode available.

Tasks
1. Promote core architectural state width (pc/reg/csr where required by ISA mode).
2. Add RV64-specific instruction semantics:
- 64-bit shifts and compares
- sign-extension semantics where required
- W-suffix operations behavior
3. Update CSR/misa reporting for RV64 mode.
4. Ensure dual-mode operation strategy:
- compile-time or runtime mode selection
- existing RV32 path still validated

Exit Criteria
1. RV64I smoke programs execute correctly.
2. RV32 baseline remains green.
3. No major performance regression relative to RV32 mode baseline.

## Phase 5: RV64GC Completion and Extension-Ready Path
Objective: expand from RV64I to RV64GC and prepare future extensions.

Tasks
1. Complete C behavior validation under RV64.
2. Validate M/A/F/D paths and CSR implications.
3. Add extension registration points to avoid future monolithic decode growth.
4. Add compatibility checks for privileged paths affected by XLEN.

Exit Criteria
1. RV64GC test subset passes.
2. RV32 regression still passes.
3. New extension onboarding path is documented and low-friction.

## Risk Register and Mitigations
1. Risk: hidden 32-bit assumptions in helper functions.
Mitigation: grep/type audit plus width utility centralization.
2. Risk: refactor introduces subtle pipeline behavior changes.
Mitigation: trace comparison and instruction-count gating.
3. Risk: performance drift during abstraction.
Mitigation: no virtual calls in hot path, inline small helpers, benchmark every slice.
4. Risk: device/MMIO path width mismatch.
Mitigation: separate device bus width handling from core XLEN logic.

## First 2-Week Execution Plan
Week 1
1. Finish Phase 0 baseline harness.
2. Add RV32GC-focused smoke checks for C/M/CSR/load-store corner cases.
3. Establish performance baseline and acceptance thresholds.

Week 2
1. Start Phase 2, commit slices 1 and 2 (Decoder, ExecUnit extraction).
2. Validate after each slice (behavior + runtime).
3. Document any hotspots requiring inline optimization.

## Definition of Done for this roadmap stage
1. RV32 behavior preserved and continuously tested.
2. RV32GC confidence materially improved.
3. OOP structure introduced in core execution path with no meaningful slowdown.
4. Codebase is ready to start RV64I implementation safely.
