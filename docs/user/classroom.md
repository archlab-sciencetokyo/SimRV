# RISC-V Student Reference Manual & SimRV Educational Guide

This document serves as a comprehensive reference manual for students learning
RISC-V computer architecture (specifically RV32GC / RV64GC) and utilizing the
educational features of **SimRV**.

---

## 1. Register Files and ABI Conventions

RISC-V defines a clear mapping between raw physical registers and their symbolic
Application Binary Interface (ABI) names. Understanding this mapping is essential
for writing and debugging assembly code.

### General-Purpose Registers (GPRs)

RISC-V has 32 general-purpose registers (`x0` through `x31`). `x0` is hardwired
to zero; the others are general read/write registers. In RV32 each register holds
a 32-bit value; in RV64 each holds a 64-bit value.

| Register | ABI Name | Role / Description | Saver |
| :--- | :--- | :--- | :--- |
| **`x0`** | `zero` | Hardwired to zero (writes ignored, reads always return 0) | — |
| **`x1`** | `ra` | Return Address — stores the link address for function calls | Caller |
| **`x2`** | `sp` | Stack Pointer — points to the current top of the stack | Callee |
| **`x3`** | `gp` | Global Pointer — points to global/static variables | — |
| **`x4`** | `tp` | Thread Pointer — holds thread-local storage pointers | — |
| **`x5`** | `t0` | Temporary Register 0 | Caller |
| **`x6` – `x7`** | `t1` – `t2` | Temporary Registers 1 and 2 | Caller |
| **`x8`** | `s0` / `fp` | Saved Register 0 / Frame Pointer | Callee |
| **`x9`** | `s1` | Saved Register 1 | Callee |
| **`x10` – `x11`** | `a0` – `a1` | Function Arguments 0–1 / Return Values 0–1 | Caller |
| **`x12` – `x17`** | `a2` – `a7` | Function Arguments 2–7 | Caller |
| **`x18` – `x27`** | `s2` – `s11` | Saved Registers 2–11 | Callee |
| **`x28` – `x31`** | `t3` – `t6` | Temporary Registers 3–6 | Caller |

> [!NOTE]
>
> - **Caller-saved** registers (`ra`, `t0`–`t6`, `a0`–`a7`) can be overwritten by
>   a called function. The caller must save them on the stack before any call if
>   they are needed afterward.
> - **Callee-saved** registers (`sp`, `s0`–`s11`) must be preserved by a called
>   function. If the callee modifies them, it must restore their original values
>   before returning.

### Floating-Point Registers (FPRs)

When the Single (F) or Double (D) precision extensions are enabled, RISC-V
provides 32 floating-point registers (`f0` through `f31`).

| Register | ABI Name | Role / Description | Saver |
| :--- | :--- | :--- | :--- |
| **`f0` – `f7`** | `ft0` – `ft7` | FP Temporaries 0–7 | Caller |
| **`f8` – `f9`** | `fs0` – `fs1` | FP Saved Registers 0–1 | Callee |
| **`f10` – `f11`** | `fa0` – `fa1` | FP Arguments 0–1 / Return Values 0–1 | Caller |
| **`f12` – `f17`** | `fa2` – `fa7` | FP Arguments 2–7 | Caller |
| **`f18` – `f27`** | `fs2` – `fs11` | FP Saved Registers 2–11 | Callee |
| **`f28` – `f31`** | `ft8` – `ft11` | FP Temporaries 8–11 | Caller |

---

## 2. Instruction Formats and Split Immediates

RISC-V features a structured instruction encoding designed to simplify hardware
decode logic. There are 6 base instruction formats (R, I, S, B, U, J) plus
the R4 format used by fused floating-point operations, and a family of
compressed (16-bit) formats under the C extension.

```text
R-Type:  | funct7 (7b) | rs2 (5b) | rs1 (5b) | funct3 (3b) | rd (5b) | opcode (7b) |
I-Type:  |         immediate [11:0] (12b)      | rs1 | funct3 | rd | opcode |
S-Type:  | imm[11:5]   | rs2      | rs1      | funct3 | imm[4:0]    | opcode |
B-Type:  | imm[12|10:5]| rs2      | rs1      | funct3 | imm[4:1|11] | opcode |
U-Type:  |               immediate [31:12] (20b)              | rd | opcode |
J-Type:  | imm[20|10:1|11|19:12] (20b)                        | rd | opcode |
R4-Type: | rs3 (5b) | fmt (2b) | rs2 | rs1 | funct3 | rd | opcode |
```

### The Engineering Rationale Behind Split Immediates

In `S` (Store) and `B` (Branch) formats the immediate field is split across
non-contiguous bit positions. This is a deliberate hardware engineering decision:

1. **Alignment of Register Specifiers:** In **all** formats, `rs1` (bits 19–15),
   `rs2` (bits 24–20), and `rd` (bits 11–7) sit in the **exact same positions**.
