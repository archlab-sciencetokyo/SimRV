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
|:---|:---|:---|:---|
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
|:---|:---|:---|:---|
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

```
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
[bare-metal guide](BAREMETAL_GUIDE.md) for toolchain, startup, linker-script, C, and assembly setup;
the reusable programs under [`examples/isa/`](../examples/isa/) provide compact starting points.

```bash
# Student-facing mode: start paused with the interactive Student Guide visible
./build/rv64-release/SimRV --tui --baremetal -m program.elf --class

# Headless / CLI-only mode (fast execution)
./build/rv64-release/SimRV --cli --baremetal -m program.elf --mode fast

# Configure a portable report destination, then press x while paused
./build/rv64-release/SimRV --tui --baremetal -m program.elf --class \
    --inspection-output inspection.json
```

### Teacher-friendly interaction points

ELF symbols make planned pauses possible without a SimRV-specific source format. Add a global label
at a useful observation point, press `:` in the paused TUI, and enter the label name; continuing then
pauses before that address. `ebreak` retains its architectural meaning and raises a breakpoint
exception, so it remains suitable for trap/debugger exercises rather than acting as a hidden
classroom command.

Guest programs can write to the 16550A UART to place prompts, intermediate values, or questions in
the TUI virtual terminal. The [`uart-output.S`](../examples/isa/uart-output.S) example is a minimal
source-only implementation; [`uart-input-echo.S`](../examples/isa/uart-input-echo.S) also accepts
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
./build/rv32-release/SimRV --explain-inst 0x00B502B3
```

**Output:**
```
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
   ./build/rv32-release/SimRV --tui --baremetal -m program.elf --class
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
|:---|:---|:---|
| `c` / `Ctrl-P` | Paused / Running | Run / Pause simulation loop |
| `s` / `Space` | Paused | Single-step one instruction |
| `l` / `Alt-L` | Paused | Cycle left inspector tool tabs (Pipe / Cache / BP / Hazard / TLB / Bus / IO / Stats) |
| `r` / `Alt-R` | Paused | Cycle register tabs (GPR / FPR / VEC / CSR) |
| `o` / `Alt-O` | Paused | Open Binary & Disk Image Loader modal |
| `,` / `Alt-S` | Paused | Open Simulator Settings modal (Mode, SMP, Scheduler, Diagnostics) |
| `Alt-M` | Paused | Open MISA CSR / Extensions Configuration modal |
| `y` | Paused | Open Cycle-Accurate Microarchitecture & Cache Config modal |
| `i` | Paused | Open Memory Inspector modal |
| `m` | Paused | Open Breakpoints & Watchpoints Management modal |
| `g` | Paused | Show / hide the interactive Student Guide |
| `Enter` | Paused, guide visible | Perform the Student Guide's suggested action |
| `e` | Paused | Open / close the current instruction explanation |
| `?` | Paused | Open the glossary at the active inspector's topic |
| `x` | Paused | Export the configured inspection report |
| `Ctrl-A` | All | Toggle input focus between guest UART/PTY and TUI controls |
| `Tab` | All | Switch between right pane views (Guest Terminal / Log Buffer) |
| `F1` / `h` | Paused | Open Help & Keybindings reference modal |
| `q` / `Ctrl-Q` | All | Cleanly terminate simulation |
| `Ctrl-R` | All | Soft-reboot guest simulation |
