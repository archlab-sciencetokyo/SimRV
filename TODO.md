# SimRV 3.0.0 Release Roadmap

This document outlines the strategic implementation roadmap, feature deliverables,
performance milestones, and architectural cleanup tasks leading up to the SimRV 3.0.0
General Availability (GA) release.

---

## Milestone 1: 3.0.0-beta.2 — Performance Acceleration & TUI Hardening

**Target Window**: October 2026
**Primary Goals**: Eliminate remaining hot-path execution bottlenecks, complete branch predictor optimizations, and polish interactive TUI 2D navigation.

### 1. Performance Optimizations

- [x] **Precomputed Fast-Memory Access Classes**:
  - Classify memory accesses directly in `CachedOp` at decode time (direct-RAM, MMIO/tohost, alignment, translation guards).
  - Fast-track direct host-pointer access in integer/FP load and store paths without multi-branch qualification checks per instruction.
- [ ] **Decode Cache Footprint & Dispatch Optimization**:
  - Optimize memory layout of `CachedOp` to reduce per-entry footprint across L1/L2 host cache hierarchies.
  - Streamline the dense opcode dispatch table across fast-path and cycle-accurate fetch/decompress stages.
- [ ] **TUI Differential Rendering Throttling**:
  - Throttle updates for non-visible or off-screen inspector sub-views during continuous execution.
  - Minimize ANSI terminal escape sequence generation during high-speed stepping or headless execution bursts.

### 2. TUI & Interactive Features

- [x] **Live Memory Contents Inspection & Dual-Mode Watch**:
  - Display actual memory contents (formatted hex word, raw bytes, ASCII character box, and resolved symbol annotations) in the Memory/Stack inspector.
  - Support dual-mode memory inspection via Address modal (custom 64-bit/32-bit address navigation with live stack pointer fallback on `sp` / `0`).
- [ ] **2D & Horizontal Viewport Scrolling Revision**:
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
