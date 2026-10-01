# SimRV Changelog

All notable changes to SimRV are documented here.
Versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [v2.0.4] — 2026-10-01

Maintenance release ensuring automatic trace directory creation so simulation trace and log files are reliably generated even if the target directory does not exist.

### Tracing & Diagnostics

- **Automatic Directory Creation**: Added `Tracer::ensure_trace_directory` to automatically create missing trace directories prior to emitting simulation and architectural traces (`trace/trace.txt`, `trace/tracepc.txt`, `trace/bpred.txt`, `trace/instmix.txt`, trap logs, and initialization memory/register/disk dumps), preventing silent file open failures when no `trace/` folder exists.

## [v2.0.3] — 2026-09-29

Maintenance release adding fully static, baseline x86-64 musl portable binaries for broad Linux distribution compatibility, including Ubuntu 22.04 and newer.

### Packaging & Portability

- **Static musl Release Binaries**: Added checksum-verified GCC 15 musl release builds for RV32 and RV64 host binaries.
- **Broad Distribution Compatibility**: Configured portable builds to avoid host-specific x86-64-v3 instructions, requiring no host glibc or libstdc++ runtime.

## [v2.0.2] — 2026-08-28

Maintenance release ensuring side-effect-free instruction explanation in the TUI left pane to prevent spurious ICache hits during tool tab navigation.

### Bug Fixes & Stability

- **Side-Effect-Free Instruction Explainer**: Refactored `LeftPaneExplain::get_explain_rows()` to use an isolated, side-effect-free direct memory decode path instead of executing the core CPU `fetch_stage()` coroutine, eliminating spurious ICache hit counter increments and pipeline context mutations during TUI page cycling.

## [v2.0.1] — 2026-08-28

Maintenance release addressing cache hit/miss accounting accuracy and TUI cache visual inspector state synchronization.

### Microarchitecture & Cache

- **Cache Refill Accounting**: Fixed an issue where refill reads immediately following miss insertions generated spurious hit events and corrupted hit rate statistics.
- **TUI Cache Inspector Highlights**: Corrected hit vs. eviction highlight priority in the Left Pane Cache view to prevent stale replacement markers from masking current hit indications.
- **Base Cache State**: Ensured `BaseCache::insert` marks the current access as a compulsory/conflict miss state rather than inheriting stale hit indicators.

## [v2.0.0] — 2026-08-19

General Availability release of SimRV 2.0: A dual-width explainable RISC-V full-system simulator written in modern ISO C++23, providing high-throughput functional simulation, in-terminal visual inspection, and multi-OS/RTOS execution.

### Major Capabilities

- **Dual-Width Parametric Engine**: Compile-time parameterization (`SIMRV_XLEN`) for RV32GCBV and RV64GCBV with zero runtime virtualization overhead.
- **High-Throughput Execution**: Optimized fast-path functional engine delivering 195+ MIPS on CoreMark with an overall 1.66x geometric mean speedup over Spike across 20-run statistical benchmark evaluation.
- **Microarchitectural Explainability**: 5-stage structural pipeline modeling with dynamic inter-stage RAW/WAR/WAW hazard attribution, forwarding analysis, and natural-language causal event synthesis.
- **In-Terminal Visual Inspection**: Split-screen TUI powered by an internal ANSI/VT100 Virtual Terminal and hardware Sixel graphics protocol, streaming guest graphical framebuffers over headless SSH text sessions with zero host GUI dependencies.
- **Full-Stack OS & RTOS Platform**: Direct execution of upstream RISC-V Linux kernels (v6.x/v7.x), μT-Kernel 3.0, and embedded RTOS payloads over standard MMIO peripherals (VirtIO Block, NS16550 UART, CLINT, PLIC, RTC, and TileLink crossbar).
- **Correctness & Research Reproducibility**: 100% CTest gate pass rate across RV32 (274/274) and RV64 (368/368), real-time differential Spike lockstep verification (`SpikeLockstep`), and ACM/IEEE open-science reproducibility tooling.

## [v2.0.0-rc.10] — 2026-08-19

Release candidate 10 hardens release engineering workflows, adds strict required-suite schemas, streamlines dependencies by removing host SDL bridges while preserving simulated MMIO devices, and fixes headless execution across the vector test suite.

### Release Engineering & Reproducibility