2. **Direct Hardware Routing:** Because register specifiers never shift, the
   hardware decoder can wire instruction bits directly to the register file address
   inputs — no multiplexers needed on the register read ports.
3. **Speed and Power:** Eliminating those multiplexers removes gate delays on the
   critical path, enabling higher clock frequencies and lower power. The cost is a
   trivial software overhead in the assembler (done once), yielding permanent
   hardware gains.

---

## 3. Compilation and Execution Guide

SimRV loads ELF executables directly, including loadable segments, BSS, entry-point, and symbol
information. It also accepts raw flat binaries. See the
[bare-metal guide](baremetal.md) for toolchain, startup, linker-script, C, and assembly setup;
the reusable programs under [`examples/isa/`](https://github.com/archlab-sciencetokyo/SimRV/tree/dev/examples/isa/) provide compact starting points.

```bash
# Student-facing mode: start paused with the interactive Student Guide visible
./build/rv64-release/simrv --tui --baremetal -m program.elf --class

# Headless / CLI-only mode (fast execution)
./build/rv64-release/simrv --cli --baremetal -m program.elf --mode fast

# Configure a portable report destination, then press x while paused
./build/rv64-release/simrv --tui --baremetal -m program.elf --class \
    --inspection-output inspection.json
```

### Teacher-friendly interaction points

- Step through execution until the first branch, then inspect the instruction mnemonic, raw hex
  encoding, and branch target in the Student Guide.
- Ask students to determine which architectural register should change before advancing past an
  ALU instruction, then verify the change in the Register File.
- Introduce memory operations: observe when memory accesses read or write guest state and how the
  bus activity panel reflects peripheral transactions.
- Use `--inspection-output <path>` to capture machine state at key execution milestones;
  students can submit these JSON snapshots as lab evidence. A reference script is provided in
  `scripts/` to validate student inspection files against a golden run without relying on an ad-hoc
  classroom command.

Guest programs can write to the 16550A UART to place prompts, intermediate values, or questions in
the TUI virtual terminal. The [`uart-output.S`](https://github.com/archlab-sciencetokyo/SimRV/blob/dev/examples/isa/uart-output.S) example is a minimal
source-only implementation; [`uart-input-echo.S`](https://github.com/archlab-sciencetokyo/SimRV/blob/dev/examples/isa/uart-input-echo.S) also accepts
student input. The example catalog additionally covers load widths, loops, M/A/C/D extensions, and
architectural counters. Build all of them with `make -C examples/isa XLEN=64` or `XLEN=32`.

---

## 4. Using the Educational Explainer Utility

SimRV provides two interfaces for interactive instruction decoding and explanation:
the `--explain-inst` CLI flag and the interactive TUI EXPLAIN pane.

### Command Line Interface (CLI)

Use `--explain-inst <HEX>` to disassemble, decode, and print the step-by-step
reconstruction of any instruction hex value.

#### Example: Explaining an ADD instruction

```bash
./build/rv32-release/simrv --explain-inst 0x00B502B3
```

**Output:**

```text
=== SimRV Educational Instruction Explainer ===

Standard 32-bit Instruction Word:
  Hex Value: 0x00B502B3
  Binary   : 00000000101101010000001010110011
--------------------------------------------------------------------------------
Instruction Format: R-Type (Register-Register)
ISA Extension: RV32I / RV64I Base Integer

Visual Bit Fields Breakdown (R-Type format):
  31          25 24      20 19      15 14  12 11        7 6           0
  +------------+----------+----------+----+----------+-------------+
  |   funct7   |   rs2    |   rs1    | f3 |    rd    |   opcode    |
  +------------+----------+----------+----+----------+-------------+
  |   0000000  |  01011   |  01010   | 000 |  00101   |   0110011   |
  +------------+----------+----------+----+----------+-------------+

Field Decoded Meanings:
  opcode  : 0x33 (0110011) -> Major Opcode
  rd      : x5 (00101) -> Destination Register: x5 (t0)
  funct3  : 0x0  (000) -> Sub-function selector
  rs1     : x10 (01010) -> Source Register 1: x10 (a0)
  rs2     : x11 (01011) -> Source Register 2: x11 (a1)
  funct7  : 0x00 (0000000) -> Operations modifier
--------------------------------------------------------------------------------
Decoded Instruction Detail:
  Assembly Mnemonic: ADD
  Assembly Rep     : # add t0, a0, a1

Description (Behavior):
  Add. [RV32I/RV64I] Adds the values in rs1 and rs2 and stores the result in rd.

=================================================
```

### Interactive TUI Mode

1. Run your binary on SimRV:

   ```bash
   ./build/rv32-release/simrv --tui --baremetal -m program.elf --class
   ```

2. The simulation starts paused (`[PAUSED]`). Press `c` to unpause and run continuously, or press `s` / `Space` to single-step instructions.
3. Press `l` to cycle tool views, `r` to cycle register views, or `e` to open the instruction explainer.
4. The EXPLAIN pane displays:
   - Current PC and symbolic function name.
   - Instruction hex value and disassembled mnemonic.
   - Visual bit-field layout grid identifying opcode, register specifiers, and immediate encodings.
   - Architectural values before and after execution.
   - Architectural dataflow and, in cycle-accurate mode, microarchitectural effects and hazards.
5. The **Student Guide** explains the active view and proposes a context-sensitive next action.
   Press `Enter` to perform that action, `g` to show or hide the guide, or `?` to open the glossary
   topic associated with the active inspector.

### TUI Keybindings Reference

| Key | Context | Action |
| :--- | :--- | :--- |
| `F1` / `h` | All / Paused | Open Help & Keybindings reference modal |
| `F2` / `,` / `Alt-S` | Paused | Open Simulator Settings modal (Mode, SMP, Scheduler, Diagnostics) |
| `F3` / `r` / `Alt-R` | Paused | Cycle register tabs (GPR / FPR / VEC / CSR) |
| `F4` | All | Open Workbench Layout Presets modal |
| `F5` / `c` / `Ctrl-P` | Paused / Running | Run / Pause simulation loop |
| `F6` / `s` / `Space` | Paused | Single-step one instruction |
| `F7` | Paused | Switch active inspector to Trace & Execution page |
| `F8` / `k` | Paused | Toggle PC breakpoint at current PC |
| `F9` / `?` | Paused | Open Architecture Glossary modal at active topic |
| `F10` / `q` / `Ctrl-Q` | All | Cleanly terminate simulation |
| `Alt-1` .. `Alt-4` | All | Apply Workbench Layout Presets directly (1..4) |
| `l` / `Alt-L` | Paused | Cycle left inspector tool tabs (Pipe / Cache / BP / Hazard / TLB / Bus / IO / Stats) |
| `o` / `Alt-O` | Paused | Open Binary & Disk Image Loader modal |
| `Alt-M` | Paused | Open MISA CSR / Extensions Configuration modal |
| `y` | Paused | Open Cycle-Accurate Microarchitecture & Cache Config modal |
| `i` | Paused | Open Memory Inspector modal |
| `m` | Paused | Open Breakpoints & Watchpoints Management modal |
| `g` | Paused | Show / hide the interactive Student Guide |
| `Enter` | Paused, guide visible | Perform the Student Guide's suggested action |
| `e` | Paused | Open / close the current instruction explanation |
| `x` | Paused | Export the configured inspection report |
| `Tab` | All | Focus next workbench column / right pane |
| `Ctrl-R` | All | Soft-reboot guest simulation |

---

## 5. Instructor Guide: Authoring & Deploying Classroom Missions

SimRV provides a dedicated **Mission Framework** for instructors teaching computer architecture and systems programming. Classroom missions allow instructors to create interactive, checkpoint-based lab assignments where students are guided through assembly and microarchitectural concepts inside the TUI workbench.

### Design Principles

- **Zero-Telemetry & Privacy-Preserving:** Missions collect no student telemetry, personal data, or network activity. Everything executes entirely on the student's local machine.
- **Scaffolded Guided Exploration:** Missions automatically focus relevant workbench views (Registers, Stack, Explainer, or Trace) as students advance through key program checkpoints.
- **Non-Locking:** Students retain full control to step, inspect, open modals, or pause at any point. Students can dismiss a mission with `z` to explore freely or restart it with `Z`.

### Classroom Command Line Invocations

To launch a mission, invoke SimRV with the `--class` (classroom guidance) and `--mission` options:

```bash
# Student invocation: launch interactive mission with matching ELF
simrv --tui --class --mission lessons/control-flow-calls.mission -m control-flow-calls.elf

# Verification / non-interactive check
simrv --cli -m control-flow-calls.elf --mode fast
```

> [!TIP]
> TUI mode is SimRV's default execution interface, so passing `--mission <file>` is sufficient: SimRV will automatically start in TUI mode and enable student classroom guidance (`--class`). Running `--mission` with headless `--cli` is rejected.

### Mission File Specification (`.mission`)

Missions are simple, declarative configuration files following the version 1 format.

#### Top-level `[mission]` Header

```toml
# SimRV classroom mission format, version 1
[mission]
version = "1"
id = "control-flow-and-abi"
program = "control-flow-calls.elf"
```

- `version`: Must be `"1"`.
- `id`: An identifier for the assignment or lesson.
- `program`: The exact filename of the ELF binary expected for this mission (e.g. `lab1.elf`). SimRV validates the loaded binary against this name and displays a helpful error if a mismatched binary is loaded.

#### Checkpoint Steps (`[step]`)

Each mission consists of one or more sequential `[step]` blocks:

```toml
[step]
label = "mission_branch"
title = "Conditional Branching"
prompt = "Predict whether bne branches to target, then single-step [F6]."
why = "Branches compare register values and update PC using a signed immediate offset."
view = "explain"
glossary = "0"
```

| Field | Type | Description |
| :--- | :--- | :--- |
| `label` | string | Exact symbol name in the ELF symbol table where this checkpoint triggers (e.g., a function label or milestone tag). |
| `title` | string | Short title rendered in the student guidance header. |
| `prompt` | string | Actionable prompt directing the student what to observe, predict, or calculate. |
| `why` | string | Pedagogical explanation articulating *why* the architectural behavior occurs. |
| `view` | string | Workbench view to focus automatically: `"explain"`, `"registers"`, `"stack"`, or `"trace"`. |
| `glossary` | string (int) | (Optional) Topic index in the Architecture Glossary (`[F9]`) relevant to this concept. |

### How Missions Progress in Class

1. **Symbolic Checkpoints:** SimRV monitors retired instructions and PC targets. When the program reaches the exact address of `label`, the mission advances to that step.
2. **Ordered Progression:** Checkpoints must be encountered in sequence. Out-of-order execution (such as multiple loop iterations) will not trigger future steps prematurely.
3. **Automatic Context Switching:** When advancing to a step, SimRV automatically switches the focused workbench panel to the `view` specified in the step (e.g., opening the Stack pane when inspecting callee frames).
4. **Completion:** When the final checkpoint is visited, the status bar displays ` MISSION COMPLETE `, and the guidance drawer provides summary review guidance.

### Student Keyboard Controls During a Mission

| Key | Action |
| :--- | :--- |
| **`Enter`** | Perform the active suggestion (e.g., focus recommended tab or step forward). |
| **`g`** | Show / hide the student guidance drawer. |
| **`z`** | Dismiss the active mission (returns to unconstrained free simulation). |
| **`Z`** (Shift-Z) | Restart the mission from step 0. |

---

### Step-by-Step Instructor Tutorial: Creating a Lab Assignment

Here is how an instructor can author a complete lab assignment:

#### Step 1: Write the Lab Assembly Code with Symbolic Checkpoints

In your assembly code (`lab1_abi.S`), insert symbolic labels where you want students to pause and observe state:

```assembly
    .global _start
    .global mission_branch
    .global mission_call
    .global mission_done
    .text

_start:
    li a0, 5
    li a1, 10

mission_branch:
    # Checkpoint 1: Students observe condition before branch
    beq a0, a1, skip_branch
    addi a0, a0, 1

mission_call:
    # Checkpoint 2: Students observe ra and sp before function call
    call helper_func

mission_done:
    # Checkpoint 3: Students inspect return value in a0
    wfi

helper_func:
    addi sp, sp, -16
    sd ra, 8(sp)
    add a0, a0, a1
    ld ra, 8(sp)
    addi sp, sp, 16
    ret
```

#### Step 2: Compile with Debug Symbols

Compile the assembly with `-g` so labels are retained in the ELF symbol table:

```bash
riscv64-unknown-elf-gcc -nostdlib -g -Ttext=0x80000000 lab1_abi.S -o lab1_abi.elf
```

#### Step 3: Write the `.mission` File

Create `lab1_abi.mission`:

```toml
[mission]
version = "1"
id = "lab1-abi-functions"
program = "lab1_abi.elf"

[step]
label = "mission_branch"
title = "Branch Evaluation"
prompt = "Check a0 and a1 in Registers [F3]; determine if beq will branch."
why = "Branch decisions depend solely on register operand equality."
view = "explain"
glossary = "0"

[step]
label = "mission_call"
title = "Function Call Setup"
prompt = "Inspect ra and sp before the call instruction executes."
why = "The call pseudo-instruction (jal) overwrites ra with the return address."
view = "registers"
glossary = "0"

[step]
label = "mission_done"
title = "Function Return & ABI Contract"
prompt = "Observe the computed return value in a0 and verify sp was restored."
why = "Callee-saved registers like sp must be restored before returning to the caller."
view = "registers"
glossary = "0"
```

#### Step 4: Grading & Verification Without Surveillance

Instructors can verify student lab completion through portable inspection reports:

```bash
# Student generates an inspection report at the final milestone by pressing 'x':
simrv --tui --class --mission lab1_abi.mission -m lab1_abi.elf --inspection-output student_submission.json
```

Instructors can then validate student submissions using Python by comparing `student_submission.json` against a golden reference run:

```bash
# Verify architectural state (PC, registers, instructions retired) matches expectation
python3 scripts/validate_submission.py --golden golden_inspection.json --student student_submission.json
```
