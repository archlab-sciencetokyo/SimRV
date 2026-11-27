#if __riscv_xlen == 64
#define REG_S sd
#define REG_L ld
#define FRAME 16
#else
#define REG_S sw
#define REG_L lw
#define FRAME 8
#endif

.section .text.init
.global _start
.global checkpoint_before_call
.global checkpoint_after_call
.global add_pair
.global done

_start:
    la      sp, _stack_top
    li      a0, 20
    li      a1, 22
checkpoint_before_call:
    call    add_pair

checkpoint_after_call:
    nop
done:
    j       done

add_pair:
    addi    sp, sp, -FRAME
    REG_S   ra, 0(sp)
    add     a0, a0, a1
    REG_L   ra, 0(sp)
    addi    sp, sp, FRAME
    ret
