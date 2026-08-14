# SimRV Changelog

All notable changes to SimRV are documented here.
Versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased] — v2.0.0 release hardening

### Breaking CLI cleanup

SimRV 2.0 removes ambiguous and deprecated aliases. Removed options fail with an explicit
replacement instead of silently changing behavior.

| Removed | Replacement |
|---|---|
| `-k`, `-i`, `--kernel` | `-m`, `--image` |
| `--dtb` | `-f`, `--fdt` |
| `-a`, `--app` | `-b`, `--baremetal` |
| `-o`, `--linux` | `--os` |
| `--headless`, `--no-tui` | `-c`, `--cli` |
| `--high-accuracy`, `--accuracy-mode` | `-C`, `--cycle-accurate` |
| `--perf-mode` | `--high-performance`, `--ia` |
| `--vector-len` | `--vlen` |
| `--mouse-speed` | `--mouse-sensitivity` |
| `--contrast` | `--high-contrast` |
| `--disable-forwarding` | `--no-forwarding` |
| `-B`, `--opensbi` | Remove it; OpenSBI is automatic with `--fdt` |

The conflicting `-G` alias is now GUI-only; use `--gdb` for the GDB server. The conflicting `-c`
alias is now CLI-only; use `-f` or `--fdt` for a device tree.

### TUI framework hardening

- Split byte-routing policy from terminal I/O so focused guest input, modal input, and paused
  navigation have deterministic behavior and native test coverage.
- Added native tests for Enter routing, ANSI/UTF-8 parsing, scrollback, selection, resize/reset,
  themes, and keybinding registry integrity.
- Invalid TUI keybinding actions now report an error instead of silently resolving to Step.

### Machine, interrupt, and memory correctness

- Reset now clears PLIC, CLINT, pipeline, timer-target, and interrupt-controller state instead of
  carrying device state across a reboot.
- CLINT timer writes generate machine timer interrupts; directly emulated SBI timers generate
  supervisor timer interrupts without asserting both causes simultaneously.
- PLIC claims now honor context thresholds.
- MMIO registration rejects empty, wrapping, overlapping, and containing ranges; transactions that
  cross a device boundary or use unsupported opcodes return bus errors.
- Unaligned guest RAM accesses no longer rely on undefined host pointer casts, and framebuffer
  accesses are checked across their complete width.
- Bare-metal fast batches honor instruction limits exactly and stop promptly after a halt request.

## [v2.0.0-rc.8] — 2026-08-08

Release candidate 8 for v2.0.0. Focuses on CMake user presets modularization, scrubbing hardcoded workspace paths, floating-point rounding precision under Clang, dual-architecture `riscv-tests` integration, and repository documentation polish.

### Build System & Developer Presets
- **Preset Modularization**:
  - Reverted `CMakePresets.json` to general, portable presets without hardcoded compiler binaries.
  - Added local-only `CMakeUserPresets.json` (gitignored) for developer-specific Clang/GCC configuration (`CMAKE_C_COMPILER` and `CMAKE_CXX_COMPILER`).
- **Floating-Point Rounding & Exception Semantics**:
  - Added `-frounding-math` compiler flag check to preserve floating-point rounding mode semantics (`std::fesetround`) and exception raising under Clang `-O3` / ThinLTO optimization passes.

### Test Automation & ISA Verification
- **Dual-Architecture `riscv-tests` Integration**:
  - Built 64-bit (`make`) and 32-bit (`make XLEN=32`) `riscv-tests` test suites in `../../tests/riscv-tests`.
  - Achieved 100% CTest gate pass rate (230 test cases) across both `rv64-release` and `rv32-release` targets.

### Repository Polish & Cleanups
- **Hardcoded Path Scrubbing**:
  - Replaced absolute `/home/archlab/` paths in `scripts/run_benchmarks.py` with dynamic `TESTS_DIR` path resolution relative to `script_dir`.
  - Fixed hardcoded absolute file link to `LICENSE` in `README.md`.
