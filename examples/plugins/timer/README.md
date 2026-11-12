# SimRV Example Plugin: Cycle-Accurate Timer

This example demonstrates how to author a dynamic external device plugin for SimRV using the **Version 2 C ABI**.

## Key Concepts Demonstrated
1. **Simulation Cycle Ticking**: Implements `advance_cycles` to receive cycle advancement ticks synchronized with CPU execution.
2. **Interrupt Notification**: Asserts and clears platform interrupts using `SimRvHostServices::set_irq`.
3. **Structured Logging**: Emits messages to SimRV's logger via `SimRvHostServices::log_msg` without corrupting TUI mode.

## Building

```bash
# Using GCC directly:
gcc -O3 -shared -fPIC -I../../../include -o libsimrv-demo-timer.so timer_plugin.c

# Or with CMake:
cmake -B build
cmake --build build
```

## Running with SimRV

Option 1: Using the platform configuration file:
```bash
simrv --cli --config timer.cfg guest.elf
```

Option 2: Direct CLI plugin flag:
```bash
simrv --cli --plugin ./build/libsimrv-demo-timer.so=16 guest.elf
```

The plugin attaches to physical address `0x10050000` by default.
