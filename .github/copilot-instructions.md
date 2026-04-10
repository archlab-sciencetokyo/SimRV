# Project Guidelines

## Scope
These instructions apply to the whole SimRV workspace. Keep changes behavior-preserving unless the task explicitly asks for functional changes.

## Build and Test
Use CMake presets as the default workflow.

```bash
cmake --preset ninja-clang-release
cmake --build --preset ninja-clang-release
```

Run the required pre-PR validation gate:

```bash
cmake --build --preset ninja-clang-release --target phase1-gate
```

Useful additional targets:

```bash
cmake --build --preset ninja-clang-release --target regress
cmake --build --preset ninja-clang-release --target isa-tests
cmake --build --preset ninja-clang-release --target isa-smoke-rv32gc
cmake --build --preset ninja-clang-release --target isa-ext-rv32gc-experimental
```

If `riscv-tests` is not at the default location, set `RISCV_TESTS_DIR` before ISA targets.

## Architecture
Treat `Machine` as the orchestration boundary for pipeline, memory, and devices. Keep helper boundaries explicit:
- Decode logic through `DecodeUnit`
- Integer/branch/AMO/CSR execute logic in `ExecuteUnitInt.cpp`
- FP execute logic in `ExecuteUnit.cpp` and `ExecuteUnitFloat.cpp`
- CSR/TLB/interrupt/trap ownership in dedicated state-control components

Authoritative architecture references:
- `docs/ARCHITECTURE.md`
- `REFACTOR_CLASS_CONCEPT.md`
- `ROADMAP_RV32GC_to_RV64GC.md`

## Conventions
- Follow existing C++23 style and current warning policy from `CMakeLists.txt` (`-Wall -Wextra -Wpedantic`).
- Prefer focused, minimal diffs; avoid broad rewrites across pipeline stages unless requested.
- Preserve wrapper-based refactor direction: keep algorithms stable while improving class boundaries.
- Keep RV32 behavior stable unless task explicitly targets RV64GC migration work.

## Validation and PR Hygiene
Before proposing changes, run at least build + `phase1-gate` when feasible.

Branching and contribution flow details live in:
- `CONTRIBUTING.md`
- `README.md` (GitHub Flow section)

## RV64GC Migration Tracking
The project uses GitHub Issues, Milestones, and Projects to track RV64GC migration progress across 5 phases:

### Phase Status (Updated 2026-04-10)
- **Phase 1** ✓ COMPLETE: RV32 Stability and RV32GC Verification
- **Phase 1.5** ✓ COMPLETE: ISA-Tests Integration for RV32 (9/19 tests passing)
- **Phase 2** 🔄 ACTIVE: OOP Refactor for RV64 Readiness (Due: April 24, 2026)
  - Issue: [#14](https://github.com/archlab-sciencetokyo/SimRV/issues/14)
  - Current: Slices 1-2 complete, ready for Slice 3 (Memory helpers)
  - Branch: `phase-2/oop-refactor-slice3`
  - Guardrail: <3% performance regression, phase1-gate must pass
- **Phase 3** ⏳ UPCOMING: XLEN Abstraction Layer (Due: May 8, 2026)
  - Issue: [#15](https://github.com/archlab-sciencetokyo/SimRV/issues/15)
- **Phase 4** ⏳ UPCOMING: RV64I Baseline Functional (Due: May 22, 2026)
  - Issue: [#16](https://github.com/archlab-sciencetokyo/SimRV/issues/16)
- **Phase 5** ⏳ UPCOMING: RV64GC Completion & Extension Path (Due: June 5, 2026)
  - Issue: [#17](https://github.com/archlab-sciencetokyo/SimRV/issues/17)

### For Phase 2 (OOP Refactor) Work
- Branch pattern: `phase-2/*` (e.g., `phase-2/oop-refactor-slice3`)
- Link PRs to issue #14 using "Closes" or "Linked PR" syntax
- Run phase1-gate before every commit: `cmake --build --preset ninja-clang-release --target phase1-gate`
- Monitor performance on baseline workloads; benchmark before and after major changes
- See `REFACTOR_CLASS_CONCEPT.md` and `ARCHITECTURE.md` for design patterns

### Tracking Links
- **GitHub Project**: See "Projects" tab for visual board
- **Label system**: `milestone-phaseX`, `type-epic`, `component-*`, `rv64-migration`
- **Milestones**: View all phases and deadlines in the Milestones tab

## Pitfalls
- `isa-tests` and smoke targets require external `riscv-tests` artifacts.
- Linux/app smoke checks in `scripts/regression.sh` are optional and require environment-provided images.
- `isa-ext-rv32gc-experimental` tracks bring-up behavior and is not equivalent to stable gate coverage.
