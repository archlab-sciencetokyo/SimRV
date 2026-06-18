# RISC-V Student Reference Manual & SimRV Educational Guide

This document serves as a comprehensive reference manual for students learning the RISC-V computer architecture (specifically RV32GC / RV64GC) and utilizing the educational features of **SimRV**.

---

## 1. Register Files and ABI Conventions

RISC-V defines a clear mapping between raw physical registers and their symbolic Application Binary Interface (ABI) names. Understanding this mapping is essential for writing and debugging assembly code.

### General-Purpose Registers (GPRs)

RISC-V has 32 general-purpose registers (`x0` through `x31`). `x0` is hardwired to zero, while the others are general read/write registers.

| Register | ABI Name | Role / Description | Saver |
|:---|:---|:---|:---|
| **`x0`** | `zero` | Hardwired to zero (writes are ignored, reads always return 0) | — |
| **`x1`** | `ra` | Return Address (stores the link address for function calls) | Caller |
| **`x2`** | `sp` | Stack Pointer (points to the current top of the stack) | Callee |
| **`x3`** | `gp` | Global Pointer (points to global/static variables) | — |
| **`x4`** | `tp` | Thread Pointer (holds thread-local storage pointers) | — |
| **`x5`** | `t0` | Temporary Register 0 | Caller |
| **`x6` - `x7`** | `t1` - `t2` | Temporary Registers 1 and 2 | Caller |
| **`x8`** | `s0` / `fp` | Saved Register 0 / Frame Pointer (points to base of stack frame) | Callee |
| **`x9`** | `s1` | Saved Register 1 | Callee |
| **`x10` - `x11`** | `a0` - `a1` | Function Arguments 0 and 1 / Return Values 0 and 1 | Caller |
| **`x12` - `x17`** | `a2` - `a7` | Function Arguments 2 through 7 | Caller |
| **`x18` - `x27`** | `s2` - `s11` | Saved Registers 2 through 11 | Callee |
| **`x28` - `x31`** | `t3` - `t6` | Temporary Registers 3 through 6 | Caller |

> [!NOTE]
> * **Caller-saved** registers (`ra`, `t0`-`t6`, `a0`-`a7`) can be overwritten by a called function. If the caller needs their values after a function call, it must save them on the stack before the call.
> * **Callee-saved** registers (`sp`, `s0`-`s11`) must be preserved by a called function. If the callee modifies them, it must restore their original values before returning.

### Floating-Point Registers (FPRs)

If the Single (F) or Double (D) precision floating-point extensions are enabled, RISC-V provides 32 floating-point registers (`f0` through `f31`).

| Register | ABI Name | Role / Description | Saver |
|:---|:---|:---|:---|
| **`f0` - `f7`** | `ft0` - `ft7` | Floating-point Temporaries 0 through 7 | Caller |
| **`f8` - `f9`** | `fs0` - `fs1` | Floating-point Saved Registers 0 and 1 | Callee |
| **`f10` - `f11`** | `fa0` - `fa1` | FP Arguments 0 and 1 / Return Values 0 and 1 | Caller |
| **`f12` - `f17`** | `fa2` - `fa7` | FP Arguments 2 through 7 | Caller |
| **`f18` - `f27`** | `fs2` - `fs11` | Floating-point Saved Registers 2 through 11 | Callee |
| **`f28` - `f31`** | `ft8` - `ft11` | Floating-point Temporaries 8 through 11 | Caller |

---

## 2. Instruction Formats and Split Immediates

RISC-V features a highly structured instruction encoding designed to simplify hardware decode logic. There are 6 base instruction formats (R, I, S, B, U, J) and 1 floating-point fused format (R4).

```
R-Type:  | funct7 (7b) | rs2 (5b) | rs1 (5b) | funct3 (3b) | rd (5b) | opcode (7b) |
I-Type:  |          immediate [11:0] (12b)  | rs1 | funct3 | rd | opcode |
S-Type:  | imm[11:5]   | rs2      | rs1      | funct3 | imm[4:0]    | opcode |
B-Type:  | imm[12|10:5]| rs2      | rs1      | funct3 | imm[4:1|11] | opcode |
U-Type:  |               immediate [31:12] (20b)              | rd | opcode |
J-Type:  | imm[20|10:1|11|19:12] (20b)                        | rd | opcode |
R4-Type: | rs3 (5b) | fmt (2b) | rs2 | rs1 | funct3 | rd | opcode |
```

