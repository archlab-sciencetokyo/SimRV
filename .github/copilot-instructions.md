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

## Pitfalls
- `isa-tests` and smoke targets require external `riscv-tests` artifacts.
- Linux/app smoke checks in `scripts/regression.sh` are optional and require environment-provided images.
- `isa-ext-rv32gc-experimental` tracks bring-up behavior and is not equivalent to stable gate coverage.
