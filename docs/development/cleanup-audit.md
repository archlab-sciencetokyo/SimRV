# Legacy cleanup and source-size audit

This audit records the focused cleanup against the worktree's starting `HEAD`. It is limited to
changed production headers and sources; generated files, tests, and fixtures are excluded from the
source-line totals. Line counts use physical source lines, including comments and blank lines.

## Compatibility classification

| Area | Classification | Decision |
| --- | --- | --- |
| `SimRV::runtime` CMake target | Supported | Keep as the documented host integration boundary. |
| Guest-visible architectural behavior | Supported | Preserve, including guest firmware and SBI behavior. |
| SBI legacy extension IDs and older `FW_PAYLOAD` bundle filenames | Retained guest compatibility | Keep; these are called out in guest-facing code and support existing firmware/images. |
| `--trace-pc` CLI spelling | Retained CLI compatibility | Keep as documented in the migration guide. |
| Pipeline, device, memory, and TUI framework host types | Internal | The migration guide says these are implementation details. Remove redundant unconsumed aliases and facades; use owning types internally. |
| `PipelineSim::{f,d,e,m,w}_reg()` | Internal forwarding facade | Removed; callers use `stage_reg(PipelineStage::...)`. |
| TUI framework `Terminal.hpp` re-exports | Internal compatibility facade | Removed after confirming there were no consumers. |
| `NetworkBackend`, `CycleStage`, `TranslationResult`, root `PageTableWalker`, `pmp::PmpAccessType`, and `PmpConfigView` aliases | Internal aliases | Removed; consumers use the owning type or namespace. The PMP test now names `PmpConfigEntry` directly. |

## Duplication and source-of-truth findings

- MMIO and PCI virtio-input each implemented split-ring descriptor traversal and event queueing.
  They now share `VirtioInputQueue`; DMA access and interrupt delivery remain transport-specific.
- Branch-predictor and hazard TUI panes repeated the same inactive cycle-mode rows. They now use
  `append_cycle_mode_rows`.
- Preset and config-file initialization repeated CPU model application, interconnect timing setup,
  and model-specific DRAM defaults. `apply_cpu_model_runtime_config` now owns that sequence.
- The virtio VERSION_1 feature word was repeated in transport defaults and per-device getters. It
  now has one definition in `VirtioCore.hpp`; devices with extra features retain their custom bits.
- `TuiLayout` was separately re-aliased in the frame renderer. `TuiTypes.hpp` now owns the TUI
  convenience alias; the renderer uses `framework::Layout` directly.
- Legacy aliases listed above had no independent behavior. Their consumers now name the owning
  types directly.
- The typed TLB forwarding methods remain: they have active core call sites and express distinct
  instruction, data-read, and data-write access kinds.

## Source lines by cleanup group

Counts compare the production paths in each group at starting `HEAD` and after this cleanup. The
groups are disjoint and sum to the aggregate below.

| Group | Before | After | Change |
| --- | ---: | ---: | ---: |
| Virtio input queue sharing | 216 | 207 | -9 |
| TUI row sharing | 528 | 529 | +1 |
| Virtio feature source of truth | 1,784 | 1,750 | -34 |
| CPU model initialization | 774 | 767 | -7 |
| Internal alias cleanup | 2,992 | 2,945 | -47 |
| **All changed production headers and sources** | **6,294** | **6,198** | **-96** |

The TUI helper adds one net line because it centralizes repeated output logic without reducing
physical source volume. Test sources, documentation, generated files, and fixtures are excluded
from all cleanup source-line totals.

## Verification

After the cleanup, the RV64 Release build completed with `-j4`. Focused machine-config,
virtio/platform, TUI, pipeline, and CFU tests passed. For the final tree, all 48 sandbox-compatible
RV64 gate tests passed; the socket-dependent GDB stub test passed in a separate unsandboxed run.
`git diff --check` passed.

## CMake organization and target audit

The root `CMakeLists.txt` was split into focused test, developer-target, and packaging modules.
The root plus those three modules contain 1,272 lines, compared with 1,411 lines in the original
root file. This is a net reduction of 139 lines while keeping each concern in a separately
maintainable file. Repeated Linux image, Linux run, and Linux PTY registrations now use small
helpers with their existing arguments and test properties preserved.

The custom target inventory keeps `integration-gate`, `release-check`, optional
`linux-images` profiles, optional `run-linux`/`run-tui`, and optional `vector-gate`. Formatting
and linting have no custom targets: CI invokes `clang-format` directly, and
`SIMRV_ENABLE_CLANG_TIDY` applies clang-tidy during compilation. The duplicate `isa-gate` alias,
formatting targets, and deprecated Doxygen target were removed. The auxiliary `rv32-dev` and
`rv64-dev` presets were also removed. Contributor and automation guidance now points at
`integration-gate`.

Validation: the RV64 Release build completed with `-j4`; all 48 sandbox-compatible gate tests
passed. An optional Linux/tooling configure exposed the expected targets via `cmake --build
<build-dir> --target help`; a cached compiler probe emitted a ccache read-only diagnostic during
that listing, but CMake regenerated the tree and listed the valid targets. `git diff --check`
passed.

