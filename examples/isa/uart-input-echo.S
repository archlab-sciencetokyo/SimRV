.equ UART_BASE, 0x10000000
.equ UART_RBR,  0
.equ UART_THR,  0
.equ UART_LSR,  5
.equ UART_DR,   0x01
.equ UART_THRE, 0x20

.section .text.init
.global _start
.global checkpoint_wait_for_input
.global checkpoint_line_received
.global getchar
.global putchar
.global done

_start:
    la      s0, prompt
1:
    lbu     a0, 0(s0)
    beqz    a0, checkpoint_wait_for_input
    call    putchar
    addi    s0, s0, 1
    j       1b

checkpoint_wait_for_input:
    li      s1, 0
2:
    call    getchar
    li      t0, 10
    beq     a0, t0, checkpoint_line_received
    call    putchar
    addi    s1, s1, 1
    j       2b

checkpoint_line_received:
    li      a1, 10
    mv      a0, a1
    call    putchar
    mv      a0, s1

done:
    j       done

getchar:
    li      t0, UART_BASE
3:
    lbu     t1, UART_LSR(t0)
    andi    t1, t1, UART_DR
    beqz    t1, 3b
    lbu     a0, UART_RBR(t0)
    ret

putchar:
    li      t0, UART_BASE
4:
    lbu     t1, UART_LSR(t0)
    andi    t1, t1, UART_THRE
    beqz    t1, 4b
    sb      a0, UART_THR(t0)
    ret

.section .rodata
prompt:
    .asciz  "Type a line; SimRV will echo it: "
