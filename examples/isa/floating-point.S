.section .text.init
.global _start
.global checkpoint_fp_result
.global done

_start:
    # Set mstatus.FS=Initial before using architectural floating-point state.
    li      t1, 0x2000
    csrs    mstatus, t1
    la      t0, operands
    fld     fa0, 0(t0)
    fld     fa1, 8(t0)
    fadd.d  fa2, fa0, fa1
    fmul.d  fa3, fa0, fa1
    fsd     fa2, 16(t0)

checkpoint_fp_result:
    # Expected values: fa2 = 3.75 and fa3 = 3.375.
    nop
done:
    j       done

.section .data
.align 3
operands:
    .double 1.5, 2.25, 0.0