- **Legacy File Cleanups & Script Updates**:
  - Updated `scripts/build-linux-image.sh` to target latest OpenSBI v1.9, Linux Kernel v7.1.7, and BusyBox v1.38.0.
  - Added `--libc (musl|glibc)` and `--cross-compile` flags with auto-detection for both `musl` (`riscv64-unknown-linux-musl-`) and `glibc` (`riscv64-unknown-linux-gnu-`) toolchains.
  - Removed obsolete `help.txt` and `Makefrag` files from repository root.
  - Updated documentation version headers (`ARCHITECTURE.md`).

## [v2.0.0-rc.6] — 2026-08-05

Release candidate 6 for v2.0.0. Focuses on atomic state synchronization, $O(1)$ TLB generation epoch flushes, selective hardware/soft TLB invalidation, deterministic CLINT timer integration, devicetree syscon-poweroff standard bindings, and post-shutdown execution retention.

### Performance & Cache / TLB Optimizations
- **$O(1)$ Soft TLB Generation Epoch Flushing**:
  - Replaced $O(N)$ 4096-entry memory loops during `soft_tlb_flush()` with a single-instruction `++soft_tlb_epoch` generation increment.
- **Selective Hardware & Soft TLB Invalidation**:
  - Implemented page-level selective invalidation in `Tlb::flush_selective` and `soft_tlb_flush_selective`, ensuring `SFENCE.VMA vaddr` invalidates only target page entries rather than wiping the entire 2048-entry TLB.
- **Cache & TLB Struct Compaction**:
  - Aligned `CacheLine` to 64 bytes (`alignas(64)`) to match host L1 CPU cache line boundaries.
  - Aligned `SoftTlbEntry` and `TLBEntry` to 32 bytes (`alignas(32)`), enabling power-of-two bit-shift indexing (`shl rax, 5`).
  - Replaced bounds-checked `.at()` array accesses with direct subscript indexing across `BaseCache`, `ICache`, and `DCache`.

### State Atomization & Synchronization
- **Lock-Free Execution State Machine**:
  - Atomized `ExecutionState` and cross-thread shared state (`tohost`, `mtime`, `mtimecmp`, `e_icount`) with `std::atomic<T>`.
  - Eliminates data races and torn 32-bit reads across simulation, TUI rendering, and GDB control threads.

### Devices & Devicetree Standard Compliance
- **Deterministic CLINT MMIO Time Advancement**:
  - Derived simulated clock time strictly from `clint_mmio.mtime`, guaranteeing deterministic cycle progress and freezing time advancement during simulation pause.
- **Standard Devicetree Syscon Poweroff & Reboot Bindings**:
  - Added `regmap = <&test>;` links to `poweroff` and `reboot` nodes in `virt-rv64.dts` and `virt-rv32.dts`.
  - Recompiled `linux-images/rv64/devicetree.dtb` and `linux-images/rv32/devicetree.dtb` for native OpenSBI `sifive_test` / `syscon-poweroff` reset driver parsing.

### TUI & System Lifecycle UX
- **Post-Shutdown Execution Safety & Window Retention**:
  - Halting or shutting down the guest system (`poweroff` / `halt`) pauses execution and renders `[SHUTDOWN]` badge while keeping the TUI window open for full inspection of registers, memory, stats, and logs.
  - Prohibits stepping or unpausing from a shut-down state, presenting a clear guidance modal (`"SYSTEM SHUTDOWN - Please reboot [Ctrl-R], load [o], or quit [q]"`).
- **Simulator Reload on Guest Reboot**:
  - Updated `request_reboot()` and `[Ctrl-R]` keybinding to signal simulation loop exit, cleanly re-instantiating the simulator with preserved settings.

---

## [v2.0.0-rc.3] — 2026-07-31

Release candidate 3 for v2.0.0. Focuses on TUI keybinding centralization, Notice Modals UX enhancement, automatic reboot on post-shutdown resume, and licensing compliance.

