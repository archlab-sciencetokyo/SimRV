# SimRV Changelog

All notable changes to SimRV are documented here.
Versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Unreleased

## [v3.0.0-rc.6] — 2026-10-06

- Source builds now default to the isolated `/usr/local/simrv` install prefix (executable at
  `/usr/local/simrv/bin/simrv`); CPack distro packages retain their standard `/usr` layout.
- `--isa` now accepts the standard `rv32g`/`rv64g` shorthand separately from `GC`, and canonical
  multi-letter `Zicsr`, `Zifencei`, and `Zicntr` extensions,
  including supported ratified 2.0 version suffixes. The concise `rv64gc_zicntr2p0` spelling
  reflects that `G` already implies `Zicsr` and `Zifencei`; the expanded spelling remains accepted
  for compatibility.
- Preserved the architectural MISA.X indication for SimRV's documented non-standard trace
  extension in the TUI ISA editor, avoiding an unnecessary simulator reconfiguration when changing
  execution precision.
- Standardized MMIO event payloads with an explicit `latency_cycles` field (`null` when the
  simulator does not model per-access device latency) and added a CLI regression for lifecycle
  timestamps/order, lifecycle/retirement stream separation, and schema-1 retirement compatibility.
- Added schema-2 lifecycle, call, and device event envelopes using architectural `U`/`S`/`M`
  privilege names; lifecycle output now records initialization, running, and completion transitions.
  Trace metadata includes the build commit, lowercase runtime MISA ISA string, trace level,
  guest image path and SHA-256, configuration SHA-256, command-line argument vector, and host
  platform identity, plus the effective registered MMIO devices and fixed VirtIO RNG seed.
- Added a level-1 `interrupts.jsonl` stream for architectural interrupt entry and xRET return,
  plus pending-state assertion/deassertion edges. Records include delegated CSR-bank names, cause
  values, saved PCs, handler PCs, source components, and nesting depth.
- Added level-4 TileLink-C transaction events in `bus.jsonl`, preserving protocol source/sink IDs
  and labeling them as bus identifiers rather than hart IDs. Request/response records now include
  sequence, hart/master, target, transfer width and byte enables, data, request/completion cycles,
  modeled latency, and denial/corruption status when applicable.
- Added level-4 `pipeline.jsonl` stall events directly from the cycle-accurate pipeline, independent
  of TUI execution-detail capture, with per-stage PCs, remaining latency, and stall reason.
- Added level-1 DMA scheduler start/completion events to `devices.jsonl`, correlating generic DMA and
  VirtIO transfers by run-local transfer ID, source component, byte count, and modeled timing.
- Added a deterministic guest execution fixture covering an ACLINT MSWI MMIO write, device-raised
  interrupt assertion, handler entry/return, correlated TileLink request/response, and `tohost`
  completion.
- Added pre-serialization event-name, hart, and inclusive cycle-range filters for architectural
  event streams; lifecycle records remain unfiltered and selected filters are recorded in metadata.
- Added an inclusive architectural PC-range filter, retaining the legacy numeric `--trace-pc`
  sampling behavior and accepting ranges through `--trace-pc` or `--trace-pc-range`.
- Added exact ELF-symbol and component-name filters through `--trace-function` and
  `--trace-device`; filters are applied before serialization and included in run metadata.
- Added level-4 `memory.jsonl` events for successfully retired scalar loads, stores, and AMOs,
  distinguishing RAM, MMIO, unmapped, and unresolved physical translations while keeping detailed
  device accesses in `devices.jsonl`.
- Added level-4 `memory_fault` events for precise synchronous scalar load/store/AMO faults, carrying
  the effective address, attempted store value when relevant, architectural cause, and `tval`.
- Added cycle-scheduled architectural checkpoints with non-overwriting names and JSON sidecars
  describing cycle, hart PCs/privilege, RAM and checkpoint hashes, and replay limitations.