- **Release & Evidence Schemas**: Added versioned release, evidence, and experiment schemas with strict required-suite coverage.
- **Reproducibility Tooling**: Added portable dependency inputs, machine-readable evidence, deterministic aggregation and plotting, and reproducibility archive generation tooling.
- **Evidence-Only Performance Policy**: Changed performance qualification to an evidence-only policy while retaining explicit FP/RVV gaps.
- **Documentation & Research Interfaces**: Added citation metadata, a host support matrix, research-companion documentation, and academic support/security boundaries.
- **Linux PTY Shutdown Validation**: Hardened Linux PTY shutdown validation against the expected terminal-close race.
- **Headless Vector Testing**: Fixed vector test runner to execute in headless `--cli` mode, achieving 100% pass across all 1,067 vector tests.
- **Host Dependency Streamlining**: Removed experimental SDL3 host audio/display bridges and third-party soundfont headers (`tsf.h`, `tml.h`) while preserving simulated MMIO device models (`Audio`, `Framebuffer`, `InputDevice`).

## [v2.0.0-rc.9] — 2026-08-14

Release candidate 9 focuses on architectural compliance, trap and interrupt correctness, OS lifecycle control, MMIO safety, and TUI/UART stability ahead of v2.0.0.

### CLI Modernization

SimRV 2.0 standardizes command-line options. Deprecated aliases fail with explicit replacements:

| Removed Option | Modern Replacement |
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
| `-B`, `--opensbi` | Automatic with `--fdt` |

The conflicting `-G` alias is GUI-only (`--gdb` for GDB server); `-c` is CLI-only (`-f`/`--fdt` for device tree).

### Terminal UI & Visual Inspection

- **Display Cell Accounting**: Corrected Unicode display-cell accounting for wide and combining characters, centralized frame/modal resize geometry, and ensured constrained modal borders remain closed with clipping indicators.
- **Keybinding Registry Help**: Generated footer labels and online action help dynamically from the canonical keybinding registry with exact-width two-column help rows and resize-stable mouse hit-testing.
- **Frame Composition**: Extracted full-frame composition into a pure tested renderer; standardized breakpoint controls (`[:]` sets breakpoint, `[k]` toggles at current PC).
- **Global Control Keys**: Maintained `Ctrl-R` reboot and `Ctrl-Q` quit availability globally across modals and post-shutdown states.
- **Opt-In Guidance Strip**: Made the educational guidance pane opt-in with `[g]`, automatically suppressing it during active execution or on compact terminal viewports.
- **Input Routing & Terminal Isolation**: Separated byte-routing policy from terminal I/O so guest input, modal forms, and paused inspection operate deterministically.
- **TUI Test Coverage**: Added native regression coverage for Enter routing, ANSI/UTF-8 parsing, scrollback buffers, text selection, terminal resize, themes, and keybinding registry integrity.

### Core Architecture, Interrupts & Memory

- **State Reset Cleanups**: Ensured simulator reset clears PLIC, CLINT, pipeline, timer-target, and interrupt-controller state without carrying stale device state across reboots.
- **CLINT & SBI Timer Isolation**: Directed CLINT timer writes to generate machine timer interrupts and emulated SBI timers to generate supervisor timer interrupts without asserting both simultaneously.
- **PLIC Context Thresholds**: Enforced context priority threshold checking on all PLIC interrupt claims.
- **MMIO Range Safety**: Enforced strict validation rejecting empty, wrapping, overlapping, and containing MMIO ranges; transactions crossing device boundaries or using unsupported opcodes return bus errors.
- **Safe Memory Alignment**: Eliminated undefined host pointer casts on unaligned guest RAM accesses and validated framebuffer access across full width bounds.
- **Lifecycle Control Helper**: Included a guest `simrv-power` `/dev/mem` utility in generated Linux images for clean programmatic poweroff, reboot, crash, and simulator-exit requests.

## [v2.0.0-rc.8] — 2026-08-08

Release candidate 8 focuses on CMake user presets modularization, scrubbing hardcoded workspace paths, floating-point rounding precision under Clang, dual-architecture `riscv-tests` integration, and repository documentation polish.

### Build System & Developer Presets

- **Portable CMake Presets**: Restored `CMakePresets.json` to general portable configurations without hardcoded toolchain paths; added local `CMakeUserPresets.json` (gitignored) for developer-specific compiler configurations.
- **Floating-Point Rounding Semantics**: Added `-frounding-math` flag validation to preserve IEEE 754 floating-point rounding mode semantics (`std::fesetround`) and exception raising under Clang `-O3` / ThinLTO passes.

### Test Automation & ISA Verification

- **Dual-Architecture ISA Verification**: Built and integrated 64-bit and 32-bit `riscv-tests` suites, achieving 100% CTest gate pass rate (230 test cases) across both `rv64-release` and `rv32-release` targets.

### Tooling & Cleanups

