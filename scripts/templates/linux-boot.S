    .section .text
    .globl _start
_start:
    # a0 and a1 are pre-loaded by simulator:
    # a0 = 0 (hartid)
    # a1 = 0x81000000 (DTB physical address, already virtual-mapped in supervisor mode)
    # DO NOT overwrite them!

    li t0, 0xfbff
    csrw medeleg, t0

    li t0, 0x222
    csrw mideleg, t0

    li t0, 0x7
    csrw mcounteren, t0
    csrw scounteren, t0

    li t0, 0x800
    csrs mstatus, t0

    # Final touch: set mepc to physical address where kernel is loaded.
    # SimRV loads the combined memory image at DRAM base 0x80000000, and
    # the RV32 Linux Image header expects the payload at a 4 MiB offset.
    # Therefore the kernel entry handoff address is 0x80400000.
    li t0, 0x80400000
    csrw mepc, t0

    mret