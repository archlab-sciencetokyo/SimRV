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

These helpers are intentionally thin in this phase to preserve behavior while making class boundaries explicit.

## Data Flow (Main CPU Path)
1. IFA/IFB/IFC fetch raw instruction bits and translate addresses.
2. CVT normalizes compressed instructions and updates instruction mix.
3. ID/OF extract fields and fetch register/CSR operands.
4. EX1/LD/EX2/SD/WB execute operation, handle memory paths, and write back results.
5. COM/FIN update control flow, traps, and simulation completion.

## Build and Validation
- Primary build framework: CMake + Ninja via preset `ninja-clang-release`.
- Validation gate:
  - `phase1-gate` = regression baseline + ISA smoke subset.

## Repository Layout
- `src/` contains implementation units.
- `include/simrv/` contains public/internal headers used by the simulator targets.

## Current Refactor Boundaries
- Decode and execute helper calls in `Machine` route through class wrappers.
- Internal algorithms remain unchanged in `module.cpp` to keep behavioral parity.
- `PipelineContext` type exists as a staging container for upcoming `r_*` field extraction.
- Architectural scalar aliases now use templated XLEN traits (`Word`, `Register`) with RV32 as active default.

## Next Refactor Steps
- Introduce `PipelineContext` struct for current `r_*` transient fields.
- Isolate memory access into a dedicated class while preserving branch shape.
- Progressively reduce direct helper usage in `Microcn` path as needed.