- **Dynamic Test Path Resolution**: Replaced absolute paths in benchmark runners with dynamic path resolution relative to repository root.
- **Linux Image Builder Modernization**: Updated `scripts/build-linux-image.sh` to target OpenSBI v1.9, Linux Kernel v7.1.7, and BusyBox v1.38.0 with automated musl and glibc cross-compiler detection.
- **Repository Tree Cleanups**: Removed obsolete root `help.txt` and `Makefrag` files and aligned documentation version headers.

## [v2.0.0-rc.6] — 2026-08-05

Release candidate 6 focuses on atomic state synchronization, $O(1)$ TLB generation epoch flushes, selective hardware/soft TLB invalidation, deterministic CLINT timer integration, devicetree syscon-poweroff standard bindings, and post-shutdown execution retention.

### Performance & Cache / TLB Optimizations

- **O(1) Soft TLB Epoch Flushing**: Replaced $O(N)$ 4096-entry memory loops during `soft_tlb_flush()` with a single-instruction generation epoch counter increment (`++soft_tlb_epoch`).
- **Selective Page Invalidation**: Implemented selective page invalidation in `Tlb::flush_selective` and `soft_tlb_flush_selective`, ensuring `SFENCE.VMA vaddr` invalidates only target page entries rather than wiping the complete 2048-entry TLB.
- **Cache & TLB Memory Alignment**: Aligned `CacheLine` to 64 bytes (`alignas(64)`) to match host L1 cache lines; aligned TLB entries to 32 bytes (`alignas(32)`) for power-of-two bit-shift indexing.
- **Direct Subscript Indexing**: Replaced bounds-checked `.at()` array lookups with direct subscript indexing across `BaseCache`, `ICache`, and `DCache`.

### State Synchronization & Devices

- **Lock-Free State Machine**: Atomized `ExecutionState` and shared cross-thread variables (`tohost`, `mtime`, `mtimecmp`, `e_icount`) with `std::atomic<T>`, eliminating data races across simulation, TUI rendering, and GDB stub threads.
- **Deterministic CLINT Time Advancement**: Derived simulated clock time strictly from `clint_mmio.mtime`, guaranteeing deterministic cycle progress and freezing time advancement during simulation pause.
- **Standard Syscon Reset Bindings**: Added standard devicetree `syscon-poweroff` and `reboot` nodes across `virt-rv64.dts` and `virt-rv32.dts` for native OpenSBI driver reset integration.

### Terminal UI & System Lifecycle

- **Post-Shutdown Window Retention**: Retained the full TUI window after guest shutdown (`poweroff`/`halt`) with a `[SHUTDOWN]` badge, enabling post-mortem inspection of registers, memory, statistics, and logs without hanging.
- **Clean Reload on Reboot**: Configured `request_reboot()` and `[Ctrl-R]` to cleanly re-instantiate the simulation engine while preserving user settings.

## [v2.0.0-rc.3] — 2026-07-31

Release candidate 3 focuses on TUI keybinding centralization, Notice Modals UX enhancement, automatic reboot on post-shutdown resume, and licensing compliance.

### Terminal UI & Visual Inspection

- **Centralized Keybinding Registry**: Unified key action bindings, status footer labels, and online help definitions in `TuiKeybindings`.
- **Intuitive Navigation Hotkeys**: Mapped `[n]` for Next step, `[b]` for Backstep, `[m]` for Manage Break/Watchpoints, `[i]` for Inspect Memory, `[w]` for Set Watchpoint, and `[:]`/`[k]` for PC Breakpoints.
- **Centered Notice Modals**: Migrated status notices, warnings, and settings confirmations to centered dialog modals with auto-wrapped text to prevent clipping.
- **Automatic Post-Shutdown Reload**: Configured stepping or resuming from a shut-down guest state to trigger `request_reboot()` automatically, reloading binary images cleanly without hanging.

### Licensing & Compliance

- **MIT Licensing**: Added repository `LICENSE` file under MIT License and documented third-party component licenses in `README.md`.

## [v2.0.0-rc.2] — 2026-07-31

Release candidate 2 focuses on TUI UX refinements, pipeline execution timeline correctness, hardware Sixel capability detection, and compiler prerequisite updates.

### Terminal UI & Visual Inspection

