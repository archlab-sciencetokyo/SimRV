.section .text.init
.global _start
.global checkpoint_sign_extension
.global done

_start:
    la      t0, samples

    # The same 0x80 byte becomes -128 with lb and +128 with lbu.
    lb      a0, 0(t0)
    lbu     a1, 0(t0)

    # The same 0x8001 halfword becomes negative with lh and positive with lhu.
    lh      a2, 2(t0)
    lhu     a3, 2(t0)

    # lw sign-extends on RV64 and fills the complete register on RV32.
    lw      a4, 4(t0)

checkpoint_sign_extension:
    nop
done:
    j       done

.section .data
.align 2
samples:
    .byte   0x80, 0x7f
    .half   0x8001
    .word   0x80000002
