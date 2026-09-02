.section .text.init
.global _start
.global checkpoint_before_atomic
.global checkpoint_after_atomic
.global done

_start:
    la      t0, shared_counter

checkpoint_before_atomic:
1:
    lr.w    t1, (t0)
    addi    t2, t1, 1
    sc.w    t3, t2, (t0)
    bnez    t3, 1b

    # amoadd.w returns the old value and atomically adds a0 to memory.
    li      a0, 5
    amoadd.w a1, a0, (t0)
    lw      a2, (t0)

checkpoint_after_atomic:
    nop
done:
    j       done

.section .data
.align 2
shared_counter:
    .word   41