- **Pipeline Chronological Execution Timeline**: Assigned unique 64-bit dynamic instruction sequence IDs (`inst_id`) to stage snapshots, preventing loop iterations from merging and rendering clean textbook pipeline execution timelines (`IF → ID → EX → MEM → WB`).
- **Simplified Cache Navigation**: Streamlined Cache Inspector navigation to use arrow keys (`[↑/↓]` for cache ways, `[←/→]` for cache sets), automatically passing arrow keys through to UART during active execution.
- **ANSI Styling & High-Legibility Badges**: Formatted key shortcut brackets `[key]` in distinct bold ANSI styling with vibrant theme accents.
- **Automatic Sixel Terminal Detection**: Added terminal capability queries (DA1 `\033[c`) and environment checks to detect hardware Sixel support, falling back to clean ANSI text on unsupported terminals.
- **Persistent Cache Statistics**: Preserved cumulative cache hit, miss, and replacement statistics across `FENCE.I` cache line flushes (`BaseCache::flush()`).

### Documentation & Prerequisites

- **Compiler Requirements**: Updated documentation specifying Clang 20+ and GCC 14+ for complete ISO C++23 standard library compatibility.

## [v2.0.0-rc.1] — 2026-07-30

Release candidate 1 for v2.0.0, completing major architectural features and focusing on inspector polish, correctness fixes, and CLI normalization.

### Terminal UI & Visual Inspection

- **Cache Inspector Accuracy**: Annotated exact hit ways using `last_hit_way` tracking and resolved toggle event conflicts between ICache and DCache views.
- **MISA & VLEN Configuration Modal**: Added interactive vector register length configuration (`VLEN`, 32–1024 bits) to the MISA configuration modal.
- **MMIO & Bus Inspector**: Added live inspection views for VirtIO device status flags, IRQ states, Virtqueue 0 physical addresses, and NS16550A UART configuration.
- **Hazard & Page Layout Alignment**: Aligned stage labels (`IF`, `ID`, `EX`, `WB`) across hazard panels and bounded TLB, BP, and Bus pages to prevent line wrapping on narrow terminals.

### Command-Line Interface

- **Command-Line VLEN Configuration**: Added `--vlen <N>` command-line option for runtime vector register length configuration.
- **Explicit Branch Prediction Tracing**: Decoupled debug mode (`-d`) from branch prediction tracing, requiring explicit `--trace-bpred`.

### Bug Fixes & Stability

- **Target Address Isolation**: Separated stack inspector click coordinates from instruction explainer target PC to prevent unintended address pollution.

## [v2.0.0-beta.36] — 2026-07-30

### Terminal UI & Visual Inspection

- **Cache Header Toggles**: Enabled section headers and `[Cache:IC]` / `[Cache:DC]` tab entries to toggle directly between ICache and DCache inspector views.
- **Register Tab Cycling**: Enabled clicking the `Regs` tab header to cycle consecutively across GPR → FPR → VEC views.

## [v2.0.0-beta.34] — 2026-07-29

### Terminal UI & Visual Inspection

- **Settings Persistence**: Ensured machine settings (cycle-accurate mode, debug mode, MISA profile, and color theme) persist across simulator reloads and binary hot-swaps.

## [v2.0.0-beta.33] — 2026-07-29

### Terminal UI & Visual Inspection

- **Individual Way Navigation**: Added cursor navigation across individual cache ways within selected cache sets.
- **32-Byte Line Inspection**: Added full 32-byte hex and ASCII cache line data inspection for selected ways.

## [v2.0.0-beta.32] — 2026-07-28

### Terminal UI & Visual Inspection

- **Interactive Cache Set Inspector**: Added set selection (`j`/`k`), way selection (`0`–`3`, `w`), and live set occupancy map visualization.
- **Cache Replacement Tracking**: Added display tracking for last-evicted tag and last-replaced set/way.
- **Keybinding Collision Audit**: Disambiguated cache inspector keybindings from global navigation shortcuts.

## [v2.0.0-beta.31] — 2026-07-27

### Terminal UI & Performance

- **TUI Rendering Optimization**: Optimized rendering throughput to prevent frame lag during high-speed simulation.
- **Context-Sensitive Tab Visibility**: Automatically hid cache inspector tab in high-performance (IA) mode and suppressed debug shortcuts in normal mode footer.

## [v2.0.0-beta.30] — 2026-07-26

### Terminal UI & Configuration

- **Interactive MISA Modal**: Added `Alt-M` modal to configure ISA extensions (A/B/C/D/F/M/V/S/U) and XLEN mode interactively with draft preview and presets.
- **Mode-Aware Settings**: Updated simulator settings modal options to reflect active simulation mode (cycle-accurate vs high-performance).

## [v2.0.0-beta.27] — 2026-07-25

### Core Architecture & RVV

- **Vector Instruction Fixes**: Corrected several RVV vector memory addressing and element permute edge cases.

### Terminal UI & Visual Inspection