- Added sparse retirement indexes with byte offsets every 256 records, enabling downstream tools
  to seek approximately by cycle, hart, PC, or event type without loading the full JSONL trace.
- Added opt-in gzip compression for retirement traces via a `.gz` `--arch-trace` path. The
  decompressed schema-1 records remain unchanged; compressed traces omit the plain-byte-offset
  index, and runtime zlib is required. Other trace streams remain JSONL.
- Schema-2 MMIO, trap, and SBI events and their cycle filters now use architectural `mcycle`; legacy
  text logs retain their existing `mtime` notation.
- Architectural device tracing now records MMIO successes, mapped access faults, and unmapped
  accesses without requiring the legacy detailed-log option, attributed to the requesting hart and
  instruction PC.
- Added guest annotations through SimRV's custom U-level CSR `0x800`, with C and assembly helpers
  and per-hart `marker_begin`, `marker_end`, and `marker` events in `markers.jsonl`. The `misa.X`
  bit advertises the non-standard extension; Spike's standard ISA string intentionally omits it.
- Kept return destinations in the standard `target_pc` field without duplicating them as
  `return_pc`; call events retain `return_pc` because it identifies the call's link address.
- Fixed cycle-accurate architectural retirement traces to record the retiring pipeline slot's
  PC, instruction, operation, and next PC without depending on TUI execution-detail capture.
- Added optional schema-versioned `call` and `return` JSONL events to `--arch-trace`, including
  hart, cycle, source/target/return PCs, and dynamic call depth. Existing `retire` events remain
  compatible.
- Added optional ELF function attribution to schema-2 events that carry a source or fault PC; the
  field is omitted when the loaded symbol table cannot resolve the address.
- Added `--log-format text|json|json-pretty` for human-readable or machine-readable diagnostic
  output. Architectural JSONL trace artifacts retain their own stable schemas and are unaffected.

## [v3.0.0-rc.5] — 2026-10-04

This release candidate adds the standard Linux and FPGA-ready boot profiles and completes the
interactive Alpine login/privilege profiling path.

- Add opt-in Alpine debug and Buildroot FPGA profiles with separable OpenSBI, Linux, DTB, and
  journaled ext4 artifacts plus reproducibility manifests.
- Use conventional BusyBox `/sbin/init`, serial getty, ext4 UUID/label mounting, and explicit
  debug-only early-userspace tracing.
- Qualify authenticated serial login, post-login JWM startup, root-to-nobody privilege checks,
  and clean poweroff/reboot on disposable filesystem clones.
- Improve the Linux PTY harness for direct UART input and Alpine shell prompt detection.

## [v3.0.0-rc.4] — 2026-10-04

This release candidate hardens the Linux GUI smoke path and makes repeated profiling safer.

- Bundle the Alpine X11/JWM desktop stack when host-side RISC-V emulation is available, refresh
  guest font caches, and remove the optional `xclock` dependency so JWM starts reliably.
- Build the Linux root filesystem as standard journaled ext4, validate it with `e2fsck`, and add
  a safe per-run clone helper for profiling and emulator smoke tests.
- Add TAP gateway DNS relay documentation and use the gateway resolver before public fallbacks.
- Use the absolute `/sbin/switch_root` path in the initramfs handoff.
- Qualify RV32/RV64 gates and the GUI/TAP browser smoke path for this release candidate.

## [v3.0.0-rc.3] — 2026-10-03

This release candidate clarifies SimRV terminology by reserving “profile” for standards-defined
RISC-V profiles and naming SimRV-selectable configurations as presets.

### Terminology

- Renamed the internal `CpuModelProfile` API to `CpuModelPreset` and made `--cpu-preset` the
  only supported CLI spelling.
- Renamed the internal MISA preset API to `IsaPreset`, including parser, serializer, and runtime
  configuration names.
- Renamed CPU-model configuration keys to `preset`/`cpu_preset` and ISA configuration to
  `isa_preset`.
- Removed unused compatibility facades and wrappers from the TUI, trap controller, and CPU
  interrupt-device plumbing.
