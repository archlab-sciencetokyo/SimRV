OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
  ram (rwx) : ORIGIN = 0x80000000, LENGTH = 256M
}

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

SECTIONS
{
  . = ORIGIN(ram);
  __image_start = .;

  .text : ALIGN(4)
  {
    KEEP(*(.text.entry))
    *(.text .text.*)
  } > ram :text

  .rodata : ALIGN(8)
  {
    *(.srodata .srodata.*)
    *(.rodata .rodata.*)
  } > ram :text

  .data 0x80020000 :
  {
    . = ALIGN(8);
    __global_pointer$ = . + 0x800;
    /* Keep writable data away from SimRV's hardcoded tohost window at 0x80001000. */
    BYTE(0)
    . = ALIGN(8);
    *(.sdata .sdata.*)
    *(.data .data.*)
  } > ram :data

  .bss (NOLOAD) : ALIGN(8)
  {
    __bss_start = .;
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(8);
    __bss_end = .;
  } > ram :data

  .stack (NOLOAD) : ALIGN(16)
  {
    __stack_bottom = .;
    . += 0x10000;
    __stack_top = .;
  } > ram :data

  __image_end = .;

  /DISCARD/ :
  {
    *(.comment)
    *(.riscv.attributes)
    *(.note .note.*)
    *(.eh_frame .eh_frame.*)
  }
}
