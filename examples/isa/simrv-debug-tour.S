# A tiny deterministic guest for learning SimRV's trace, stepping, breakpoints,
# and register watchpoints. The doubled increment is an intentional bug.

.section .text.init
.option norvc
.global _start
.global debug_loop
.global debug_accumulate
.global debug_loop_backedge
.global debug_result
.global debug_done

_start:
    la sp, _stack_top
    li a0, 3
    li a1, 0

debug_loop:
    beq a0, zero, debug_result
debug_accumulate:
    addi a1, a1, 2       # Bug: this should add one each iteration.
    addi a0, a0, -1
debug_loop_backedge:
    j debug_loop

debug_result:
    nop
debug_done:
    j debug_done
