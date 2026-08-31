# Extending SimRV: Adding New ISA Extensions

This guide outlines the process for onboarding new RISC-V extensions (e.g., Vector `V`,
Bit-Manipulation `Zb*`, Crypto `K`) into the SimRV architecture.

The simulator uses a decode-and-dispatch pipeline model, so new extensions follow a
linear integration path through define → decode → execute → state → TUI.

---

## 1. Register the Extension and Opcodes

All core ISA definitions and shared constants live in
[`include/simrv/Define.hpp`](../include/simrv/Define.hpp).

1. **Extension Bit:** Add your extension to the `IsaExtension` enum
   (e.g., `V = 21` for Vector).

2. **`misa` Profiles:** Ensure the extension bit is included in the appropriate
   `misa_profile_bits()` entries if it belongs to a standard profile.

3. **Opcodes and Funct fields:**
   - Add new major opcodes to the `Opcode` enum.
   - Add sub-operation discriminants to `Funct3`, `Funct7`, or create a dedicated
     enum (e.g., `Funct6Vector`) for dense sub-encoding spaces.

4. **Operation ID (Profiling and Instruction Mix):**
   - Add every new instruction to the `OperationId` enum.
   - Define range sentinels at the bottom of the enum
     (e.g., `kOpRangeRv32vBegin` / `kOpRangeRv32vEnd`) so that `Tracer` and
     instruction-mix stats can bucket your extension correctly.

5. **Requirements Check:** Update `required_extension_for_instruction()` so the
   core raises `IllegalInstruction` when the extension is disabled in `misa`.

---

## 2. Decode the Instruction

Instruction decoding logic lives in:
- [`include/simrv/pipeline/Decoder.hpp`](../include/simrv/pipeline/Decoder.hpp) — field extraction helpers
- [`src/pipeline/Decoder.cpp`](../src/pipeline/Decoder.cpp) — master decode switch

Steps:

1. **Decoder Helpers:** If the extension introduces new instruction formats
   (e.g., Vector `vtype`, `vm` mask fields), add extraction methods to the
   `Decoder` class.

2. **Operation Identification:** Update the `simrv::pipeline::decoder()` function.
   This is the master lookup using `switch/case` over `Opcode` and `Funct3`/`Funct7`
   to map raw instruction bits to your new `OperationId` values.

3. **Compressed (RVC) variants:** If your extension includes 16-bit forms, handle
   them in the decompression path before the main decode switch.

---

## 3. Implement Execution Logic

Execution routing happens in the Execute stage:
- [`src/execute/ExecuteUnit.cpp`](../src/execute/ExecuteUnit.cpp) — routing hub
- [`src/execute/ExecuteUnitInt.cpp`](../src/execute/ExecuteUnitInt.cpp) — integer ops
- [`src/execute/ExecuteUnitFloat.cpp`](../src/execute/ExecuteUnitFloat.cpp) — FP ops

Steps:

1. **Routing:** Inside the execute dispatch, add a case for your `OperationId` range
   or individual operations, delegating to a new execute unit if warranted.

2. **Execution Context:**
   - Fetch operands from `PipelineContext` (populated during the ID stage).
   - For non-standard state (e.g., vector registers), route to a new dedicated unit
     (e.g., `VectorUnit::execute()`).

3. **Writeback:** Store results back into `PipelineContext` writeback fields so
   `CPU::writeback_registers()` can commit them to architectural state during WB stage.

---

## 4. Add Architectural State (Registers / CSRs)

If the extension requires new architectural state (e.g., 32 vector registers of
variable length):

1. **Types:** Define new register identifiers and associated types in
   [`include/simrv/xlen/Types.hpp`](../include/simrv/xlen/Types.hpp).

2. **State Storage:** Add state arrays or structs to `ArchState` inside
   [`include/simrv/core/Cpu.hpp`](../include/simrv/core/Cpu.hpp) (embedded in `CPU`
   via `state_`).

3. **Control and Status Registers (CSRs):**
   - Add new CSR addresses to the `Csr` enum in `Define.hpp`.
   - Implement read/write behavior, access control, and illegal-instruction guards in
     [`src/core/CsrFile.cpp`](../src/core/CsrFile.cpp).

---

## 5. Instruction Explainer Integration

SimRV's interactive educational explainer (`InstructionExplainer`) shows per-instruction
descriptions in the TUI EXPLAIN pane and via `--explain-inst` CLI. Ensure new instructions
appear correctly:

1. **Mnemonic and Assembly Rep:** Add entries to `InstructionExplainer::get_mnemonic()`
   and `get_assembly_repr()` in
   [`src/util/InstructionExplainer.cpp`](../src/util/InstructionExplainer.cpp).

2. **Description:** Add a short educational description to `get_description()`.
   Include the ISA extension name in the description so users can identify which
   extension an instruction belongs to.

3. **Format:** Set the correct `InstructionFormat` (R, I, S, B, U, J, R4, CR, CI,
   CSS, CIW, CL, CS, CB, CJ) returned by `get_format()`.

---

## 6. TUI and Tracer Integration

To maintain full observability:

1. **Tracer:** Ensure `OPERATION_NAME` strings are populated for all new `OperationId`
   values so instruction-mix output and trace files work correctly.

2. **TUI Register & Subsystem Panes:** The TUI is split into modular components under `src/tui/panels/`:
   - If you add new architectural state (e.g., vector registers), update
     [`src/tui/panels/InspectorPaneRegs.cpp`](../src/tui/panels/InspectorPaneRegs.cpp) or create a dedicated panel view. Add a new view type in
     [`include/simrv/tui/panels/InspectorPane.hpp`](../include/simrv/tui/panels/InspectorPane.hpp) and wire it into the pane cycler.
   - Use the centralized theme helpers from `TuiTheme.hpp` — never hardcode raw ANSI escape sequences.

---

## 7. Testing and Validation

1. **ISA Tests:** Set `RISCV_TESTS_DIR=/path/to/riscv-tests` at CMake configuration time. ISA tests and lockstep checks will automatically register matching targets.

2. **Vector Tests:** For vector extensions, set `SIMRV_VECTOR_TESTS_DIR=/path/to/riscv-vector-tests` or `--vector-tests-dir`.

3. **Gate Suite:** Run the CTest gate suite to validate regression coverage across both architectures:

   ```bash
   ctest --test-dir build/rv64-release --output-on-failure -L gate
   ctest --test-dir build/rv32-release --output-on-failure -L gate
   ```
