# SimRV Classroom Sample Missions

This directory contains ready-to-run educational classroom missions for computer architecture and assembly courses.

## Available Missions

### 1. Control Flow & ABI Function Calls (`control-flow-calls.mission`)

- **Target Program:** `control-flow-calls.elf`
- **Source Code:** [`examples/isa/control-flow-calls.S`](../control-flow-calls.S)
- **Prebuilt ELF:** [`examples/isa/build-rv64/control-flow-calls.elf`](../build-rv64/control-flow-calls.elf)
- **Concepts Covered:**
  1. **Conditional Branching:** Evaluating `bne` branch condition based on register operands.
  2. **Loop Control:** Following backward loop branches in the Trace inspector.
  3. **Function Call Setup:** Understanding caller-saved argument registers (`a0`, `a1`), link register (`ra`), and stack pointer (`sp`).
  4. **Callee Stack Frame:** Observing callee stack allocation (`addi sp, sp, -16`) and `ra` preservation.
  5. **Function Return:** Tracing `ret` (`jalr x0, ra, 0`) back to the caller's control flow.
  6. **ABI Invariants:** Inspecting computed return values in `a0` and confirming `sp` restoration.

---

## Quickstart for Students

To run this sample mission in interactive classroom mode:

```bash
# Launch the sample mission (runs in TUI mode with classroom guidance enabled)
simrv --mission examples/isa/lessons/control-flow-calls.mission -m examples/isa/build-rv64/control-flow-calls.elf
```

### Student Controls Inside the Mission

- **Step Execution:** Press `F6` (or `s`) to step forward one instruction.
- **Run / Pause:** Press `F5` (or `c`) to run or pause simulation.
- **Follow Guidance:** Press `Enter` to follow the suggested action in the bottom Guidance drawer.
- **Toggle Guide:** Press `g` to show or hide the Guidance drawer.
- **Dismiss Mission:** Press `z` to dismiss the mission and freely explore the simulator.
- **Restart Mission:** Press `Z` (`Shift-Z`) to restart the mission from step 0.
- **Architecture Glossary:** Press `F9` (or `?`) to view detailed RISC-V architectural explanations.

---

## Quickstart for Instructors

Instructors can use this sample mission as a template for authoring custom course assignments:

1. **Source Code Checkpoints:** Place symbolic labels (`.global mission_label`) at didactic milestones in your assembly or C code.
2. **Compile with Debug Symbols:** Compile with `-g` so symbols are preserved in the ELF symbol table.
3. **Mission Configuration:** Write a `.mission` file pairing each symbolic label with student prompts, architectural rationale (`why`), and target workbench views (`view = "explain" | "registers" | "stack" | "trace"`).
4. **Validation:** Review the [Instructor Guide in the SimRV Documentation](../../../docs/user/classroom.md#5-instructor-guide-authoring--deploying-classroom-missions) for formatting and submission-checking details.
