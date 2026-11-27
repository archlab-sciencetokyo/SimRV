.section .text.init
.global _start
.global checkpoint_results
.global done

_start:
    li      a0, 7
    li      a1, 5
    add     a2, a0, a1
    sub     a3, a0, a1
    slli    a4, a2, 2

checkpoint_results:
    nop
done:
    j       done
