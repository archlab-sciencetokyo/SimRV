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
  - **Slices 1-4 Complete** (Merged to dev):
    - ✅ Slice 1-2 (CPU refactor): PipelineContext extraction from CPU
    - ✅ Slice 3 (Memory helpers): PipelineContext integration in Microcn
    - ✅ Slice 4 (Microcontroller): Microcn extraction & C++23 modernization (merged as commit 393e114)
  - 🔄 **Slice 5 (IN PROGRESS)**: Comprehensive testing & validation
    - Extended regression testing (include Linux boot, ISA tests)
    - Performance profiling & regression analysis
    - Cross-slice validation
    - Branch: `phase-2/oop-refactor-slice5`
    - Run: `cmake --build --preset ninja-clang-release --target phase2-gate`
  - Branch tracking: `phase-2/oop-refactor-slice*`
  - Guardrail: <3% performance regression, phase1-gate must pass
- **Phase 3** ⏳ UPCOMING: XLEN Abstraction Layer (Due: May 8, 2026)
  - Issue: [#15](https://github.com/archlab-sciencetokyo/SimRV/issues/15)
- **Phase 4** ⏳ UPCOMING: RV64I Baseline Functional (Due: May 22, 2026)
  - Issue: [#16](https://github.com/archlab-sciencetokyo/SimRV/issues/16)
- **Phase 5** ⏳ UPCOMING: RV64GC Completion & Extension Path (Due: June 5, 2026)
  - Issue: [#17](https://github.com/archlab-sciencetokyo/SimRV/issues/17)

### For Phase 2 (OOP Refactor) Work
- Branch pattern: `phase-2/oop-refactor-slice*` (e.g., `phase-2/oop-refactor-slice5`)
- Link PRs to issue #14 using "Closes" syntax in commit messages
- Run phase1-gate before every commit: `cmake --build --preset ninja-clang-release --target phase1-gate`
- Run phase2-gate for comprehensive validation: `cmake --build --preset ninja-clang-release --target phase2-gate`
- Monitor performance on baseline workloads; benchmark before and after major changes
- See `REFACTOR_CLASS_CONCEPT.md` and `ARCHITECTURE.md` for design patterns

### Phase 2 Slice Descriptions
- **Slices 1-2**: CPU refactor (PipelineContext extraction from Machine, FETCH→WRITEBACK consolidation)
- **Slice 3**: Minimal controller refactor (PipelineContext integration in Microcn::exec())
- **Slice 4**: Microcontroller extraction (remove old members, add System ISA support, error handling, C++23 modernization)
- **Slice 5**: Comprehensive testing & validation (all regression paths, Linux boot, ISA tests, performance profiling)

### Comprehensive Validation (Slice 5)
The `phase2-gate` target runs comprehensive tests across:
1. **Build** - CMake configuration and compilation
2. **CLI** - Help output, error handling (required)
3. **Application** - Optional smoke test if SIMRV_APP_IMG set
4. **Linux Boot** - Extended test with full kernel boot if images available (NEW in Slice 5)
5. **ISA Tests** - riscv-isa-tests execution if available
6. **Performance** - Baseline profiling and regression analysis

Run: `bash scripts/phase2-gate.sh` or `cmake --build --preset ninja-clang-release --target phase2-gate`

### Tracking Links
- **GitHub Project**: See "Projects" tab for visual board
- **Label system**: `milestone-phaseX`, `type-epic`, `component-*`, `rv64-migration`
- **Milestones**: View all phases and deadlines in the Milestones tab

## Pitfalls
- `isa-tests` and smoke targets require external `riscv-tests` artifacts.
- Linux boot test in Slice 5 requires SIMRV_LINUX_MEM_IMG and SIMRV_LINUX_DISK_IMG to be set.
- `isa-ext-rv32gc-experimental` tracks bring-up behavior and is not equivalent to stable gate coverage.
- Slice 5 comprehensive testing is "no-skip" - all available tests are attempted.

<!-- mermaid-ai-skills:start -->
## Mermaid Diagrams

When the user asks to create, edit, or visualize a diagram, follow the
instructions in `.github/instructions/mermaid.instructions.md`.
<!-- mermaid-ai-skills:end -->