- Made the Alpine/JWM Games submenu idempotent so repeated boots do not duplicate it on the
  persistent root filesystem.

## [v3.0.0-rc.2] — 2026-10-03

This release candidate hardens the 3.0 GA workflow around portable operation, capability
discovery, and reproducible release qualification.

### Release workflow and diagnostics

- Added `simrv --isa-info` to expose the qualified ISA, vector, privilege, and debugger contract,
  including explicit RVV and RMM qualification boundaries.
- Added `simrv --doctor` for terminal, Sixel, mouse, attach-transport, and package-runtime
  diagnostics.
- Added a repeatable GA smoke workflow covering binary versioning, capability discovery, SoC
  manifest export, schema compatibility, and optional guest execution.
- Added release-gate and CTest coverage for the GA smoke workflow and new CLI diagnostics.

### SoC and Linux usability

- Versioned exported SoC manifests with a 3.0 compatibility contract and generator identity.
- Documented the Alpine/JWM graphical Linux path, VirtIO framebuffer/input behavior, and the
  attachable TUI/GDB acceptance workflow.
- Kept Sixel presentation optional while validating guest framebuffer behavior independently.

## [v3.0.0-rc.1] — 2026-09-30

This release candidate completes the local attachable TUI path for the 3.0 architecture-debugging
workflow.

### Attach and inspect

- Hardened `--listen-tui`/`--attach` Unix-socket sessions with protocol validation, bounded queues,
  stale-endpoint recovery, local-only permissions, reconnect lifecycle notifications, and clean
  detach behavior.
- Added plain-output attach behavior for pipes and scripts; interactive attachments retain the full
  split-screen TUI.
- Made the inspector log pane scrollable with visible position hints and disabled stale value
  hitboxes while execution is running.

### Release qualification

- Added IPC stream, malformed-frame, endpoint cleanup, reconnect, and controller-isolation tests.
- Added a supported Linux distribution matrix for native packages and a fully static musl portable
  archive build.

## [v3.0.0-beta.3] — 2026-09-29

This beta packages the RV64-capable `simrv` runtime as the single public simulator build and
improves installation and versioned documentation for lab and artifact users.

### Distribution & Documentation

- Consolidated CPack TGZ, RPM, and DEB outputs around the public `simrv` command.
- Added package dependency manifests, clean-install validation, and package linting.
- Published stable and development MkDocs trees together on GitHub Pages, with installation
  guidance for portable archives and native packages.
- Retained native RV32 qualification and hosted RV32 guest coverage as strict-width checks.

## [v3.0.0-beta.2] — 2026-09-28

This beta consolidates the architecture-debugging, reproducibility, platform-timing, and native
packaging work intended for evaluation of SimRV 3.0.

### Distribution & Reproducibility

- Added component RPM and DEB packages for RV32 and RV64 release builds: a small simulator runtime,
  optional architecture tools, and separate benchmark tooling.
- Added GitHub Release publication and package-content validation, with RPMs built on Fedora and
  DEBs built in a Debian-based GCC 16 environment. Both formats are indexed by guest architecture
  with standard RPM repository metadata and Debian dependency manifests.
- Hardened benchmark provenance, stopping-policy comparison, event cleanup, JSON-schema validation,
  and the paper artifact workflow.

### Platform & User Interface

- Replaced the legacy CLI `--misa` spelling with canonical `--isa` profiles.
- Centralized CPU clock and architectural timebase constants across execution, generated device
  trees, real-time pacing, and TUI statistics.
- Updated Linux image tooling and user documentation for reproducible scratch-based builds and
  current RISC-V toolchains.

## [v3.0.0-beta.1] — 2026-09-24

This first beta release of SimRV 3.0 concludes Milestone 1: Performance Acceleration & TUI Hardening. It delivers unified inlined fast memory access, RVV 1.0 high-throughput bulk memory transfers, branch predictor optimizations, 2D viewport navigation overhaul, vector register and CSR status inspection, differential TUI line rendering with execution burst throttling, and full dual-architecture gate qualification across RV64 and RV32.

