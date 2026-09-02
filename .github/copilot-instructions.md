# Project Guidelines

## Scope
These instructions apply to the whole SimRV workspace. Keep changes behavior-preserving unless the task explicitly asks for functional changes.

## Build and Test
Use CMake presets as the default workflow.

```bash
cmake --preset rv64-release
cmake --build --preset rv64-release
```

Run the required pre-PR validation gate:

```bash
ctest --preset rv64-gate
ctest --preset rv32-gate
```

Useful additional targets:

```bash
ctest --test-dir build/rv64-release --output-on-failure -L regress
cmake --build build/rv64-release --target isa-gate
cmake --build build/rv64-release --target integration-gate
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
Before proposing changes, run at least the relevant build and CTest gate when feasible. Changes to
shared execution code require both `rv32-gate` and `rv64-gate`.

Branching and contribution flow details live in:
- `CONTRIBUTING.md`
- `README.md` (GitHub Flow section)

## Pitfalls
- ISA and application smoke tests require external `riscv-tests` artifacts or `SIMRV_APP_IMG`.
- Linux integration tests require the image variables documented in `docs/RELEASE.md`.
- Performance comparisons require the same compiler, host, guest, configuration, and instruction
  limit; use repeated warmups and samples.

<!-- mermaid-ai-skills:start -->
## Mermaid Diagrams

When the user asks to create, edit, or visualize a diagram, follow the
instructions in `.github/instructions/mermaid.instructions.md`.
<!-- mermaid-ai-skills:end -->
