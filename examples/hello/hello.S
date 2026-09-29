.section .text.init, "ax", @progbits
.global _start
.type _start, @function

_start:
    # Keep the stack above the small example image.
    li sp, 0x80010000
    la t0, message

1:
    lbu t1, 0(t0)
    beqz t1, 2f
    li t2, 0x10000000       # SimRV's UART transmit register
    sb t1, 0(t2)
    addi t0, t0, 1
    j 1b

2:
    li t0, 0x80001000       # SimRV tohost termination register
    li t1, 1
    sw t1, 0(t0)
3:
    j 3b

.section .rodata
message:
    .asciz "Hello from SimRV!\n"
