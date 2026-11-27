# SimRV Debug Tour: Labmate Test

This short usability test is for someone who has not used SimRV's mission mode. Ask the tester
to think aloud and avoid explaining the controls unless they are fully stuck. Record where they
pause, misread a key, or cannot tell what to do next.

## Build and launch

From the repository root:

```sh
make -C examples/isa XLEN=32 ARCH=rv32ima ABI=ilp32 \
  OUT_DIR=/tmp/simrv-debug-tour-rv32ima \
  /tmp/simrv-debug-tour-rv32ima/simrv-debug-tour.elf
python3 scripts/check_mission.py examples/isa/lessons/simrv-debug-tour.mission \
  --program /tmp/simrv-debug-tour-rv32ima/simrv-debug-tour.elf --preview --smoke-test \
  --simrv build/rv32-release/simrv
build/rv32-release/simrv --mission examples/isa/lessons/simrv-debug-tour.mission \
  -m /tmp/simrv-debug-tour-rv32ima/simrv-debug-tour.elf
```

If your cross-compiler uses a different executable prefix, set `RISCV_PREFIX` on the `make`
command. For example, use `RISCV_PREFIX=riscv32-unknown-elf-`.

## What to observe

- Can the tester tell what the PC, Trace, and Registers are showing?
- Can they tell when F6 steps the guest versus when Space advances the instruction?
- Can they set the `a1` watchpoint, then use **M**, **Up/Down** (or wheel), and **Enter** to remove it without help?
- Can they set a symbol breakpoint and distinguish it from the mission checkpoint?
- Can they diagnose the wrong result from the final register value and trace?

Ask them to mark the first point where they felt unsure. Then ask what small wording or control
change would have helped. Do not score or collect identifying information.

## Facilitator check

The guest intentionally adds 2 to `a1` on each of three loop iterations, so the final value is
6 instead of 3. The faulty instruction is `addi a1, a1, 2`. The mission's four checkpoints should
be reached in this order: `debug_loop`, `debug_accumulate`, `debug_loop_backedge`, `debug_done`.
