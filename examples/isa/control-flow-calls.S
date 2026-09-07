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
.global mission_branch
.global mission_loop
.global mission_before_call
.global mission_in_callee
.global mission_after_call
.global mission_done
.global add_pair

_start:
    la sp, _stack_top
    li a0, 2
    li a1, 3
mission_branch:
    bne a0, zero, mission_loop
    ebreak
mission_loop:
    addi a0, a0, -1
    bne a0, zero, mission_loop
    li a0, 20
    li a1, 22
mission_before_call:
    call add_pair
mission_after_call:
    nop
mission_done:
    j mission_done

add_pair:
mission_in_callee:
    addi sp, sp, -FRAME
    REG_S ra, 0(sp)
    add a0, a0, a1
    REG_L ra, 0(sp)
    addi sp, sp, FRAME
    ret
