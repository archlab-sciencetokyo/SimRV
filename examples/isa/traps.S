.section .text.init
.global _start
.global checkpoint_before_trap
.global checkpoint_after_trap
.global trap_handler
.global done

_start:
    la      t0, trap_handler
    csrw    mtvec, t0
checkpoint_before_trap:
    # Keep this breakpoint instruction 32-bit because the handler advances mepc by four bytes.
    .option push
    .option norvc
    ebreak
    .option pop
checkpoint_after_trap:
    li      a0, 1

done:
    j       done

.align 2
trap_handler:
    csrr    t1, mepc
    addi    t1, t1, 4
    csrw    mepc, t1
    mret
