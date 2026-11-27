.section .text.init
.global _start
.global checkpoint_products
.global checkpoint_division
.global done

_start:
    li      a0, 21
    li      a1, 4
    mul     a2, a0, a1
    mulh    a3, a0, a1

checkpoint_products:
    div     a4, a0, a1
    rem     a5, a0, a1

    # RISC-V defines division by zero: quotient is all ones, remainder is dividend.
    div     a6, a0, zero
    rem     a7, a0, zero

checkpoint_division:
    nop
done:
    j       done
