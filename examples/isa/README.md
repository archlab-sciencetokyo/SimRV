# SimRV ISA Examples

These small, source-first programs demonstrate individual RISC-V concepts without prescribing a
course, lesson order, or grading workflow. SimRV loads ELF files directly, so no `objcopy` step is
required.

Build the complete set into a temporary directory:

```bash
make -C examples/isa XLEN=64
make -C examples/isa XLEN=32
```

`OUT_DIR` and `RISCV_PREFIX` are overridable for classroom toolchains. For example:

```bash
make -C examples/isa XLEN=64 OUT_DIR=/tmp/my-simrv-examples
```

Build one example directly for RV64:

```bash
riscv64-unknown-elf-gcc -march=rv64gc -mabi=lp64d -nostdlib -static \
  -T examples/isa/linker.ld -o /tmp/simrv-arithmetic.elf examples/isa/arithmetic.S
./build/rv64-release/SimRV --tui --baremetal -m /tmp/simrv-arithmetic.elf --class
```

For RV32, use `-march=rv32gc -mabi=ilp32d` and the RV32 SimRV build. Each program stops in a
globally visible `done` loop so it can be stepped and inspected safely.

## Example catalog

| Source | Primary concepts | Useful state to inspect |
| --- | --- | --- |
| `arithmetic.S` | Immediate and register arithmetic | `a0`–`a4` at `checkpoint_results` |
| `addressing.S` | Base-plus-offset loads and stores | Registers and `values` memory |
| `load-widths.S` | Byte/halfword/word loads and sign extension | `a0` versus `a1`, `a2` versus `a3` |
| `loops-arrays.S` | Pointer traversal, loop counters, reductions | `t0`, `t1`, and running sum `a0` |
| `branches.S` | Signed comparison and conditional control flow | PC movement and `a2` |
| `calls-stack.S` | ABI calls, returns, and stack preservation | `ra`, `sp`, and `a0` |
| `multiply-divide.S` | M-extension multiply/divide/remainder | `a2`–`a7`, including division by zero |
| `atomics.S` | A-extension LR/SC and AMO behavior | Reservation loop and `shared_counter` |
| `floating-point.S` | D-extension loads, arithmetic, and stores | `fa0`–`fa3` and result memory |
| `compressed.S` | C-extension 16-bit encodings | Instruction widths and `a0`/`a1` |
| `counters.S` | `cycle` and `instret` CSRs | Counter deltas in `a4`/`a5` |
| `traps.S` | `mtvec`, `mepc`, `ebreak`, and `mret` | Trap CSRs and handler control flow |
| `uart-output.S` | Guest output through the 16550A UART | Virtual terminal text and MMIO polling |
| `uart-input-echo.S` | Polling input, calls, and terminal interaction | UART state and character count in `a0` |

The examples intentionally overlap concepts. They are demonstrations that teachers can select or
modify independently, not sequenced exercises.

## Interactive exploration

Press `g` to show or hide the Student Guide, `Enter` to perform its suggested action, `e` to explain
the current instruction, `?` for the relevant glossary topic, and `v` to record a short execution
trace.

Every example exports descriptive `checkpoint_*` symbols. Press `:` in the paused TUI, enter one of
those names, and continue with `c`; SimRV pauses before the labeled instruction. This makes it easy
to give students prompts such as “predict the next register change, then press `Enter` or `s`.” Use
`m` to review or remove configured breakpoints.

`ebreak` remains a real RISC-V breakpoint exception: use it when teaching traps or with a debugger,
not as a simulator-only classroom marker.

For the interactive UART example:

```bash
./build/rv64-release/SimRV --tui --baremetal \
  -m /tmp/simrv-isa-rv64/uart-input-echo.elf --class
```

Press `c` to run. Type into the guest terminal when prompted; `Ctrl-A` switches input focus between
the guest UART and TUI controls. End the line with Enter, then pause and inspect `a0` for the number
of echoed characters.

## Adapting examples for a class

- Rename or add `.global checkpoint_*` labels at discussion points.
- Change constants or data tables to create variants without changing the observation workflow.
- Use `--inspection-output <path>` when students need to share a reproducible state snapshot.
- Keep generated ELF files in a build or temporary directory rather than committing them.