### Execution Engine & Memory Performance

- Implemented unified inlined fast memory engine: precomputed access sizes in `CachedOp` at decode time and inlined direct SoftTLB host-pointer translation alongside M-mode direct-RAM accesses in integer load/store paths (`execute_cached_load`/`execute_cached_store`) and floating-point fast paths (`FLW`/`FLD`/`FSW`/`FSD`).
- Added direct fast opcode dispatch in `CPU::execute_cached_op_fast` for RVV 1.0 vector instructions, avoiding fallback into the multi-stage pipeline simulation model during fast-mode execution.
- Accelerated unit-stride vector memory operations (`vle*`, `vse*`, `vl*re*`, `vs*re*`, `vlm`, `vsm`) with page-bounded bulk contiguous memory transfers (`std::memcpy`), guarded by memory geometry, watchpoint checks, and PMP permissions, with transparent element-by-element fallback for page crossings and faults.
- Streamlined `CachedOp` footprint and dispatch tables across fast-path and cycle-accurate fetch and decompression stages.

### Branch Predictor & Strong Types

- Optimized branch predictor tables using branchless two-bit saturation updates and fast return address stack (RAS) lookups.
- Strengthened type safety with dedicated domain aliases for architectural registers, addresses, and instruction traits.

### User Interface & Interactive Navigation

- Implemented true differential rendering in `Tui::render()`, skipping unchanged screen lines, tracking cursor motion, suppressing empty frame generation, and throttling UI refreshes during continuous high-speed stepping bursts.
- Throttled non-visible detailed execution sub-views (Pipeline, Hazard, Branch Predictor, Disassembly, Explain, Trace) during fast continuous execution, minimizing simulator hot-path overhead.
- Overhauled 2D scrolling in `framework::ScrollView` across all inspector tabs with responsive viewport clipping, scroll overflow markers (`▲`, `▼`, `◀`, `▶`), and live scroll update synchronization.
- Enhanced the TUI Register Inspector (`TuiRegPage::VEC` double-column view) to display vector control and status registers: `vl`, `vtype` (with decoded `vsew` and `vlmul`), `vstart`, `vlenb` (with `vlen`), `vxrm`, `vxsat`, `vta/vma`, and `vill`.

### Verification & Quality Assurance

- Validated full dual-architecture gate suites: 100% pass rate across RV64 (40/40) and RV32 (40/40) release gates.
- Added comprehensive semantic regressions for bulk vector memory operations and TUI differential rendering throttling.
- Verified Linux kernel boot stability on fast-mode PTY (`linux-boot-pty`) and multi-hart SMP (`linux-ia-quantum-smp-pty`).

## [v3.0.0-alpha.4] — 2026-09-19

This alpha release delivers dynamic SMP multi-hart Linux boot and device tree synthesis, an authoritative hardware register scoreboard, complete CLI/tooling installation with man pages, responsive TUI horizontal scrolling overhaul, and branchless branch predictor acceleration.

### SMP & System Architecture

- Dynamically generated Flattened Device Tree (FDT) topology for arbitrary SMP core counts (2 to 16 harts), creating CPU nodes, CLINT software/timer interrupt mappings, and PLIC context controllers on the fly.
- Updated secondary hart initialization to dynamically coordinate with OpenSBI HSM loops (`Started` under OpenSBI, `Stopped` under direct-SBI) and synchronized architectural vector configuration (`vlen`).
- Verified clean multi-core Linux kernel boots and concurrent multi-hart userland scheduling.

### Pipeline Microarchitecture & Telemetry

- Implemented an authoritative hardware register scoreboard tracking inflight functional unit dependencies across INT, FP, and Vector pipelines.
- Unified instruction unpacking into `DecodedInstruction` to guarantee structural parity across microarchitectural pipelines.
- Accelerated the cycle-accurate branch predictor update path using branchless saturating counter lookup tables and branchless statistic accounting.

