.section .text.init
.global _start
.global checkpoint_loop
.global checkpoint_sum
.global done

_start:
    la      t0, values
    li      t1, 5
    li      a0, 0

checkpoint_loop:
    lw      t2, 0(t0)
    add     a0, a0, t2
    addi    t0, t0, 4
    addi    t1, t1, -1
    bnez    t1, checkpoint_loop

checkpoint_sum:
    # Expected result: a0 = 15.
    nop
done:
    j       done

.section .rodata
.align 2
values:
    .word   1, 2, 3, 4, 5