### The Engineering Rationale Behind Split Immediates

In formats like `S` (Store) and `B` (Branch), the immediate field is split across different bit positions in the instruction word rather than being contiguous. This seems complex, but it is a brilliant engineering decision for hardware efficiency:

1. **Alignment of Register Sources/Destinations:**
   Notice that in **all** instruction formats, the source registers (`rs1` at `[19:15]` and `rs2` at `[24:20]`) and the destination register (`rd` at `[11:7]`) remain in the **exact same bit positions**.
2. **Direct Hardware Routing:**
   Because the register specifiers never shift positions, the hardware decoder can route the instruction bits `[19:15]`, `[24:20]`, and `[11:7]` directly to the address inputs of the Register File. No multiplexers are needed to select which bits define the registers.
3. **Speed and Power Optimization:**
   Avoiding multiplexers on the register read ports eliminates gate delays on the processor's critical path, allowing the CPU to achieve higher clock frequencies and consume less power. The cost is a slight overhead in compiling/assembling (which is done once in software), but it yields a permanent performance boost in the physical silicon.

---

## 3. Compilation and Execution Guide

To run custom assembly or C programs on SimRV, you must compile them into a raw flat binary format.

### Step 1: Write Your Code

#### Example Assembly (`add.S`)
```assembly
.global _start
.section .text

_start:
    li a0, 5        # Load immediate 5 into a0 (x10)
    li a1, 10       # Load immediate 10 into a1 (x11)
    add a2, a0, a1  # Add a0 and a1, store result in a2 (x12)
    
    # Infinite loop to pause simulator inspection
loop:
    j loop
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

Use the GNU Toolchain (`riscv64-unknown-elf-gcc` or `riscv32-unknown-elf-gcc`) or `clang`. You must specify the 32-bit architecture (`rv32gc`) and ABI (`ilp32`) for RV32 compatibility.

```bash
# Compile assembly
riscv64-unknown-elf-gcc -march=rv32gc -mabi=ilp32 -static -nostdlib -Ttext 0x80000000 -o program.elf add.S

# Compile C code
riscv64-unknown-elf-gcc -march=rv32gc -mabi=ilp32 -static -nostdlib -Ttext 0x80000000 -o program.elf main.c
```
* `-Ttext 0x80000000`: Sets the entry point and base code segment address to `0x80000000`.
* `-nostdlib`: Prevents inclusion of the standard system startup libraries, which are not present in raw baremetal simulation.

### Step 3: Extract Raw Flat Binary

SimRV loads raw memory images directly into memory. Convert your compiled ELF executable into a flat binary file:

```bash
riscv64-unknown-elf-objcopy -O binary program.elf program.bin
```

### Step 4: Run on SimRV

```bash
./build/rv32-release/SimRV -m program.bin
```

---

## 4. Using the Educational Explainer Utility

SimRV provides both a CLI interface and an interactive TUI visualization to explain instruction decoding.

### Command Line Interface (CLI)

Use the `--explain-inst <HEX>` option to disassemble, decode, and print the step-by-step immediate reconstruction of any instruction hex value.

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
  Add. Adds the values in rs1 and rs2 and stores the result in rd.

=================================================
```

### Interactive TUI Mode

1. Run a binary in TUI mode:
   ```bash
   ./build/rv32-release/SimRV -m program.bin
   ```
2. Pause the simulation by pressing `Ctrl-P` (or clicking the mouse if supported).
3. Press `e` or `E` to toggle the left Register Pane to the **EXPLAIN** page.
4. The pane will display:
   * Current PC and its symbolic name (if symbol table loaded).
   * Instruction Hex value (handling decompression if 16-bit).
   * Decoded assembly instruction.
   * Visual layout grid highlighting opcode, registers, and sub-fields.
   * Decoded field values (such as source/destination register name + current architectural value).
   * A short educational description of what the instruction does.
5. Step the simulator using `s` (Single Step) or `Space` to watch the next instruction get decoded and explained in real-time.
