# SimRV Baremetal Programming & MMIO Guide

This guide details the physical memory map, MMIO device registers, and compilation workflow for developing baremetal programs to run on SimRV.

---

## Physical Memory Map

SimRV uses a physical memory map where memory-mapped I/O (MMIO) and PCIe devices are allocated in lower address ranges, and RAM is mapped starting at `0x80000000`.

| Device / Region | Base Address | Size | Description |
|:---|:---|:---|:---|
| **Power Controller** | `0x00100000` | `0x00001000` (4 KB) | SiFive test-finisher poweroff / reboot port |
| **UART 16550A** | `0x10000000` | `0x00000100` (256 B) | Serial console input/output port |
| **VirtIO MMIO Slots** | `0x10001000` | `0x00008000` (32 KB) | VirtIO MMIO transport slots (Disk, Net, Console, GPU, Input, Sound, RNG) |
| **PCIe ECAM Space** | `0x30000000` | `0x10000000` (256 MB) | PCI Express enhanced configuration space |
| **PCIe 32-bit MMIO** | `0x40000000` | `0x10000000` (256 MB) | PCI Express non-prefetchable 32-bit BAR window |
| **PLIC / AIA APLIC** | `0x0C000000` / `0x50000000` | `0x04000000` (64 MB) | Platform-Level Interrupt Controller / AIA APLIC |
| **CLINT / ACLINT** | `0x02000000` / `0x60000000` | `0x000C0000` (768 KB) | Core Local Interruptor (mtimecmp / software interrupts) |
| **RTC** | `0x70000000` | `0x00001000` (4 KB) | Real-Time Clock (`mtime` mirror) |
| **System RAM** | `0x80000000` | Configurable (e.g. 128 MB – 2 GB) | Main physical memory space |

---

## Writing Baremetal Programs

To develop a baremetal program, you need:
1. An assembly startup file (`startup.S`) to initialize the stack pointer and invoke your main entry point.
2. A linker script (`link.ld`) to map the sections starting at the physical RAM base (`0x80000000`).
3. An exit routine using the `tohost` termination register or Power MMIO device to signal simulation shutdown.

### 1. Linker Script (`link.ld`)

```ld
OUTPUT_ARCH( "riscv" )
ENTRY(_start)

SECTIONS
{
  . = 0x80000000;
  .text : {
    *(.text.init)
    *(.text .text.*)
  }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.*) }
}
```

### 2. Startup Assembly (`startup.S`)

Initialize the stack pointer `sp` to a safe region in RAM (e.g., `0x8E000000`) and call `main`:

```assembly
.section .text.init
.global _start
_start:
    # Set up stack pointer to 0x8E000000
    li sp, 0x8E000000

    # Call main C/C++ function
    jal main

    # Fallback exit: write 1 to tohost (0x80001000) or poweroff
_startup_exit:
    li t0, 0x00100000
    li t1, 0x5555
    sw t1, 0(t0)
loop:
    wfi
    j loop
```

### 3. Compiling and Packaging

Compile your source files using a RISC-V cross-compiler and dump it to a raw binary file:

```bash
# Compile and Link
riscv64-unknown-elf-gcc -march=rv32imac -mabi=ilp32 -T link.ld startup.S main.c -o program.elf -ffreestanding -O2 -nostdlib

# Convert ELF to raw flat binary image
riscv64-unknown-elf-objcopy -O binary program.elf program.bin
```

### 4. Running the Program

Run the resulting binary image in SimRV in baremetal mode (`-b` / `--baremetal`). SimRV starts in interactive TUI mode by default:

```bash
# Interactive TUI mode (Default)
./build/rv32-release/SimRV -b -m program.bin

# Headless / CLI-only mode (fast execution)
./build/rv32-release/SimRV -b -m program.bin --mode fast --cli

# Cycle-accurate 4-stage pipeline simulation
./build/rv32-release/SimRV -b -m program.bin --mode cycle-accurate --cli

# 5-stage classic pipeline simulation
./build/rv32-release/SimRV -b -m program.bin --mode cycle-accurate --pipeline 5stage --cli
```

---

## Device Driver API Examples

### UART 16550A Serial I/O
The UART registers are aligned to 4-byte boundaries (word-addressed) or 1-byte boundaries. 

```c
#define UART_BASE 0x10000000
#define UART_REG(offset) ((volatile uint32_t*)(UART_BASE + (offset)))

#define UART_THR UART_REG(0x00) // Transmitter Holding Register (write)
#define UART_LSR UART_REG(0x14) // Line Status Register (read)

void uart_putc(char c) {
    // Wait until Transmit Holding Register Empty (THRE) bit 5 is set
    while ((*UART_LSR & 0x20) == 0);
    *UART_THR = c;
}

void uart_puts(const char* s) {
    while (*s) {
        uart_putc(*s++);
    }
}
```

### Real-Time Clock / Timer Delay
Use the RTC `mtime` register at `0x70000000` to measure accurate delays. `mtime` runs at approximately 10 MHz (10,000 ticks = 1 ms).

```c
#define RTC_MTIME ((volatile uint64_t*)0x70000000)

uint32_t get_time_ms() {
    return (uint32_t)(*RTC_MTIME / 10000);
}

void delay_ms(uint32_t ms) {
    uint32_t start = get_time_ms();
    while (get_time_ms() - start < ms);
}
```

### Power Controller (Shutdown & Reboot)
Write SiFive test-finisher commands to `0x00100000` to cleanly terminate or reboot the machine:

```c
#define POWER_BASE ((volatile uint32_t*)0x00100000)

void power_shutdown() {
    *POWER_BASE = 0x5555; // Pass / Clean Poweroff
}

void power_reboot() {
    *POWER_BASE = 0x7777; // Reset / Warm Reboot
}
```
