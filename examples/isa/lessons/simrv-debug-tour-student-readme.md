# SimRV Debug Tour

This folder is a self-contained classroom exercise for **Linux x86-64**. It includes a portable
SimRV binary and a prebuilt **RV32IMA** guest, so you do not need to clone the source repository or
install a RISC-V compiler.

## Start

Open a terminal in this folder and run:

```sh
./simrv --mission simrv-debug-tour.mission -m simrv-debug-tour.elf
```

The Lesson Tasks panel (titled **Lesson Tasks / Guide**) explains the current step. Read it, then
use the controls below to work through the guest. It contains the failure report, goal, task, hint,
rationale, checkpoint status, and available controls; the five-line lesson drawer keeps a short step
summary, checkpoint state, and the **F12 Tasks** shortcut visible while you work.

## Scenario

The test expects `a1=3` after three loop passes, but the supplied guest program reaches `a1=6`.
This is a bug in the guest program, not in the processor simulator. The pseudocode is:

```text
count = 3
total = 0
repeat while count != 0:
    total += ?
    count -= 1
```

Use SimRV's PC, Trace, and Registers to discover the increment and explain the mismatch. In this
exercise, **F6** is the exploration key: it executes one CPU instruction so you can inspect each
change. **F5** runs until the next breakpoint or lesson checkpoint; it is useful for moving between
known locations, but can pass over instructions you may want to study.

## Controls

| Key | Action |
| --- | --- |
| `F5` | Run until the next breakpoint or mission checkpoint; may pass over instructions |
| `F6` | Execute one guest instruction |
| `F12` | Open or reopen Lesson Tasks / Guide |
| `Enter` / `Esc` | Hide the task panel and inspect the workbench |
| `Space` / `N` | Advance the lesson step while Tasks is open |
| `N` | Advance the lesson step from the workbench |
| `P` | Return to the previous lesson step if you advanced by mistake; guest execution is not rewound |
| `Y` | Cycle through hidden, hint, and full rationale |
| `W` | Open Set Watchpoint; enter a register such as `a1`, then press Enter |
| `M` | Manage active breakpoints and watchpoints |
| `:` | Open Set PC Breakpoint; enter an address or symbol such as `debug_result` |
| `R` | Restart the mission and guest |
| `Q` / `F10` | Exit SimRV |

In the manager, use **Up/Down** or the mouse wheel to select an entry. Press **Enter** or
**Backspace** to remove the selected entry. Mission checkpoints appear in the same list and are
marked as lesson-owned. **C** clears all breakpoints and watchpoints, including the active lesson
checkpoint; hardware triggers are unaffected. Avoid it during the exercise.

Use **F6** when you want to watch what each instruction does; use **F5** to move quickly to a
known breakpoint or checkpoint. Advancing lesson steps does not mark a checkpoint reached. Only executing the guest to a named
checkpoint records it. If the terminal is too small for all requested panels, SimRV reports the
required size and restores the full layout when the terminal grows.

The included assembly source is `simrv-debug-tour.S`. It uses base RV32I instructions, so it also
assembles with an RV32IMA toolchain.
