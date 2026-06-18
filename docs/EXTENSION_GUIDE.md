# Extending SimRV: Adding New ISA Extensions

This guide outlines the process for onboarding new RISC-V extensions (e.g., Vector `V`, Bit-Manipulation `B`, Crypto `K`) into the SimRV architecture.

The simulator uses a straightforward decode-and-dispatch model, meaning new extensions generally follow a linear integration path.

---

## 1. Register the Extension and Opcodes
All core ISA definitions and shared constants live in [`include/simrv/Define.hpp`](../include/simrv/Define.hpp).

1. **Extension Bit:** Add your extension to the `IsaExtension` enum (e.g., `V = 21` for Vector).
2. **`misa` Profiles:** Ensure the new extension is available via `misa_profile_bits()` if it belongs to a standard profile.
3. **Opcodes and Funct fields:** 
   * Add any new major opcodes to the `Opcode` enum.
   * Add sub-operation types to `Funct3` or create a new specific enum (e.g., `Funct6Vector`).
4. **Operation ID (Profiling):** 
   * Add every new instruction to the `OperationId` enum.
   * Define the bounds for your extension at the bottom of the file (e.g., `kOpRangeRv32vBegin` and `kOpRangeRv32vEnd`).
5. **Requirements Check:** Update `required_extension_for_instruction()` so that the core will correctly generate an `IllegalInstruction` exception if the extension is disabled in `misa`.

---

## 2. Decode the Instruction
Instruction decoding logic resides in [`include/simrv/decode/Decoder.hpp`](../include/simrv/decode/Decoder.hpp) and [`src/decode/Decoder.cpp`](../src/decode/Decoder.cpp).

1. **Decoder Helpers:** If the extension uses new instruction formats (e.g., Vector `vtype`, `vm` masks), add extraction methods to the `Decoder` class.
2. **Operation Identification:** Update the `simrv::decode::decoder(Instruction ir)` function. This acts as the master lookup, using `switch/case` over the `Opcode` and `Funct3`/`Funct7` fields to map raw bits to your new `OperationId`.

---

## 3. Implement Execution Logic
Execution routing happens during the pipeline Execute stage in [`src/core/StageEX.cpp`](../src/core/StageEX.cpp), or via specialized execution units if the architecture dictates.

1. **Routing:** Inside `StageEX::evaluate()`, add handling for your major `Opcode`.
2. **Execution Context:**
   * Fetch operands using `cpu.regs.read(decoder.rs1())`.
   * For non-standard state interactions, route to dedicated execution units (e.g., `VectorUnit::execute()`).
3. **Writeback:** Place results into the pipeline registers so they can be written back to architectural state during `StageWB` or `StageCommit`.

---

## 4. Add Architectural State (Registers/CSRs)
If the extension requires new architectural state (e.g., 32 Vector registers of variable length `VLEN`):

1. **Types:** Define new register identifiers in [`include/simrv/xlen/Types.hpp`](../include/simrv/xlen/Types.hpp).
2. **State Storage:** Add the state arrays to the `Cpu` class in [`include/simrv/core/Cpu.hpp`](../include/simrv/core/Cpu.hpp).
3. **Control and Status Registers (CSRs):**
   * Add new CSR addresses to the `Csr` enum in `Define.hpp`.
   * Implement read/write behavior, access control, and illegal instruction checks for the new CSRs in [`src/core/CsrFile.cpp`](../src/core/CsrFile.cpp).

---

## 5. TUI and Tracer Integration
To maintain high observability, ensure your new extensions are visible to developers:

1. **Tracer:** Ensure `OPERATION_NAME` strings are populated so instruction mix files output correctly.
2. **TUI (Terminal User Interface):** 
   * If you added new state (like Vector registers), update [`src/device/Tui.cpp`](../src/device/Tui.cpp) to display them. You may need to add a new pane or cycle through register views.

---

## 6. Testing and Validation
1. Include the `riscv-tests` ISA subset for your extension.
2. Verify behavior using `ctest -L gate` to ensure that adding the new extension didn't break RV32/64 base integer or floating-point correctness.
