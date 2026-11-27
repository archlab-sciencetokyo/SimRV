.section .text.init
.global _start
.global checkpoint_loaded
.global checkpoint_stored
.global done

_start:
    la      t0, values
    lw      a0, 0(t0)
    lw      a1, 4(t0)
checkpoint_loaded:
    add     a2, a0, a1
    sw      a2, 8(t0)

checkpoint_stored:
    nop
done:
    j       done

.section .data
.align 2
values:
    .word   11, 31, 0
