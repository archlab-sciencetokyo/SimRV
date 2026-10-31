OUTPUT_ARCH("riscv")
ENTRY(_start)

SECTIONS
{
  . = 0x80000000;
  .text.init : { *(.text.init) }
  . = ALIGN(0x1000);
  .tohost : { *(.tohost) }
  . = ALIGN(0x1000);
  .text : { *(.text) }
  . = ALIGN(0x1000);
  .data.signature : {
    begin_signature = .;
    rvtest_sig_begin = .;
    *(.data.signature)
    end_signature = .;
    rvtest_sig_end = .;
  }
  .data : { *(.data) }
  .bss : {
    *(.bss)
    . = ALIGN(8);
    . += 0x1000;
    stack_top = .;
  }
  _end = .;
}
