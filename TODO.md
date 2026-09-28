# SimRV 3.0 beta.2 handoff

Branch: `release/3.0.0-beta.2`
Target: `dev`
Latest functional commit: `2ffe1a1 feat(release): package native Linux distributions`

Do not create a tag or GitHub release. Keep the PR as the delivery vehicle until all
required qualification checks are green.

Immediate continuation tasks:

1. Confirm the pushed `dev` CI matrix is green and push this release branch for qualification.
2. Rebuild RV32/RV64 RPM and DEB artifacts with beta.2 metadata and inspect package dependencies.
3. Install the base and development packages on clean Fedora and Debian/Ubuntu hosts; compile the
   SDK consumer from the installed CMake package.
4. Merge the qualified release branch into `dev`, then create and push `v3.0.0-beta.2` so GitHub
   Releases publishes the native package matrix.
5. Resume the lab `riscv-buildchain` project: finish the Newlib/musl toolchain validation, exercise
   CFU-Proving-Ground and RVComp through their existing Makefile interfaces, then add Buildroot.

Local context intended to reduce repeated discovery:

- Native CPack components are `Runtime`, `Tools`, and `Benchmark`; source installs retain the SDK.
- Fedora release jobs build RPMs; the `gcc:16` Debian-based job builds DEBs to avoid glibc skew.
- `simrv` intentionally excludes benchmark tooling; it is supplied by `simrv-benchmark`.
- SimRV official builds use DRAM base `0x80000000` and a 256 MiB configured default, while
  `--ram-size` remains runtime-configurable.

---

# SimRV 3.0.0 Release Roadmap

This document outlines the strategic implementation roadmap, feature deliverables,
performance milestones, and architectural cleanup tasks leading up to the SimRV 3.0.0
General Availability (GA) release.

---

## Milestone 1: 3.0.0-beta.1 — Performance Acceleration & TUI Hardening

**Target Window**: October 2026
**Primary Goals**: Eliminate remaining hot-path execution bottlenecks, complete branch predictor optimizations, and polish interactive TUI 2D navigation.

### 1. Performance Optimizations

- [x] **Precomputed Fast-Memory Access Classes**:
  - Classify memory accesses directly in `CachedOp` at decode time (direct-RAM, MMIO/tohost, alignment, translation guards).
  - Fast-track direct host-pointer access in integer/FP load and store paths without multi-branch qualification checks per instruction.
- [x] **Decode Cache Footprint & Dispatch Optimization**:
  - Optimize memory layout of `CachedOp` to reduce per-entry footprint across L1/L2 host cache hierarchies.
  - Streamline the dense opcode dispatch table across fast-path and cycle-accurate fetch/decompress stages.
- [x] **TUI Differential Rendering Throttling**:
  - Throttle updates for non-visible or off-screen inspector sub-views during continuous execution.
  - Minimize ANSI terminal escape sequence generation during high-speed stepping or headless execution bursts.

### 2. TUI & Interactive Features

- [x] **Live Memory Contents Inspection & Dual-Mode Watch**:
  - Display actual memory contents (formatted hex word, raw bytes, ASCII character box, and resolved symbol annotations) in the Memory/Stack inspector.
  - Support dual-mode memory inspection via Address modal (custom 64-bit/32-bit address navigation with live stack pointer fallback on `sp` / `0`).
- [x] **2D & Horizontal Viewport Scrolling Revision**:
  - Unify 2D scrolling behavior in `framework::ScrollView` across all inspector tabs (Pipeline, Cache, Disassembly, Memory).
  - Fix clipping and horizontal column alignment issues when rendering wide disassembly and multi-hart register matrices.
  - Provide explicit scroll indicators (`▲`, `▼`, `◀`, `▶`) when content overflows pane boundaries.

### 3. Branch & Typing Integration

- [x] **Merge `perf/strong-types-and-bpred-opt` into `dev`**:
  - Land strongly-typed `PhysAddr`, `VirtAddr`, and `SoftTlbEntry` interfaces across pipeline and memory modules.
  - Finalize flat saturating counter array and branchless statistics updates in `BranchPredictor`.

---

## Milestone 2: 3.0.0-rc.1 — Feature Freeze, Linux SMP Certification & Code Modernization

**Target Window**: November 2026
**Primary Goals**: Freeze architectural interfaces, certify Linux multi-hart SMP operation, and enforce clean modern C++23 standards.

### 1. Interface & Feature Freeze

- [ ] **CLI & Configuration Stability**:
  - Lock all CLI options, execution profiles (`--mode fast|detailed|cycle-accurate`), and pipeline targets (`3stage`, `5stage`, `dual-issue`).
  - Freeze CPU model and machine configuration JSON schemas (`release/schemas/`).

### 2. Linux Multi-Hart SMP Certification

- [ ] **Guest Secondary Hart Utilization**:
  - Certify that secondary harts (`hart 1..N`) are actively scheduled and utilized by guest Linux.
  - Verify `/proc/cpuinfo` hart enumeration, OpenSBI HSM wait/start loops, and TileLink IPI delivery.
  - Implement guest-level affinity verification tests (`taskset`) within the PTY regression harness (`scripts/test_linux_pty.py`).

### 3. Domain Strong Typing Audit

- [ ] **Complete Strong Typing Across Subsystems**:
  - Introduce and enforce dedicated domain types for CSR addresses (`CSRAddress`), privilege modes (`PrivilegeMode`), and memory transfer access sizes.
  - Replace remaining primitive integer types (`uint64_t`, `uint32_t`, `unsigned long`) in core execution logic with explicit domain aliases (`Word`, `Address`, `Register`, `TrapCause`).

### 4. Legacy 2.x Deprecation & Architecture Hygiene

- [ ] **Purge 2.x Remnants**:
  - Audit codebase for obsolete comments, dead configurations, and removed rollback/reverse-stepping hooks.
  - Modernize buffer interfaces to use `std::span` rather than raw pointer and length pairs.
  - Verify strict adherence to architecture layer boundaries (`include/simrv/` public interfaces vs `src/` private implementations).

---

## Milestone 3: 3.0.0 GA — Qualification, Documentation & General Availability

**Target Window**: December 2026
**Primary Goals**: Full multi-compiler release qualification, updated documentation, and official delivery PR qualification.

### 1. Documentation & Visual Assets

- [ ] **Architecture & Mission Documentation**:
  - Update `README.md` and `docs/` architecture overviews with 3.0 multi-hart and TileLink-C cache coherence topologies.
  - Update classroom mission files and student guidance manuals to match 3.0 TUI layouts and inspector controls.

### 2. Multi-Compiler & Matrix Qualification

- [ ] **Full Release Qualification Matrix**:
  - Clean builds and 100% gate pass across:
    - RV64 / RV32 release presets (`rv64-release`, `rv32-release`).
    - Clang 22+ and GCC 16+ native release presets.
    - ThreadSanitizer (`tsan`), AddressSanitizer (`asan`), and UndefinedBehaviorSanitizer (`ubsan`).
    - Full Linux boot and PTY lifecycle regression suites (`linux-boot-pty`, `linux-ia-quantum-smp-pty`).
- [ ] **Release Manifest Finalization**:
  - Update `release/release-manifest.json`, `CITATION.cff`, and write final `CHANGELOG.md` entry for `[v3.0.0]`.
  - Validate release artifacts with `scripts/release_check.py`.