### TUI & Visualizers
- **Centralized TUI Keybindings Registry (`TuiKeybindings`)**:
  - Centralized all key action mappings, footer labels, and online help definitions in `TuiKeybindings.hpp` / `TuiKeybindings.cpp`.
  - Re-assigned intuitive, semantic hotkeys: `[n]` for Next step, `[b]` for Backstep, `[m]` for Manage Break/Watchpoints, `[i]` for Inspect Memory, `[w]` for Set Watchpoint, and `[:]`/`[k]` for PC Breakpoint.
  - Isolated comma (`[,]`) strictly to opening the Simulator Settings modal.
- **Notice Modals & Modal Collision Safety**:
  - Moved status notices, warnings, and settings confirmations to centered Notice Modals.
  - Formatted multi-line text across Breakpoint and Watchpoint creation modals to prevent text clipping.
  - Re-mapped Manage Breakpoints modal to `[a]`, leaving `[b]` dedicated strictly to reverse step execution (`perform_backstep`).
- **Post-Shutdown Automatic System Reload**:
  - Resuming or single-stepping after guest system shutdown (`machine_.is_shutdown_ == true`) now triggers `machine_.request_reboot()`, reloading the guest binary image cleanly instead of hanging.
  - Re-enabled execution on `perform_backstep()` when stepping backward out of shutdown state.

### License & Documentation
- **MIT License & Third-Party Notices**:
  - Added repository `LICENSE` file under the MIT License (Copyright (c) 2024-2026 ArchLab @ ScienceTokyo).
  - Included third-party software notices for TinySoundFont (`tsf.h`, MIT License) and TinyMidiLoader (`tml.h`, zlib License).
  - Updated `README.md` with a dedicated License section documenting core and third-party licenses.

---

## [v2.0.0-rc.2] — 2026-07-31

Release candidate 2 for v2.0.0. Focuses on TUI UX refinements, pipeline execution timeline correctness, hardware Sixel capability detection, and compiler prerequisite updates.

### TUI & Visualizers
- **Pipeline Execution Timeline Overhaul**:
  - Assigned a unique 64-bit dynamic instruction sequence ID (`inst_id`) to stage registers and cycle snapshots.
  - Fixed a stage merging bug where instructions in a loop (sharing the same static PC) merged across iterations, producing repeating stage artifacts like `WB ID EX MEM WB ID EX MEM WB`.
  - Dynamic instruction instances now render as distinct rows in chronological order, producing clean textbook pipeline execution diagrams (`IF → ID → EX → MEM → WB`).
- **Arrows-Only Cache Inspector Navigation**:
  - Simplified Cache Inspector navigation: Up/Down arrow keys (`[↑/↓]`) cycle Cache Ways (`0-3`), and Left/Right arrow keys (`[←/→]`) cycle Cache Sets (`0-15`).
  - Removed redundant keybindings (`w`/`W`, `j`/`k`, number keys `0-3`) from the Cache page, leaving `[w]` dedicated strictly to the Set Watchpoint dialog.
  - Arrow keys automatically pass through to the guest virtual terminal / UART when the simulator is running.
- **High-Legibility Bold Key Badges & Styling**:
  - Key shortcut brackets `[key]` across status bars, help modals, and inspector hints are now formatted in distinct ANSI bold (`\033[1m`) paired with vibrant theme accent colors (`kThemeSky` cyan).
- **Automatic Sixel Terminal Query & ANSI Fallback**:
  - Added Primary Device Attributes (DA1 - `\033[c`) query and environment checks (`TERM`, `TERM_PROGRAM`) to detect Sixel graphics support.
  - Automatically falls back to clean standard ANSI text and diff cell rendering on unsupported terminals without producing escape artifacts.
- **Cache Statistics Persistence**:
  - Preserved cumulative cache hit, miss, and replacement statistics across `FENCE.I` cache line flushes (`BaseCache::flush()`).
  - Performance counters now accumulate accurately throughout guest execution and only reset on explicit machine reset/reboot.

