#include "model_test.h"
#include "arch_test.h"

RVTEST_ISA("RV64GCBV")
RVTEST_CODE_BEGIN

    la s0, begin_signature

    # 1. Read misa
    csrr t0, misa
    # Lower 32 bits of misa
    sw t0, 0(s0)
    # Upper 32 bits of misa (contains MXL=2 in bits 63:62 -> 0x80000000)
    srai t1, t0, 32
    sw t1, 4(s0)

    # 2. Check mstatus presence
    csrr t2, mstatus
    # Verify mstatus is readable and non-zero
    sw t2, 8(s0)

    # 3. Arithmetic compliance signature
    li a1, 0x12345678
    li a2, 0x00000002
    mul t3, a1, a2
    sw t3, 12(s0)

RVTEST_CODE_END

RVTEST_DATA_BEGIN
    .space 16
RVTEST_DATA_END
