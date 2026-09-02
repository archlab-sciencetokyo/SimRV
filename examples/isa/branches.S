.section .text.init
.global _start
.global checkpoint_compare
.global less
.global done

_start:
    li      a0, 4
    li      a1, 9
checkpoint_compare:
    blt     a0, a1, less
    li      a2, 0
    j       done

less:
    li      a2, 1

done:
    j       done
