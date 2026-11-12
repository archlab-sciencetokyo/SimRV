# SimRV Example Plugin: DMA Memory Accelerator

This example demonstrates how to build an external device plugin that acts as a **Bus Master** using the **Version 2 C ABI**.

## Key Concepts Demonstrated
1. **Physical Memory DMA Mastering**: Reads and writes guest physical RAM directly using `SimRvHostServices::dma_read` and `SimRvHostServices::dma_write`.
2. **Interrupt Notification**: Raises platform interrupt upon DMA transfer and checksum completion.
3. **Structured Logging**: Uses `SimRvHostServices::log_msg` for formatted status reporting without interfering with the TUI.

## Building

```bash
# Using GCC directly:
gcc -O3 -shared -fPIC -I../../../include -o libsimrv-demo-dma-accel.so dma_accel_plugin.c

# Or with CMake:
cmake -B build
cmake --build build
```

## Running with SimRV

Option 1: Using the platform configuration file:
```bash
simrv --cli --config dma_accel.cfg guest.elf
```

Option 2: Direct CLI plugin flag:
```bash
simrv --cli --plugin ./build/libdemo-dma-accel.so=17 guest.elf
```

The device is mapped at physical address `0x10060000` by default.
