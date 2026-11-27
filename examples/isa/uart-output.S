.equ UART_BASE, 0x10000000
.equ UART_THR,  0
.equ UART_LSR,  5
.equ UART_THRE, 0x20
.equ UART_TEMT, 0x40

.section .text.init
.global _start
.global checkpoint_before_output
.global checkpoint_after_output
.global putchar
.global done

_start:
    la      s0, message

checkpoint_before_output:
next_character:
    lbu     a0, 0(s0)
    beqz    a0, wait_for_uart_empty
    call    putchar
    addi    s0, s0, 1
    j       next_character

putchar:
    li      t0, UART_BASE
wait_for_uart:
    lbu     t1, UART_LSR(t0)
    andi    t1, t1, UART_THRE
    beqz    t1, wait_for_uart
    sb      a0, UART_THR(t0)
    ret

checkpoint_after_output:
wait_for_uart_empty:
    li      t0, UART_BASE
    lbu     t1, UART_LSR(t0)
    andi    t1, t1, UART_TEMT
    beqz    t1, wait_for_uart_empty
    nop
done:
    j       done

.section .rodata
message:
    .asciz  "SimRV classroom example\n"
