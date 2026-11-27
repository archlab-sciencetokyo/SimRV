# SimRV Classroom Sample Missions

This directory contains editable educational classroom missions for computer architecture and
assembly courses. Packages include the mission and source files, but omit generated build
directories and ELF binaries.

## Available Missions

### SimRV Debug Tour (`simrv-debug-tour.mission`)

- **Target Program:** `simrv-debug-tour.elf`
- **Source Code:** [`examples/isa/simrv-debug-tour.S`](../simrv-debug-tour.S)
- **Build target:** `simrv-debug-tour.elf` (build with the example Makefile)
- **Concepts Covered:** Reading the PC, stepping through a branch, watching a register change,
  setting a symbol breakpoint, and diagnosing an intentional wrong result.
- **Student quickstart:** [`simrv-debug-tour-student-readme.md`](simrv-debug-tour-student-readme.md)
- **Labmate test sheet:** [`simrv-debug-tour-test.md`](simrv-debug-tour-test.md)
- **RV32IMA build:** `make -C examples/isa XLEN=32 ARCH=rv32ima ABI=ilp32`

### 1. Control Flow & ABI Function Calls (`control-flow-calls.mission`)

- **Target Program:** `control-flow-calls.elf`
- **Source Code:** [`examples/isa/control-flow-calls.S`](../control-flow-calls.S)
- **Build target:** `control-flow-calls.elf` (build with the example Makefile)
- **Concepts Covered:**
  1. **Conditional Branching:** Evaluating `bne` branch condition based on register operands.
  2. **Loop Control:** Following backward loop branches in the Trace inspector.
  3. **Function Call Setup:** Understanding caller-saved argument registers (`a0`, `a1`), link register (`ra`), and stack pointer (`sp`).
  4. **Callee Stack Frame:** Observing callee stack allocation (`addi sp, sp, -16`) and `ra` preservation.
  5. **Function Return:** Tracing `ret` (`jalr x0, ra, 0`) back to the caller's control flow.
  6. **ABI Invariants:** Inspecting computed return values in `a0` and confirming `sp` restoration.

---

## Quickstart for Students

From a source checkout, build the sample and run it in interactive classroom mode:

```bash
make -C examples/isa XLEN=64
simrv --mission examples/isa/lessons/control-flow-calls.mission \
  -m /tmp/simrv-isa-rv64/control-flow-calls.elf
```

The source package installs examples under `/usr/share/doc/SimRV/examples/isa`. Copy that
directory to a writable location, build it with a RISC-V ELF toolchain, and point `--mission` and
`-m` at the copied files. SimRV packages do not include a guest compiler or generated guest ELF.

### Student Controls Inside the Mission

- **Step execution:** Press `F6` (or `S`) to execute one guest instruction and inspect its effect.
- **Run to a stop:** Press `F5` (or `C`) to run until the next active breakpoint or mission checkpoint. It can pass over guest instructions; use `F6` to inspect them one at a time.
- **Lesson Tasks:** Press `F12` to open or reopen the current task, goal, hint, and checkpoint status.
- **Hint and rationale:** Press `Y` to cycle through hidden, hint, and full explanation.
- **Next lesson step:** Press `Space` in Tasks or `N` from the workbench. This advances teaching content, not guest execution or checkpoint evidence.
- **Dismiss mission:** Press `z` to dismiss the mission and explore freely.
- **Restart mission:** Press `Z` (`Shift-Z`) to restart the guest and lesson from step 1.
- **Architecture Glossary:** Press `F9` (or `?`) to view detailed RISC-V architectural explanations.

---

## Quickstart for Instructors

Instructors can use this sample mission as a template for authoring custom course assignments:

1. **Source Code Checkpoints:** Place symbolic labels (`.global mission_label`) at didactic milestones in your assembly or C code.
2. **Compile with Debug Symbols:** Compile with `-g` so symbols are preserved in the ELF symbol table.
3. **Mission Configuration:** Write a `.mission` file pairing each symbolic label with student prompts, architectural rationale (`why`), and target workbench views (`view = "explain" | "registers" | "stack" | "trace"`).
4. **Validation:** Review the [Instructor Guide in the SimRV Documentation](../../../docs/user/classroom.md#5-instructor-guide-authoring--deploying-classroom-missions) for formatting and submission-checking details.