### User Interface & Packaging

- Completed the user packaging suite (`make install`) including documentation, developer headers, sample lesson missions, and man page (`simrv.1`).
- Overhauled the TUI Inspector pane with responsive horizontal viewport panning, dynamic width calculations, and per-page scroll state isolation.
- Refined TUI keybindings and modal navigation for seamless pause/run state transitions and guest terminal input routing.
- Updated host compiler requirements to modern C++23 standards (Clang 22+ and GCC 16+).

## [v3.0.0-alpha.3] — 2026-09-19

This alpha release introduces the modular RTL parity framework, exact cycle parity validation against physical RTL designs, human-editable CPU model profiles, and automated pipeline tuning utilities.

### RTL Parity & Validation Framework

- Implemented a reusable, modular RTL parity framework under `scripts/rtl_parity/` with dynamic target registry support.
- Achieved exact cycle-level execution parity against CFU-Proving-Ground RVProc.
- Added automated RTL parity evaluation harness (`scripts/evaluate_rtl_parity.py`) with customizable trace format parsing and cycle-by-cycle comparison.
- Added comprehensive documentation for RTL parity methodology, trace extraction, and target registration in `docs/RTL_PARITY.md`.

### CPU Model Configuration & Calibration

- Introduced human-editable JSON configuration profiles under `models/` validated against `schemas/cpu-model.schema.json`.
- Implemented `scripts/cpu_model_wizard.py` for interactive CLI CPU model creation and validation.
- Added `scripts/tune_cpu_model.py` for automated parameter search and calibration of pipeline stage latencies and penalties.
- Enabled export and inspection of active CPU model profiles directly from the TUI workbench.
- Supported 32-bit CPU model configurations on RV64 platforms with dynamic MISA synchronization.

### Execution Engine & Concurrency Hardening

- Resolved TSan race conditions during concurrent multi-hart TUI telemetry and snapshot publication.
- Unified bare-metal and OS runner quiescence wait loops and scoped runner activity accounting.
- Hardened SMP worker thread lifecycle and quantum scheduling synchronization.

## [v3.0.0-alpha.2] — 2026-09-01

This alpha release delivers comprehensive strong domain typing, code reduction, and modernization across all major architectural subsystems (Core, Memory, Execution, Platform, Debug, and Pipeline).

### Modernization & Strong Typing

- Modernized TLB subsystem with dedicated `Asid`, `TlbSetIndex`, `TlbWay`, and `TlbFlushFilter` domain types.
- Strongly typed GDB stub socket handles with `UniqueFd` RAII wrappers and added strongly typed `GdbBreakpointType` and `GdbSignal`.
- Strongly typed symbol table structures with `SymbolType`, `SymbolBinding`, `SymbolVisibility`, and `SymbolIndex`.
- Replaced raw integer constants with strongly typed offsets and bitmasks in UART (`UartRegisterOffset`, `UartInterruptFlag`), PMP (`PmpStatusFlag`), TileLink (`TlPort`), and ACLINT (`AclintRegisterOffset`).
- Added strong types for SBI extension/function IDs, AIA APLIC/IMSIC IDs, VirtIO feature masks, and PCIe configuration space domains.

### Code Reduction & Architectural Cleanups

- Streamlined floating-point unit implementations with generic execution and class templates across single- and double-precision instructions.
- Consolidated B-extension bit manipulation and integer word arithmetic units.
- Unified PLIC priority evaluation and claim scanning across all execution contexts.
- Extracted `RunnerBase` to deduplicate multi-threaded worker lifecycle and quiescence synchronization between bare-metal and OS runner engines.
- Templated decode tables and centralized instruction disassembly formatters and CSR name lookups.
- Streamlined CLI parser and bus node attachment logic, eliminating redundant option predicates.

## [v3.0.0-alpha.1] — 2026-08-31

This alpha rebaselines the former 2.1 development line as 3.0 because the execution architecture,
host APIs, CLI compatibility surface, and pipeline choices are breaking changes. The historical
2.1 alpha entries below are retained for provenance; that prerelease line is superseded by 3.0.

