/*
 * Assembly macros for the SimRV Xsimrvtrace CSR annotation ABI.
 * Include with the RISC-V assembler. Each macro preserves its pointer operand
 * and clobbers t0/t1. The CSR writes are harmless when architectural tracing is off.
 */
.macro SIMRV_TRACE_EMIT pointer, command
    mv t1, \pointer
.Lsimrv_trace_loop_\@:
    lbu t0, 0(t1)
    beqz t0, .Lsimrv_trace_done_\@
    csrw 0x800, t0
    addi t1, t1, 1
    j .Lsimrv_trace_loop_\@
.Lsimrv_trace_done_\@:
    li t0, \command
    csrw 0x800, t0
.endm

.macro SIMRV_TRACE_BEGIN pointer
    SIMRV_TRACE_EMIT \pointer, 0x40000000
.endm

.macro SIMRV_TRACE_END pointer
    SIMRV_TRACE_EMIT \pointer, 0x80000000
.endm

.macro SIMRV_MARKER pointer
    SIMRV_TRACE_EMIT \pointer, 0xC0000000
.endm
