# SimRV Architecture

## Scope

This document describes the current high-level structure after the initial OOP wrapper refactor slices.

## Core Runtime Components

- `Machine`: top-level pipeline orchestration, memory/device integration, and run-loop control.
- `CPU`: architectural state (GPRs, CSRs, TLB, privilege/exceptions) owned by `Machine`.
- `Disk` and `Console`: MMIO-backed peripheral models attached through `Machine`.
- `Microcn`: optional micro-controller path for I/O support.

## Execution Helpers

- `DecodeUnit`: wrapper around decode/decompression/immediate generation helpers.
- `ExecuteUnit`: wrapper around integer ALU, branch, AMO, and CSR value helper logic.
- `CsrFile`: dedicated CSR register-file component used by `CPU` for CSR read/write and mstatus handling.
- `TlbUnit`: dedicated TLB state helper used by `CPU` for TLB flush behavior.
- `InterruptController`: encapsulates PLIC-facing interrupt bookkeeping (`mip` updates and IRQ set/clear).
- `TrapController`: encapsulates trap entry and return flow (`mret`, `sret`, delegated/non-delegated exception paths).

These helpers are intentionally thin in this phase to preserve behavior while making class boundaries explicit.

## Data Flow (Main CPU Path)

1. IF stage (`stage_if`) runs IFA/IFB/IFC and CVT for fetch, translation, and decompression.
2. ID stage (`stage_id`) runs ID and OF for field decode and register/CSR operand capture.
3. EX stage (`stage_ex`) runs EX1 for ALU/branch/core execute decisions.
4. MEM stage (`stage_mem`) runs LD/EX2/SD for memory read/modify/write behavior.
5. WB stage (`stage_wb`) runs WB for integer/floating writeback.
6. Commit/finalize (`stage_commit` + FIN) applies control-flow updates, trap/interrupt handling, and termination checks.

## Build and Validation

- Primary build framework: CMake + Ninja via preset `ninja-clang-release`.
- Validation gate:
  - `phase1-gate` = regression baseline + ISA smoke subset.

## Repository Layout

- `src/` contains implementation units.
- `include/simrv/` contains public/internal headers used by the simulator targets.

## Current Refactor Boundaries

- Decode and execute helper calls in `Machine` route through class wrappers.
- CSR read/write and mstatus policy route through `CPU::csr_file` (`CsrFile`) for clearer ownership.
- Remaining state-control methods in `CPU` now delegate to dedicated components (`TlbUnit`, `InterruptController`, `TrapController`).
- Internal algorithms remain unchanged in `module.cpp` to keep behavioral parity.
- `PipelineContext` type exists as a staging container for upcoming `r_*` field extraction.
- Architectural scalar aliases now use templated XLEN traits (`Word`, `Register`) with RV32 as active default, with semantic aliases (`CSRValue`, `CSRAddress`, `Address`, `TrapCause`) expanded in core state paths.

## Next Refactor Steps

- Introduce `PipelineContext` struct for current `r_*` transient fields.
- Isolate memory access into a dedicated class while preserving branch shape.
- Progressively reduce direct helper usage in `Microcn` path as needed.