The follow-up CMake audit moved the repeated sanitizer ccache paths into shared configure and
build presets. The `integration-gate` target now depends on all native test executables, so it
builds what CTest will run. Compile database export is now set only by the project, and the
compiler-flag check module is included once. Debian and RPM package descriptions now reuse the
common CPack description values. `cmake --list-presets`, the RV64 Release configure, and an RV64
ASan configure passed; Ninja's target query confirmed the gate dependencies. RV64 Debug and
Release builds both passed with `-j4`, and both sandbox-compatible gates passed 48/48. The
ACLINT/APLIC/IMSIC tests apply queued interrupt-line changes at the CPU boundary before checking
`mip`.

## Execution subsystem modularization and ratified ISA audit

### Execution subunit separation
- Split monolithic `ExecuteUnit.hpp` into modular headers matching canonical RISC-V extensions:
  - `ExecuteUnitInt.hpp`: Base Integer (I), Multiply/Divide (M), Atomics (A), and CSR operations.
  - `ExecuteUnitFp.hpp`: Floating-Point operations (F, D).
  - `ExecuteUnitZb.hpp`: Bit manipulation operations (Zba, Zbb, Zbc, Zbs).
  - `ExecuteUnitVector.hpp`: Vector operations (RVV 1.0).
  - `ExecuteUnitZk.hpp`: Scalar cryptography operations (Zkn, Zks, Zk).
  - `ExecuteUnit.hpp`: Retained as a backward-compatible inline facade delegating to the subunits.
- Renamed `src/execute/ExecuteUnitFloat.cpp` to `src/execute/ExecuteUnitFp.cpp` and updated CMake targets.
- Added `src/execute/ExecuteUnitZk.cpp` to provide clean subunit symmetry in `simrv-execution`.
- Replaced 46 duplicated case labels across `aluInt` and `aluIntW` in `ExecuteUnitInt.cpp` with `isa::is_zb_op(op_id)` delegation.
- Renamed internal evaluation template `alu_int_b_eval` to `alu_int_zb_eval` in `ExecuteUnitZb.cpp`.

### Ratified ISA extensions and legacy shorthand deprecation
- Replaced draft single-letter `b` configurations with standard ratified multi-letter presets:
  `gczb`, `gczbv`, `gcv`, `rv32gczb`, `rv64gczb`, `rv32gczbv`, `rv64gczbv`, `rv32gcv`, `rv64gcv`.
- Explicitly reject legacy draft shorthands (`gcbv`, `rv32gcbv`, `rv64gcbv`, `gcb`, `rv32gcb`, `rv64gcb`)
  with diagnostic errors guiding users to ratified equivalents.
- Added full multi-letter extension parsing and composition: `zba`, `zbb`, `zbc`, `zbs`, `zb`,
  `zbkb`, `zbkc`, `zbkx`, `zknd`, `zkne`, `zknh`, `zksed`, `zksh`, `zkr`, `zkt`, `zkn`, `zks`, `zk`,
  `zicsr`, `zifencei`, `zicntr`, `zihpm`, `zve*`, `zvl*`, `xsimrvtrace`.
- Expanded `tests/CoreSemanticsTests.cpp` to explicitly verify `GCZB` (B without V), `GCV` (V without B),
  and `GCZBV` (both B and V) MISA bits.
- Synchronized man pages (`docs/man/simrv.1`), CPU model docs (`docs/hardware/models.md`), compliance
  docs (`docs/architecture/compliance.md`), and extension guides (`docs/hardware/extensions.md`).

### Dual-architecture validation
- RV64 Release build completed cleanly with `-j4`; 49/49 CTest gate tests passed (100%).
- RV32 Release build completed cleanly with `-j4`; 49/49 CTest gate tests passed (100%).
- `clang-format --dry-run --Werror` clean across all production headers, sources, and tests.
- `git diff --check` passed with zero whitespace or line-ending errors.

## VirtIO queue deduplication and ELF loader audit (Pass 3)

### VirtIO Block and Net queue deduplication
- Extracted `include/simrv/device/virtio/VirtioBlockQueue.hpp` (`process_block_queue`):
  - Unified split-ring traversal, descriptor chain parsing, commands (`READ`, `WRITE`, `FLUSH`, `DISCARD`, `WRITE_ZEROES`), used-ring updates, and deferred DMA scheduling.
  - Reduced `VirtioMmioBlock.cpp` from 186 to 75 lines (-111 lines) and `VirtioPciBlock.cpp` from 187 to 87 lines (-100 lines).
- Extracted `include/simrv/device/virtio/VirtioNetQueue.hpp` (`process_net_queue`):
  - Unified `VirtioNetHdr` struct definition across MMIO and PCI transports.
  - Unified RX packet consumption, header construction, payload writing, TX packet extraction, and deferred DMA scheduling.
  - Reduced `VirtioMmioNet.cpp` from 190 to 85 lines (-105 lines) and `VirtioPciNet.cpp` from 238 to 134 lines (-104 lines).

