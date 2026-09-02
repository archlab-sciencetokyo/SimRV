.section .text.init
.global _start
.global checkpoint_counter_delta
.global done

_start:
    rdcycle a0
    rdinstret a1

    addi    t0, zero, 4
1:
    addi    t0, t0, -1
    bnez    t0, 1b

    rdcycle a2
    rdinstret a3
    sub     a4, a2, a0
    sub     a5, a3, a1

checkpoint_counter_delta:
    # a4 is elapsed cycles; a5 is the retired-instruction delta.
    nop
done:
    j       done
