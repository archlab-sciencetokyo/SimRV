# SimRV Refactor Class Concept (Phase 2 Blueprint)

## Scope
This document defines the class boundaries and migration strategy for the first OOP refactor wave while preserving current RV32 behavior and performance.

## Non-Negotiables
1. No ISA behavior changes in refactor-only commits.
2. No virtual dispatch on hot path.
3. Keep `make phase1-gate` green after each slice.
4. Keep existing data layout stable unless a commit explicitly measures impact.

## Current Functional Buckets
1. Pipeline + machine state orchestration is concentrated in `Machine`.
2. Decode/decompress/ALU helpers are global functions in module files.
3. CPU architectural and CSR state is owned by `CPU`.
4. Memory/MMIO/translation operations are mixed into `Machine` methods.

## Target Class Model

### 1) DecodeUnit
Responsibility:
1. Convert fetched instruction to canonical 32-bit instruction.
2. Generate immediate values.
3. Produce operation ID for instmix/debug.

Owns logic currently in:
1. `CB_inst_decomp`
2. `CB_imm_gen`
3. `decoder`

Proposed API:
1. `uint32_t decompress(uint32_t raw_ir) const;`
2. `uint32_t immGen(uint32_t ir) const;`
3. `OPERATION_ID decodeOp(uint32_t ir) const;`
4. `bool isCompressed(uint32_t raw_ir) const;`

Performance note:
1. Keep methods inline-friendly (header or translation-unit static wrappers).

### 2) ExecuteUnit
Responsibility:
1. Integer ALU operations.
2. Branch condition evaluation.
3. AMO arithmetic helper operations.
4. CSR writeback value helper.

Owns logic currently in:
1. `ALU_IM`
2. `ALU_B`
3. `ALU_A`
4. `ALU_C`

Proposed API:
1. `uint32_t aluInt(uint32_t a, uint32_t b, uint32_t funct3, uint32_t funct7) const;`
2. `uint32_t branchTaken(uint32_t a, uint32_t b, uint32_t funct3) const;`
3. `uint32_t aluAmo(uint32_t rs2, uint32_t mem, uint32_t funct5) const;`
4. `uint32_t csrWriteValue(uint32_t csr, uint32_t rs1, uint32_t imm, uint32_t funct3) const;`

Performance note:
1. Avoid object state; make this mostly stateless and trivially optimizable.

### 3) PipelineContext
Responsibility:
1. Hold per-instruction transient `r_*` fields currently embedded in `Machine`.
2. Make stage input/output relationships explicit.

Owns logic currently represented as members in `Machine`:
1. IF/CVT/ID/OF/EX/LD/SD temporary registers (`r_ir`, `r_opcode`, `r_mem_addr`, etc.).

Proposed structure:
1. `struct PipelineContext { ... }` with grouped stage sub-structs.
2. Keep names close to existing `r_*` fields in first migration step.

Performance note:
1. Keep POD layout and contiguous memory.
2. Do not heap-allocate.

### 4) MemoryAccess
Responsibility:
1. Encapsulate virtual-to-physical translation and read/write access path.
2. Centralize RAM/MMIO branch points.

Owns logic currently in `Machine`:
1. `target_read`
2. `target_write`
3. helper calls to page-walk and MMIO mapping behavior.

Proposed API:
1. `uint32_t read(uint32_t vaddr, uint32_t funct3, CPU*, uint8_t* mmem, Disk*, Console*, MachineConfig*);`
2. `void write(uint32_t vaddr, uint32_t wdata, uint32_t funct3, CPU*, uint8_t* mmem, Disk*, Console*, MachineSignals*);`

Performance note:
1. Keep switch-based MMIO decode and branch shape unchanged initially.

### 5) MachineConfig and MachineSignals
Responsibility:
1. Separate static config/flags from runtime side effects.
2. Shrink `Machine` responsibility to orchestration.

Proposed split:
1. `MachineConfig`: knobs like `s_*` options.
2. `MachineSignals`: tohost updates, trace triggers, end conditions.

## Stage Ownership After Refactor
1. `Machine::exec` remains top-level orchestrator.
2. `Machine` stages call into `DecodeUnit`, `ExecuteUnit`, and `MemoryAccess`.
3. `PipelineContext` carries stage-local fields.

## Initial File Layout (Concept)
1. `decode_unit.hpp`, `decode_unit.cpp`
2. `execute_unit.hpp`, `execute_unit.cpp`
3. `memory_access.hpp`, `memory_access.cpp`
4. `pipeline_context.hpp`

No requirement to move all logic immediately; wrappers first, relocation later.

## Migration Plan (Safe Slices)

Status update (2026-04-01):
1. Slice 1 completed: `DecodeUnit` wrapper added and integrated in `Machine` decode path.
2. Slice 2 completed: `ExecuteUnit` wrapper added and integrated in `Machine` execute path.
3. Slice 3 started: `PipelineContext` type introduced and attached to `Machine` for staged field migration.

### Slice 1: DecodeUnit wrapper only
1. Add `DecodeUnit` class that internally forwards to existing module helpers.
2. Replace direct calls in `Machine::CVT` and `Machine::ID_` paths via wrapper methods.
3. No algorithm or bit-manipulation changes.

Acceptance:
1. `make phase1-gate` unchanged and green.

### Slice 2: ExecuteUnit wrapper only
1. Add `ExecuteUnit` wrapper around existing ALU helpers.
2. Replace direct calls in `EX1` and `EX2`.
3. Keep exact argument order and semantics.

Acceptance:
1. `make phase1-gate` unchanged and green.

### Slice 3: PipelineContext extraction
1. Move `r_*` fields into a dedicated context struct inside `Machine` first.
2. Keep names and types identical to avoid semantic drift.

Acceptance:
1. Build and tests unchanged.
2. No instruction count regression on smoke runs.

### Slice 4: MemoryAccess extraction
1. Wrap `target_read`/`target_write` into class methods while preserving switches and side effects.
2. Keep `Machine` as caller.

Acceptance:
1. `make phase1-gate` green.
2. ISA smoke parity maintained.

## Performance Guardrails During Refactor
1. Compile with existing optimization flags unchanged.
2. Prefer `final`/non-virtual concrete classes.
3. Keep small methods eligible for inlining.
4. Record elapsed simulation speed line from smoke logs before and after each slice.

## Out of Scope for This Phase
1. RV64 data-width changes.
2. CSR semantic rewrites.
3. MMU redesign.
4. Reordering pipeline stages.

## Decision Record
If a slice requires behavior change, split into:
1. mechanical refactor commit
2. behavior-changing commit

This keeps bisectability and regression analysis straightforward.