### ELF segment loader consolidation
- In `src/core/MachineInit.cpp`, unified 32-bit and 64-bit ELF segment load loops into a parameterized `load_segment` helper:
  - Eliminated 40 lines of duplicate segment bounds checking, memory copying, and BSS zeroing logic.
  - Ensures identical segment recording and bounds safety for both 32-bit and 64-bit ELF binaries.

### TUI inspector correctness
- In `src/tui/panels/InspectorPanePipeline.cpp`, added `mode == 9` (`Sv48`) and `mode == 10` (`Sv57`) translation mode names.
- Streamlined `reg_name` and `fp_reg_name` to utilize canonical helpers from `RegisterNames.hpp`.

## Ratified specs conformance and GPU queue audit (Pass 4)

### RISC-V ratified specifications conformance
- **`satp` Translation Modes (Sv48/Sv57)**:
  - In `include/simrv/xlen/Constants.hpp:satp_mode_supported`, enabled modes 9 (`Sv48`) and 10 (`Sv57`) on RV64, removing the artificial restriction that blocked guest writes configuring Sv48.
  - In `include/simrv/memory/PageTableWalker.hpp`, added 57-bit sign-extension check in `is_canonical` for mode 10 (`Sv57`).
  - In `src/memory/PageTableWalker.cpp`, implemented 5-level page table walk in `begin_walk` for mode 10 (`Sv57`).
- **`misa` MXL Legalization**:
  - In `src/core/MachineInit.cpp`, parameterized 32-bit execution mode initialization via `isa::misa_with_mxl(initial_misa, 32)` rather than hardcoded 64-bit-only bitshifts.
- **Default Advertised Target**:
  - In `include/simrv/isa/IsaConfig.hpp`, updated `kMisaDefault` to use ratified `IsaPreset::GCZBV`.
- **Privileged Architecture 1.13 Exception Codes**:
  - In `include/simrv/isa/TrapTypes.hpp`, added standard causes `SoftwareCheck = 0x12` (cause 18) and `HardwareError = 0x13` (cause 19).

### VirtIO GPU queue deduplication
- Extracted `include/simrv/device/virtio/VirtioGpuQueue.hpp` (`process_gpu_queue`):
  - Unified `VirtioGpuResp` struct definition across MMIO and PCI transports.
  - Unified split-ring traversal, request/response descriptor parsing, and completion signaling.
  - Reduced `VirtioMmioGpu.cpp` from 83 to 42 lines (-41 lines) and `VirtioPciGpu.cpp` from 73 to 38 lines (-35 lines).
  - Completed deduplication across the entire VirtIO peripheral device suite (Block, Net, Console, Rng, Input, Gpu).

## Ratified specs conformance and subsystem cleanup (Pass 5)

### RISC-V ratified specifications conformance & tooling
- **Symbolic CSR Resolution (`InstructionExplainer.cpp`)**:
  - Added full symbolic naming resolution in `simrv::util::csr_name`:
    - Vector 1.0 CSRs: `vstart` (0x008), `vxsat` (0x009), `vxrm` (0x00A), `vcsr` (0x00F), `vl` (0xC20), `vtype` (0xC21), `vlenb` (0xC22).
    - Sstc ratified CSRs: `stimecmp` (0x14D), `stimecmph` (0x15D).
    - Privileged 1.13 & Security CSRs: `menvcfg` (0x30A), `menvcfgh` (0x31A), `senvcfg` (0x10A), `mseccfg` (0x747), `mseccfgh` (0x757), `mcountinhibit` (0x320).
    - Programmatic decoding ranges: `pmpcfg0..3` (0x3A0..0x3A3), `pmpaddr0..15` (0x3B0..0x3BF), `mhpmevent3..31` (0x323..0x33F), `mhpmcounter3..31` (0xB03..0xB1F), `hpmcounter3..31` (0xC03..0xC1F).
- **ISA Extension Name Categorization (`InstructionExplainer.cpp`)**:
  - Canonically categorized instructions in `get_isa_extension_name`:
    - `Zicsr` for CSR instructions (`CSRRW`..`CSRRCI`).
    - `Zifencei` for `FENCE_I`.
    - `Privileged` for `ECALL` and `EBREAK`.
    - `Vector` for vector instruction families.
- **Modern Ratified Extension CLI Parser (`CliParser.cpp`)**:
  - Added support in `parse_isa_preset` for ratified extensions frequently emitted by modern toolchains:
    - `zihintpause` (Pause hint).
    - `sstc` (Supervisor-mode timer interrupts).
    - `zicbom`, `zicboz`, `zicbop` (Cache-block management operations).
    - `svinval`, `svnapot`, `svpbmt` (Fine-grained TLB invalidation, NAPOT page sizes, and page-based memory types).

### VirtIO queue container optimization (`VirtioCore.hpp`)
- Replaced linear `std::vector` FIFO queues with `std::deque`:
  - `ConsoleBackend::rx_fifo_`: replaced `erase(begin())` with $O(1)$ `pop_front()`.
  - `NetBackend::rx_queue_`: replaced `erase(begin())` with $O(1)$ `pop_front()` and move-semantics vector transfer (`std::move(rx_queue_.front())`), eliminating whole-packet buffer copying and memory shifting.

