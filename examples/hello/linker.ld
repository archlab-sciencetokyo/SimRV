OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
  ram (rwx) : ORIGIN = 0x80000000, LENGTH = 256M
}

SECTIONS
{
  . = ORIGIN(ram);
  .text :
  {
    KEEP(*(.text.init))
    *(.text .text.*)
  } > ram
  .rodata : { *(.rodata .rodata.*) } > ram
  .data : { *(.data .data.*) } > ram
  .bss : { *(.bss .bss.*) *(COMMON) } > ram
}