### Execution architecture

- Replaced post-retirement timing penalties with one deterministic per-cycle transition kernel for
  three-stage and five-stage in-order pipelines. Fast CLI and observable TUI policies share the
  same transitions and differ only in bounded diagnostic history.
- Kept `--ia` and `--ca` as the public execution choices. CLI selects the fast policy and TUI
  selects the observable policy for the chosen engine.
- Added timed instruction/data cache traffic, resumable Sv32/Sv39 page walks, atomic accessed/dirty
  PTE updates, deterministic hart-order arbitration, and cycle-boundary coherence effects.
- Removed the coroutine pipeline, dual-issue model, aggregate cache/TLB penalty fields, and the TUI
  high-performance toggle. `--pipeline` now accepts only `3stage` or `5stage`; there are no
  compatibility aliases for removed pipeline presets or tuning flags.

### Developer interfaces

- Generalized instruction decoding around collision-checked `consteval` tables, including common
  and mask-dependent vector encodings and single/double FP operations. Irregular encodings retain
  explicit secondary-field validation, while pipeline timing classes and mnemonic lookup use
  shared operation metadata.
- Added `--log-file FILE` to mirror timestamped configuration, diagnostics, typed termination
  reason, performance counters, cache statistics, and bus statistics without requiring the TUI.
- Added machine-readable repeated benchmark reports and optional Linux `perf stat` host counters.
  Host counters supplement wall-clock throughput and must only be compared under a controlled host
  configuration.
- Limited full ELF symbol loading to observable TUI execution; CLI loads only runtime-essential
  symbols needed for guest control.

### Correctness and qualification

- Added register-bank-aware RAW interlocks for FP loads, FP arithmetic, integer/FP conversions,
  FP stores, and fused operations including `rs3`, while preserving integer forwarding.
- Made cache refills count exactly one miss without a synthetic hit and made the instruction
  explainer inspect a stable snapshot without changing cache, PC, pipeline, TLB, or counters.
- Treat performance studies as non-blocking evidence. Numerical results in the historical 2.1
  entries are local measurements, not 3.0 release guarantees, unless reproduced with recorded
  host, compiler, workload, revision, and configuration provenance.

## [v2.1.0-alpha.2] — 2026-08-31

Alpha 2 release featuring high-performance floating-point acceleration, multi-level cache hierarchy, quantum-sliced and multi-threaded SMP execution, and compile-time 2D LUT instruction decoding.

### Key Capabilities & Enhancements

- **Lazy MXCSR Floating-Point Unit**: Replaced per-instruction libc rounding mode transitions with host MXCSR state tracking, accelerating FP execution (e.g. `mm.riscv` double-precision matrix multiplication from 13.2 MIPS to 51.6 MIPS, 1.35x faster than Spike).
- **Hardware FMA Acceleration**: Direct `__builtin_fmaf` / `__builtin_fma` fused multiply-add intrinsics with single-rounding precision and branchless canonical quiet NaN fixups.
- **Multi-Level Cache Hierarchy**: Fast $O(1)$ L1 tag matching, 64 KiB 8-way L2 Unified Intermediate Cache, and 512 KiB 16-way shared Last-Level Cache (LLC) integrated with TileLink-C Directory Coherence.
- **Quantum-Sliced & True MT-SMP Schedulers**: Configurable instruction burst scheduling (`smp_quantum`) and parallel `std::jthread` worker threads with atomic SBI v2.0 Hart State Management (HSM) states and direct IPI wakeups, achieving 108.2 MIPS on `mt-matmul.riscv` (1.72x faster than Spike).
- **Compile-Time 2D LUT Decoder**: Replaced nested switch branches with `constexpr` 2D lookup tables for ALU, OP-IMM, CSR, AMO, and FMA instruction groups, reaching 235.5 MIPS core speed on Montgomery multiplication.
- **TUI Alignment**: Hierarchical L1/L2/L3 cache metrics in the Cache Inspector, Status Bar SMP indicators (`[SMP: 4 (MT)]`), and live decoder LUT hit metrics.

