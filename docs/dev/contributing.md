# Developer & Contributing Guide

This guide outlines the architecture, coding standards, branch lifecycle, and testing procedures for developers and contributors working on **SimRV**.

---

## 1. Project Organization & Subsystems

SimRV follows a strict separation between public header declarations (`include/simrv/`) and implementation source units (`src/`):

```text
include/simrv/ | src/
├── core/         # Machine, CPU, ArchState, CSR registers, MMU, PMP, traps
├── execute/      # ALU, Multiplier/Divider, FPU, Vector execution units
├── pipeline/     # CycleKernel, Fetch, Decode, Scoreboard, BranchPredictor
├── memory/       # Physical memory, RamView, TileLink bus interconnect
├── cache/        # L1/L2/L3 cache hierarchies, CoherenceHub, MESI protocol
├── device/       # CLINT, PLIC, AIA, 16550A UART, VirtIO (block/net/rng), PCIe
├── tui/          # TUI framework, ScrollView, InspectorPane, TerminalPane, Modals
├── debug/        # GDB RSP server, Tracer, Instruction explainer
└── util/         # CLI parser, FDT generator, CPU model loader
```

---

## 2. Build Environment & CMake Presets

SimRV requires a modern C++23 compiler (**Clang 22+** or **GCC 16+**), **CMake 3.31+**, and **Ninja**.

### Preset Workflows

Always configure and build using CMake presets:

```bash
# RV64 targets
cmake --preset rv64-release
cmake --build --preset rv64-release -j$(nproc)

# RV32 targets
cmake --preset rv32-release
cmake --build --preset rv32-release -j$(nproc)

# Debug targets with AddressSanitizer (ASan) & UB-Sanitizer
cmake --preset rv64-debug
cmake --build --preset rv64-debug -j$(nproc)
```

### Formatting Verification

Code formatting strictly follows Google C++ style with a 100-column margin. Run dry-run checks before committing:

```bash
clang-format --dry-run --Werror $(find include src tests -name "*.cpp" -o -name "*.hpp")
```

---

## 3. C++23 Architectural & Coding Idioms

1. **Compile-Time Fixed XLEN**:
   - Architecture bitwidth is compile-time fixed via `SIMRV_XLEN` (32 or 64). There is no runtime XLEN switching.
2. **Domain-Specific Type Aliases**:
   - Avoid generic primitive types (`uint64_t`, `uint32_t`, `int`) when domain aliases exist:
     - `Word`, `Address`, `PhysAddr`, `VirtAddr`, `RegId`, `CSRValue`, `TrapCause`.
   - Proactively declare strongly typed aliases for new architectural concepts.
3. **Logging & Tracing Standards**:
   - Use `simrv::log::info`, `simrv::log::warn`, and `simrv::log::error` from `simrv/core/Logger.hpp` for console and TUI messages.
   - **Never** write raw `std::cout`, `std::cerr`, or `printf` calls inside core simulation logic.
   - Use `simrv::core::Tracer` for architectural simulation artifacts in `trace/` (`trace.txt`, `traplog.txt`, `bpred.txt`, `instmix.txt`).
4. **Physical Memory Protection (PMP)**:
   - PMP access permissions are centralized in `simrv::core::pmp::check_access` and evaluated across all memory requests: instruction fetch, load/store execution, and hardware page table walks.
   - Any modification to PMP CSRs must call `cpu_.state().refresh_pmp_status()` and flush translation buffers (`cpu_.TLB_flush()`).

---

## 4. Branching & Commit Guidelines

SimRV development follows strict branch hygiene defined in `.agents/rules/branching.md`:

### Feature & Topic Branches

- **Naming**: Use lowercase kebab-case naming:
  - `feature/<name>` for new features or subsystems (e.g. `feature/dynamic-vlen`)
  - `fix/<name>` for bug fixes and regression remediation
  - `perf/<name>` for targeted microarchitectural optimizations
  - `refactor/<name>` for structural refactoring without behavioral divergence
- **Base Branch**: Always base feature development on the latest `dev` branch.
- **Pull Requests**: Target PRs at `dev`. Rebase onto `dev` to keep linear commit history.

### Release Qualification Branches

- **Naming**: Strictly follow `release/<semver>` (e.g., `release/3.0.0-alpha.4`).
- **Release Metadata Bumps**: Version bumps across `CMakeLists.txt`, `release/release-manifest.json`, `CITATION.cff`, `CHANGELOG.md`, and `TODO.md` must be committed directly to the release branch with message:
  `chore(release): bump version to <version> and update release metadata`
- **Delivery Vehicle**: Use the PR targeting `dev` as the delivery tracking vehicle. **Do not create Git tags or publish GitHub releases** until all CI matrix jobs are green and merged.

### Commit Message Format

Follow the conventional commit format: `<type>(<scope>): <summary>`

- **Types**: `feat`, `fix`, `perf`, `refactor`, `test`, `docs`, `chore`
- **Scopes**: `pipeline`, `core`, `memory`, `tui`, `device`, `smp`, `release`, `bpred`
- Keep summary lines under 72 characters, written in the imperative mood (e.g., `feat(pipeline): implement scoreboard`).

---

## 5. Adding Regression Tests

Any architectural, pipeline, or device behavioral changes **must be validated against both 64-bit and 32-bit targets**.

### Adding a Test Suite in `CMakeLists.txt`

Use the `simrv_add_runtime_test` helper macro rather than manually defining test targets:

```cmake
simrv_add_runtime_test(
  simrv-myfeature-tests
  tests/MyFeatureTests.cpp
  my-feature-test
  "gate;regress;pipeline"
)
```

### Running Validation Gates

Before submitting a pull request, run the dual-architecture validation gates:

```bash
# RV64 gate
ctest --test-dir build/rv64-release --output-on-failure -L gate

# RV32 gate
ctest --test-dir build/rv32-release --output-on-failure -L gate

# Release metadata conformance
python3 scripts/release_check.py --binary build/rv64-release/SimRV
```
