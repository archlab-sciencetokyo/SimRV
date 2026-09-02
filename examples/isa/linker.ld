ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5); /* Read + execute. */
    data PT_LOAD FLAGS(6); /* Read + write. */
}

SECTIONS
{
    . = 0x80000000;
    .text : { *(.text.init) *(.text .text.*) } :text
    .rodata : { *(.rodata .rodata.*) } :text

    . = ALIGN(0x1000);
    .data : { *(.data .data.*) } :data
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        . += 4096;
        _stack_top = .;
    } :data
}