## [v2.1.0-alpha.1] — 2026-08-20

Alpha prerelease introducing full **Multi-Hart Symmetric Multiprocessing (SMP)** support, **TileLink-C 5-Channel Directory Cache Coherence (MESI)**, and fast inline directory lookups.

### Key Capabilities & Enhancements

- **Multi-Hart SMP Architecture**: Full OpenSBI SBI 3.0 HSM (Hart State Management) and IPI (Inter-Processor Interrupt) dispatch across dual/multi-core RISC-V systems.
- **TileLink-C Directory Cache Coherence**: Complete 5-channel messaging implementation (Channels A, B, C, D, E) with `AcquireBlock`, `AcquirePerm`, `ProbeBlock`, `ProbeAckData`, and `GrantData` transactions.
- **MESI Coherence Protocol**: Standard 4-state line management (`[M]` Modified, `[E]` Exclusive Clean, `[S]` Shared Clean, `[I]` Invalid) with sole-sharer Exclusive Clean grants to eliminate upgrade bus traffic on local stores.
- **Direct-Mapped Inline Directory Cache**: $O(1)$ fast cache path for directory state lookups and dirty writeback management.
- **TUI Visual Inspection**: Real-time MESI cache line state tags (`[M]`, `[E]`, `[S]`, `[I]`) in the Cache Inspector and TileLink-C channel metric counters in the Bus/IO panel.
- **Dynamic Device Tree Multi-Hart Generation**: Automatic phandle-isolated CPU node and interrupt controller generation supporting multi-core Linux boot.

## [v2.0.2] — 2026-08-28

Maintenance release ensuring side-effect-free instruction explanation in the TUI left pane to prevent spurious ICache hits during tool tab navigation.

### Bug Fixes & TUI Stability

- **Instruction Explainer Side-Effect Removal**: Refactored `LeftPaneExplain::get_explain_rows()` to use an isolated, side-effect-free direct memory decode path instead of executing the core CPU `fetch_stage()` coroutine, eliminating spurious ICache hit counter increments and pipeline context mutations during TUI page cycling.

## [v2.0.1] — 2026-08-28

Maintenance release addressing cache hit/miss accounting accuracy and TUI cache visual inspector state synchronization.

### Bug Fixes & Microarchitecture

- **Cache Accounting**: Fixed an issue where refill reads immediately following miss insertions generated spurious hit events and corrupted hit rate statistics.
- **TUI Cache Inspector**: Corrected hit vs. eviction highlight priority in the Left Pane Cache view to prevent stale replacement markers from masking current hit indications.
- **Base Cache State**: Ensured `BaseCache::insert` marks the current access as a compulsory/conflict miss state rather than inheriting stale hit indicators.

## [v2.0.0] — 2026-08-19

General Availability release of SimRV 2.0: A dual-width explainable RISC-V full-system simulator written in modern ISO C++23, providing high-throughput functional simulation, in-terminal visual inspection, and multi-OS/RTOS execution.

### Architectural Highlights & Major Capabilities

- **Dual-Width Parametric Engine**: Compile-time parameterization (`SIMRV_XLEN`) for RV32GCBV and RV64GCBV with zero runtime virtualization overhead.
- **High-Throughput Execution**: Optimized fast-path functional engine delivering 195+ MIPS on CoreMark with an overall 1.66x geometric mean speedup over Spike across 20-run statistical benchmark evaluation.
- **Microarchitectural Explainability**: 5-stage structural pipeline modeling with dynamic inter-stage RAW/WAR/WAW hazard attribution, forwarding analysis, and natural-language causal event synthesis.
- **In-Terminal Visual Inspection**: Split-screen TUI powered by an internal ANSI/VT100 Virtual Terminal and hardware Sixel graphics protocol, streaming guest graphical framebuffers over headless SSH text sessions with zero host GUI dependencies.
- **Full-Stack OS & RTOS Platform**: Direct execution of upstream RISC-V Linux kernels (v6.x/v7.x), μT-Kernel 3.0, and embedded RTOS payloads over standard MMIO peripherals (VirtIO Block, NS16550 UART, CLINT, PLIC, RTC, and TileLink crossbar).
- **Correctness & Research Reproducibility**: 100% CTest gate pass rate across RV32 (274/274) and RV64 (368/368), real-time differential Spike lockstep verification (`SpikeLockstep`), and ACM/IEEE open-science reproducibility tooling.

