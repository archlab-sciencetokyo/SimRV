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

To run custom assembly or C programs on SimRV, compile them to a raw flat binary
image and load it with `-m`.

### Step 1: Write Your Code

#### Example Assembly (`add.S`)
```assembly
.global _start
.section .text

_start:
    li a0, 5        # Load immediate 5 into a0 (x10)
    li a1, 10       # Load immediate 10 into a1 (x11)
    add a2, a0, a1  # Add a0 and a1, store result in a2 (x12)

loop:
    j loop          # Infinite loop — pause here for TUI inspection
```

#### Example C Code (`main.c`)
```c
void _start() {
    int a = 5;
    int b = 10;
    volatile int c = a + b;
    while (1);
}
```

### Step 2: Compile to ELF

Use the GNU Toolchain or Clang. Specify `-march=rv32gc -mabi=ilp32` for RV32,
or `-march=rv64gc -mabi=lp64d` for RV64.

```bash
# RV32GC assembly
riscv64-unknown-elf-gcc -march=rv32gc -mabi=ilp32 \
    -static -nostdlib -Ttext 0x80000000 \
    -o program.elf add.S

# RV64GC assembly
riscv64-unknown-elf-gcc -march=rv64gc -mabi=lp64d \
    -static -nostdlib -Ttext 0x80000000 \
    -o program.elf add.S
```

- `-Ttext 0x80000000`: Sets the entry point to `0x80000000` (SimRV's DRAM base).
- `-nostdlib`: Skips standard startup libs — not present in bare-metal simulation.

### Step 3: Extract Raw Flat Binary

SimRV loads raw memory images, not ELF. Convert with `objcopy`:

```bash
riscv64-unknown-elf-objcopy -O binary program.elf program.bin
```

### Step 4: Run on SimRV

SimRV 2.0 launches in interactive visual TUI mode by default:

```bash
# RV32 build (Bare-metal mode)
./build/rv32-release/SimRV -b -m program.bin

# RV64 build (Bare-metal mode)
./build/rv64-release/SimRV -b -m program.bin

# Headless / CLI-only mode
./build/rv64-release/SimRV -b -m program.bin -c
```

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
   ./build/rv32-release/SimRV -b -m program.bin
   ```
2. The simulation starts paused (`[PAUSED]`). Press `c` to unpause and run continuously, or press `s` / `Space` to single-step instructions.
3. Press `e` to switch directly to the **EXPLAIN** pane, or press `r` to cycle through left pane views (GPRs → FPRs → Pipeline → Cache → TLB → Breakpoints → Hazards → I/O → Stats → Stack → Explain).
4. The EXPLAIN pane displays:
   - Current PC and symbolic function name.
   - Instruction hex value and disassembled mnemonic.
   - Visual bit-field layout grid identifying opcode, register specifiers, and immediate encodings.
   - Architectural values before and after execution.
   - Educational prose explaining microarchitectural effects and hazards.
5. Press `g` to toggle the compact **Guided Inspection** assistant ribbon for contextual advice.

### TUI Keybindings Reference (SimRV 2.0)

| Key | Context | Action |
|:---|:---|:---|
| `c` | Paused | Unpause / Continue continuous simulation |
| `p` / `Ctrl-P` | Running | Pause execution and enter interactive inspection mode |
| `s` / `Space` | Paused | Single-step one instruction |
| `e` | Paused | Jump directly to EXPLAIN inspection view |
| `r` | Paused | Cycle left panel view (GPR, FPR, Pipeline, Cache, TLB, BP, Hazard, IO, Stats, Stack, Explain) |
| `Tab` / `Shift-Tab` | All | Switch between right pane tabs (PTY, Display, Stats, Logs) |
| `g` | Paused | Toggle Guided Inspection ribbon |
| `b` | Paused | Open Breakpoint Management modal |
| `m` | Paused | Open MISA / Extension Configuration modal |
| `?` | Paused | Open Help & Keybindings reference modal |
| `q` / `Ctrl-Q` | All | Cleanly terminate simulation |
| `Ctrl-R` | All | Soft-reboot guest simulation |