### Build & Documentation
- **Compiler Prerequisites**: Updated minimum compiler requirements in `README.md` to Clang 20+ and GCC 14+ for complete C++23 standard library compatibility.
- **Release Assets Note**: Added guidance in `README.md` noting that GitHub release packed binaries contain standalone executables without supplementary test scripts or images.

---

## [v2.0.0-rc.1] — 2026-07-30

Release candidate for v2.0.0. All major features are complete; this cycle focuses
on inspector polish, correctness fixes, and CLI normalization.

### TUI Inspectors
- **Cache inspector**: per-way hit/miss markers (`◄ HIT` / `◄ MISS ▸ REPLACED`)
  now correctly annotate only the exact hit way using `last_hit_way` tracking;
  `[Cache:IC]` / `[Cache:DC]` tab click now correctly toggles IC ↔ DC (duplicate
  `esc_buf_` handler that swallowed the toggle was fixed)
- **MISA + VLEN modal**: added VLEN setting (32–1024 bits, power of 2, default 256)
  to the MISA configuration modal; displayed on row 8; applied to `s_vlen` at reboot
- **MMIO/Bus inspector**: live VirtIO status flags, IRQ state, Virtqueue 0 ring
  physical addresses (Desc / Avail / Used), UART NS16550A settings
- **Hazard inspector**: aligned stage labels (`IF `, `ID `, `EX `, `WB `) for
  uniform column layout
- **TLB, BP, Bus pages**: clamped to 36–46 visible characters per row; removed
  overflow that was clipping text on narrow terminals

### CLI
- `--vlen <N>` / `--vector-len <N>`: VLEN can now be set from the command line;
  the non-standard `-VLEN` alias has been replaced with `--vector-len`
- Debug mode (`-d`) no longer implicitly enables branch prediction trace output;
  use `--trace-bpred` explicitly

### Bug Fixes
- Stack inspector clicks no longer pollute the Explainer target PC
  (`explain_pc_` is now separate from `inspect_addr_`)

---

## [v2.0.0-beta.36] — 2026-07-30

### TUI
- Cache section headers and `[Cache:IC]` / `[Cache:DC]` tab entries toggle between
  ICache and DCache inspector views
- Regs tab click cycles GPR → FPR → VEC → GPR

---

## [v2.0.0-beta.34] — 2026-07-29

### TUI
- Machine settings (cycle-accurate mode, debug mode, MISA profile, theme) persist
  across simulator reloads and binary hot-swaps

---

## [v2.0.0-beta.33] — 2026-07-29

### TUI — Cache Inspector
- Individual Way cursor navigation inside cache sets
- Full 32-byte hex + ASCII cache line data inspection for the selected way

---

## [v2.0.0-beta.32] — 2026-07-28

### TUI — Cache Inspector
- Interactive cache Set inspector with set selection (`j`/`k`), way selection
  (`0`–`3`, `w`), and a live set occupancy map
- Replacement tracking: last-evicted tag, last-replaced set/way displayed
- Collision-free keybindings for cache navigation

---

## [v2.0.0-beta.31] — 2026-07-27

### Performance & TUI
- Optimized rendering throughput in high-speed simulation
- Cache inspector tab hidden in high-performance (IA) mode
- Debug keybindings hidden from status bar footer in normal mode

---

## [v2.0.0-beta.30] — 2026-07-26

### TUI
- **MISA CSR modal** (`Alt-M`): configure ISA extensions (A/B/C/D/F/M/V/S/U) and
  XLEN mode interactively with draft preview and quick presets (Base / IMAC / GC)
- Settings modal options are mode-aware (CA vs IA)

---

## [v2.0.0-beta.27] — 2026-07-25

### ISA — RVV
- Fixed several RVV vector memory and permute bugs

### TUI
- Interactive binary loading modal: browse and reload `.bin` images at runtime
- Disambiguated conflicting key bindings