## [v2.0.0-rc.10] — 2026-08-19

Release candidate 10 hardens release engineering workflows, adds strict required-suite schemas, streamlines dependencies by removing host SDL bridges while preserving simulated MMIO devices, and fixes headless execution across the vector test suite.

### Release engineering and reproducibility

- Added versioned release, evidence, and experiment schemas with strict required-suite coverage.
- Added portable dependency inputs, machine-readable evidence, deterministic aggregation/plotting,
  and reproducibility archive tooling.
- Changed performance qualification to an evidence-only policy and retained explicit FP/RVV gaps.
- Added citation metadata, a support matrix, research-companion documentation, and academic support
  and security boundaries.
- Hardened Linux PTY shutdown validation against the expected terminal-close race.
- Fixed vector test runner to execute in headless `--cli` mode, achieving 100% pass across all 1,067 tests.
- Removed experimental SDL3 host audio/display bridges and third-party soundfont headers (`tsf.h`, `tml.h`) while preserving simulated MMIO device models (`Audio`, `Framebuffer`, `InputDevice`).

## [v2.0.0-rc.9] — 2026-08-14

Release candidate 9 focuses on architectural compliance, trap and interrupt correctness,
OS lifecycle control, MMIO safety, and TUI/UART stability ahead of v2.0.0.

### Breaking CLI cleanup

### TUI framework hardening

- Corrected Unicode display-cell accounting for wide and combining characters, centralized frame
  and modal resize geometry, and kept constrained modal borders closed with a clipping notice.
- Generated footer labels and online action help from the canonical keybinding registry, with
  exact-width two-column help rows and resize-stable mouse hit-testing.
- Extracted full-frame composition into a pure tested renderer and removed the stale `k` alias for
  setting breakpoints; `[:]` sets a breakpoint and `[k]` toggles one at the current PC.
- Kept `Ctrl-R` reboot and `Ctrl-Q` quit globally available after shutdown and over modals; quit
  now uses the machine exit request without transiently resuming stopped execution.
- Made the interactive Student Guide opt-in with `[g]`; it remains hidden while running and in
  terminals too short to show it without displacing architectural state.
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
- Generated Linux images include a root-only `simrv-power` `/dev/mem` helper for direct poweroff,
  reboot, crash, and simulator-exit requests when the normal OS lifecycle path is unavailable.
  Its SimRV-specific `exit` request terminates the monitor as well as guest execution, while
  poweroff retains the shut-down machine for TUI inspection.

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
- **Hazard inspector**: aligned stage labels (`IF`, `ID`, `EX`, `WB`) for
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

[v3.0.0-rc.5]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v3.0.0-rc.5
[v3.0.0-rc.6]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v3.0.0-rc.6
[v3.0.0-rc.4]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v3.0.0-rc.4
[v3.0.0-rc.3]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v3.0.0-rc.3
[v3.0.0-rc.2]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v3.0.0-rc.2
[v3.0.0-rc.1]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v3.0.0-rc.1
[v3.0.0-alpha.2]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v3.0.0-alpha.2
[v3.0.0-alpha.1]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v3.0.0-alpha.1
[v2.1.0-alpha.2]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.1.0-alpha.2
[v2.1.0-alpha.1]: https://github.com/archlab-sciencetokyo/SimRV/releases/tag/v2.1.0-alpha.1
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
