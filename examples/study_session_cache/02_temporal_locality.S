# ==============================================================================
# SimRV Cache Study Session - Exercise 2: Temporal Locality (Reuse)
# ==============================================================================
# Architecture: RV32GC / Bare-metal
# Base Address: 0x80000000
#
# Concept:
#   Temporal locality refers to accessing the same memory location multiple
#   times within a short time window.
#   - Iteration 1: The first access causes a compulsory miss; the line is loaded.
#   - Iterations 2..N: Every access to the same addresses hits the L1 cache.
# ==============================================================================

.section .text
.global _start

_start:
    la a0, array_data       # Base address of data
    li a1, 5                # Loop counter: 5 outer iterations
    li a2, 0                # Accumulator sum

outer_loop:
    beqz a1, done           # If loop counter is 0, exit

    # Read 4 words from the same 32-byte cache line
    lw t0, 0(a0)            # Miss on iter 1, Hit on iters 2..5
    add a2, a2, t0
    lw t1, 4(a0)            # Hit on all iterations (spatial on 1, temporal on 2..5)
    add a2, a2, t1
    lw t2, 8(a0)            # Hit on all iterations
    add a2, a2, t2
    lw t3, 12(a0)           # Hit on all iterations
    add a2, a2, t3

    addi a1, a1, -1         # Decrement loop counter
    j outer_loop

done:
    j done                  # Pause here for inspection in SimRV TUI

.section .data
.align 5
array_data:
    .word 10, 20, 30, 40, 50, 60, 70, 80