---

## [v2.0.0-beta.26] — 2026-07-24

### TUI — Pipeline Inspector
- Reworked pipeline page layout for improved student readability
- Cleaner stage-slot display with stall/bubble indicators

---

## [v2.0.0-beta.25] — 2026-07-23

### TUI — Education Tools
- Overhauled pipeline visualizer with colour-coded in-flight instruction slots
- Modularized `TuiModal` into separate per-modal handler files

---

## [v2.0.0-beta.24] — 2026-07-22

### TUI
- Simulation speed configurable by target frequency (Hz) via `[f]` key

---

## [v2.0.0-beta.22] — 2026-07-21

### TUI
- Grouped register tabs (GPR / FPR / VEC) under a single `Regs` tab entry
- `[l]` / `Alt-L` cycles through tool inspector tabs
- Cache page column alignment fixed across all themes

---

## [v2.0.0-beta.19] — 2026-07-20

### TUI — Log & Trace
- Execution log and instruction trace views integrated into the LeftPane tab system
- Refactored pane class hierarchy to support pluggable inspector panels

---

## [v2.0.0-beta.17] — 2026-07-18

### TUI — Educational Visualizers
- Guest stack inspector with symbol-resolved frame layout
- Cache heatmap with set occupancy heat levels
- Data forwarding path diagram with active forwarding highlight

---

## [v2.0.0-beta.15] — 2026-07-17

### TUI — Instruction Explainer
- Complete FP (F/D) explanations: rounding modes, NaN semantics, exception flags
- Complete RVV (V) explanations: LMUL, SEW, VLEN, element group layout
- Fixed vector register multi-word display formatting for VLEN > 64

---

## [v2.0.0-beta.10] — 2026-07-10

### Cycle-Accurate Core
- Pipeline data hazard analysis (RAW / WAW / WAR) in the instruction explainer
- Control hazard detection with branch misprediction penalty annotation

---

## [v2.0.0-beta.7] — 2026-07-07

### Architecture
- High-performance (IA) vs cycle-accurate (CA) simulation modes selectable at runtime
- Modularized CLI argument parsing into logical groups
- ISA test suite consolidated under the standard baremetal `appmode` runner

---

## [v2.0.0-beta.1] — 2026-07-01

### Foundation
- XLEN abstraction layer: unified RV32/RV64 register and CSR handling
- Interactive TUI split-screen monitor with mouse and keyboard support
- OpenSBI boot support for supervisor-mode Linux images
- VirtIO console and disk block device models
- MMU with TLB and page table walker (Sv39 / Sv32)

---

## [v2.0.0-alpha.4] — 2026-06-15

- Performance benchmarks and initial release asset packaging

## [v2.0.0-alpha.3] — 2026-06-14

- Initial public alpha: CMake preset infrastructure, Clang-20 CI, base RISC-V pipeline

[v2.0.0-rc.8]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-rc.8
[v2.0.0-rc.6]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-rc.6
[v2.0.0-rc.3]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-rc.3
[v2.0.0-rc.2]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-rc.2
[v2.0.0-rc.1]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-rc.1
[v2.0.0-beta.36]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.36
[v2.0.0-beta.34]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.34
[v2.0.0-beta.33]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.33
[v2.0.0-beta.32]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.32
[v2.0.0-beta.31]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.31
[v2.0.0-beta.30]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.30
[v2.0.0-beta.27]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.27
[v2.0.0-beta.26]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.26
[v2.0.0-beta.25]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.25
[v2.0.0-beta.24]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.24
[v2.0.0-beta.22]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.22
[v2.0.0-beta.19]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.19
[v2.0.0-beta.17]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.17
[v2.0.0-beta.15]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.15
[v2.0.0-beta.10]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.10
[v2.0.0-beta.7]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.7
[v2.0.0-beta.1]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-beta.1
[v2.0.0-alpha.4]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-alpha.4
[v2.0.0-alpha.3]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-alpha.3
