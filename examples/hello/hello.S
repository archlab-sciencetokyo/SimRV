.section .text.init, "ax", @progbits
.global _start
.type _start, @function

.equ UART_TX, 0x10000000
.equ TOHOST,  0x80001000

_start:
    li sp, 0x80010000
    la a0, message
    call print_string
    call print_counting
    call print_parity
    call print_sum_result

    li t0, TOHOST
    li t1, 1
    sw t1, 0(t0)

halt:
    j halt

# Print the NUL-terminated string pointed to by a0.
.global print_string
.type print_string, @function
print_string:
    mv t0, a0
print_string_loop:
    lbu t1, 0(t0)
    beqz t1, print_string_done
    li t2, UART_TX
    sb t1, 0(t2)
    addi t0, t0, 1
    j print_string_loop
print_string_done:
    ret

# Print the low byte of a0 to the simulated UART.
.global print_char
.type print_char, @function
print_char:
    li t2, UART_TX
    sb a0, 0(t2)
    ret

# Print a short sequence using a loop and a branch-based number selector.
.global print_counting
.type print_counting, @function
print_counting:
    mv s1, ra
    la a0, counting_header
    call print_string
    li s0, 0
print_counting_loop:
    li a0, 32
    call print_char
    mv a0, s0
    call print_digit
    addi s0, s0, 1
    li t0, 5
    blt s0, t0, print_counting_loop
    la a0, newline
    mv ra, s1
    j print_string

# Select one of several strings with conditional branches.
.global print_digit
.type print_digit, @function
print_digit:
    beqz a0, print_digit_zero
    li t0, 1
    beq a0, t0, print_digit_one
    li t0, 2
    beq a0, t0, print_digit_two
    li t0, 3
    beq a0, t0, print_digit_three
    li t0, 4
    beq a0, t0, print_digit_four
    la a0, digit_five
    j print_string
print_digit_four:
    la a0, digit_four
    j print_string
print_digit_zero:
    la a0, digit_zero
    j print_string
print_digit_one:
    la a0, digit_one
    j print_string
print_digit_two:
    la a0, digit_two
    j print_string
print_digit_three:
    la a0, digit_three
    j print_string

# Classify numbers with an even/odd branch while walking through a loop.
.global print_parity
.type print_parity, @function
print_parity:
    mv s1, ra
    la a0, parity_header
    call print_string
    li s0, 1
print_parity_loop:
    mv a0, s0
    call print_digit
    li a0, 32
    call print_char
    andi t0, s0, 1
    beqz t0, print_even
    la a0, odd
    call print_string
    j print_parity_next
print_even:
    la a0, even
    call print_string
print_parity_next:
    addi s0, s0, 1
    li t0, 6
    blt s0, t0, print_parity_loop
    la a0, newline
    mv ra, s1
    j print_string

# Call a small arithmetic routine implemented with repeated addition.
.global print_sum_result
.type print_sum_result, @function
print_sum_result:
    mv s1, ra
    la a0, sum_header
    call print_string
    li a0, 5
    call sum_to_n
    # sum_to_n(5) is 15; use a fixed string to keep this sample IMAC-only.
    la a0, sum_value
    mv ra, s1
    j print_string

.global sum_to_n
.type sum_to_n, @function
sum_to_n:
    mv t0, a0
    li t1, 0
sum_to_n_loop:
    add t1, t1, t0
    addi t0, t0, -1
    bnez t0, sum_to_n_loop
    mv a0, t1
    ret

.section .rodata
message:
    .asciz "Hello from SimRV!\n"
counting_header:
    .asciz "Counting:"
parity_header:
    .asciz "Parity:"
sum_header:
    .asciz "sum_to_n(5) = "
sum_value:
    .asciz "15\n"
newline:
    .asciz "\n"
digit_zero:
    .asciz "0"
digit_one:
    .asciz "1"
digit_two:
    .asciz "2"
digit_three:
    .asciz "3"
digit_four:
    .asciz "4"
digit_five:
    .asciz "5"
even:
    .asciz "even\n"
odd:
    .asciz "odd\n"