- **Runtime Binary Reloading**: Added interactive binary loading modal (`[o]`) to browse and reload `.bin` workloads at runtime.
- **Shortcut Disambiguation**: Resolved conflicting keybindings across inspector tabs.

## [v2.0.0-beta.26] — 2026-07-24

### Terminal UI & Visual Inspection

- **Pipeline Inspector Overhaul**: Redesigned pipeline page layout for educational readability with clearer stage slots and stall/bubble indicators.

## [v2.0.0-beta.25] — 2026-07-23

### Terminal UI & Education Tools

- **Color-Coded Pipeline Visualizer**: Added color-coded in-flight instruction slots across pipeline stages.
- **Modular Modal Handlers**: Refactored `TuiModal` into independent per-modal handler implementations.

## [v2.0.0-beta.24] — 2026-07-22

### Terminal UI & Controls

- **Frequency-Based Rate Limiter**: Added runtime simulation speed throttling configurable by target frequency (Hz) via the `[f]` key.

## [v2.0.0-beta.22] — 2026-07-21

### Terminal UI & Visual Inspection

- **Consolidated Register Tabs**: Grouped GPR, FPR, and VEC register views under a unified `Regs` tab entry.
- **Tab Navigation**: Mapped `[l]` / `Alt-L` to cycle through tool inspector tabs sequentially.
- **Theme Layout Consistency**: Corrected cache page column alignment across light and dark color themes.

## [v2.0.0-beta.19] — 2026-07-20

### Terminal UI & Visual Inspection

- **Log & Trace Integration**: Integrated execution logging and instruction trace views directly into the LeftPane tab system.
- **Pluggable Panel Hierarchy**: Refactored pane class hierarchy to support pluggable inspector panels.

## [v2.0.0-beta.17] — 2026-07-18

### Terminal UI & Education Tools

- **Guest Stack Frame Inspector**: Added stack inspector panel with symbol-resolved frame layouts.
- **Cache Heatmap Visualizer**: Added cache occupancy heatmap displaying set access frequency levels.
- **Operand Forwarding Diagram**: Added visual data forwarding path diagram highlighting active bypass stages.

## [v2.0.0-beta.15] — 2026-07-17

### Terminal UI & Instruction Explainer

- **Floating-Point Explanations**: Added complete F/D instruction explanations detailing rounding modes, NaN semantics, and exception flags.
- **Vector Extension Explanations**: Added RVV instruction explanations detailing LMUL, SEW, VLEN, and element group layouts.
- **Multi-Word Vector Registers**: Corrected vector register formatting for register lengths exceeding 64 bits.

## [v2.0.0-beta.10] — 2026-07-10

### Microarchitecture & Pipeline

- **Data Hazard Attribution**: Added dynamic pipeline hazard analysis (RAW, WAW, WAR) inside the instruction explainer.
- **Branch Misprediction Penalty**: Added control hazard detection and branch misprediction penalty cycle annotation.

## [v2.0.0-beta.7] — 2026-07-07

### Core Architecture & CLI

- **Dual Simulation Modes**: Added runtime selection between high-performance (IA) and cycle-accurate (CA) simulation engines.
- **Modular Command-Line Parser**: Modularized CLI argument parsing into logical functional groups.
- **Consolidated Test Runner**: Consolidated ISA test execution under the standard bare-metal `appmode` runner.

## [v2.0.0-beta.1] — 2026-07-01

### Foundation Architecture

- **Dual-Width Abstraction**: Implemented XLEN abstraction layer unifying RV32 and RV64 register files and CSR state.
- **Split-Screen Terminal Workbench**: Implemented interactive split-screen TUI monitor with mouse and keyboard input.
- **Supervisor OS Support**: Added OpenSBI boot support and supervisor-mode execution for Linux kernels.
- **Virtual Device Models**: Added VirtIO console and block disk device models.
- **Memory Management Unit**: Added hardware page table walker and TLB models for Sv39 and Sv32 virtual memory.

## [v2.0.0-alpha.4] — 2026-06-15

### Release & Packaging

- **Packaging Infrastructure**: Initial automated release asset packaging and baseline performance benchmarks.

## [v2.0.0-alpha.3] — 2026-06-14

### Initial Alpha

- **Initial Public Alpha**: Initial public release featuring CMake preset infrastructure, Clang-20 CI matrix, and base RISC-V in-order pipeline.

[v2.0.4]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.4
[v2.0.3]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.3
[v2.0.2]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.2
[v2.0.1]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.1
[v2.0.0]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0
[v2.0.0-rc.10]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-rc.10
[v2.0.0-rc.9]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.0.0-rc.9
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
