# SimRV Baremetal Programming & MMIO Guide

This guide details the physical memory map, MMIO device registers, and compilation workflow for developing baremetal programs to run on SimRV.

---

## Physical Memory Map

SimRV uses a physical memory map where memory-mapped I/O (MMIO) devices are allocated in lower address ranges, and RAM is mapped starting at `0x80000000`.

| Device / Region | Base Address | Size | Description |
|:---|:---|:---|:---|
| **UART 16550A** | `0x10000000` | `0x00000100` (256 B) | Serial console input/output port |
| **Framebuffer Config** | `0x30000000` | `0x00001000` (4 KB) | Video mode control, keyboard, and mouse inputs |
| **Framebuffer Memory** | `0x30001000` | `0x001FF000` (~2 MB) | Video memory pixel buffer (e.g. 320x200 RGBA8888) |
| **Audio Controller** | `0x30200000` | `0x00010000` (64 KB) | 8-channel SFX and MIDI audio control |
| **VirtIO Console** | `0x40000000` | `0x08000000` (128 MB) | Virtio console device |
| **VirtIO Disk** | `0x48000000` | `0x08000000` (128 MB) | Virtio block storage device |
| **PLIC** | `0x50000000` | `0x04000000` (64 MB) | Platform-Level Interrupt Controller |
| **CLINT** | `0x60000000` | `0x000C0000` (768 KB) | Core Local Interruptor (mtimecmp / software interrupts) |
| **RTC** | `0x70000000` | `0x00001000` (4 KB) | Real-Time Clock (`mtime` mirror) |
| **System RAM** | `0x80000000` | Up to 512 MB | Architectural RAM space |

---

## Writing Baremetal Programs

To develop a baremetal program, you need:
1. An assembly startup file (`startup.S`) to initialize the stack pointer and invoke your main entry point.
2. A linker script (`link.ld`) to map the sections starting at the physical RAM base (`0x80000000`).
3. An exit routine using the `tohost` termination register to signal the simulator to shut down.

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

    # Fallback exit: write 1 to tohost (0x80001000) to terminate
_startup_exit:
    li t0, 0x80001000
    li t1, 1
    sw t1, 0(t0)
loop:
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

Run the resulting binary image in SimRV with baremetal/raw binary execution mode (`-b` option):

```bash
./build/rv32-release/SimRV -m program.bin -b
```

To run with the interactive TUI dashboard:
```bash
./build/rv32-release/SimRV -m program.bin -b --tui
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

### Custom Framebuffer Display
The custom framebuffer device supports simple double-buffered or direct draw routines.

```c
#define FB_CTRL_BASE   0x30000000
#define FB_WIDTH_REG   ((volatile uint32_t*)(FB_CTRL_BASE + 0x00))
#define FB_HEIGHT_REG  ((volatile uint32_t*)(FB_CTRL_BASE + 0x04))
#define FB_FORMAT_REG  ((volatile uint32_t*)(FB_CTRL_BASE + 0x08))
#define FB_FLUSH_REG   ((volatile uint32_t*)(FB_CTRL_BASE + 0x0C))
#define FB_PIXELS      ((volatile uint32_t*)0x30001000)

void display_init(uint32_t width, uint32_t height) {
    *FB_WIDTH_REG  = width;
    *FB_HEIGHT_REG = height;
    *FB_FORMAT_REG = 1; // 1 = RGBA8888
}

void display_draw_pixel(uint32_t x, uint32_t y, uint32_t color) {
    uint32_t width = *FB_WIDTH_REG;
    FB_PIXELS[y * width + x] = color;
}

void display_flush() {
    *FB_FLUSH_REG = 1; // Signal simulator to redraw screen
}
```

### Keyboard Input events
Keyboard events are queried through the Framebuffer registers.

```c
#define FB_KEY_REG     ((volatile uint32_t*)(FB_CTRL_BASE + 0x10))
#define FB_KEY_STATUS  ((volatile uint32_t*)(FB_CTRL_BASE + 0x14))

int get_keyboard_event(int* pressed, char* key) {
    if (*FB_KEY_STATUS) {
        uint32_t packed = *FB_KEY_REG;
        *pressed = (packed >> 31) & 1; // 1 if pressed, 0 if released
        *key = packed & 0xFF;          // ASCII code
        return 1; // Event received
    }
    return 0; // No event
}
```

### Real-Time Clock / Timer Delay
Use the RTC `mtime` register at `0x70000000` to measure accurate delays. `mtime` runs at approximately 10 MHz.

```c
#define RTC_MTIME ((volatile uint64_t*)0x70000000)

uint32_t get_time_ms() {
    // 10,000 RTC ticks = 1 millisecond
    return (uint32_t)(*RTC_MTIME / 10000);
}

void delay_ms(uint32_t ms) {
    uint32_t start = get_time_ms();
    while (get_time_ms() - start < ms);
}
```
