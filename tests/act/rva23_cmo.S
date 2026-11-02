#include "model_test.h"
#include "arch_test.h"

RVTEST_ISA("RV64I_Zicboz_Zicbom_Zicbop")
RVTEST_CODE_BEGIN

    la s0, begin_signature
    la s1, test_cache_block

    # 1. Fill test cache block with non-zero words
    li t0, 0x11223344
    li t1, 0x55667788
    sw t0, 0(s1)
    sw t1, 4(s1)
    sw t0, 32(s1)
    sw t1, 60(s1)

    # Record non-zero state in signature
    lw t2, 0(s1)
    sw t2, 0(s0)
    lw t2, 4(s1)
    sw t2, 4(s0)

    # 2. Execute cbo.zero on the cache block
    cbo.zero (s1)

    # Record zeroed state in signature
    lw t2, 0(s1)
    sw t2, 8(s0)
    lw t2, 4(s1)
    sw t2, 12(s0)
    lw t2, 32(s1)
    sw t2, 16(s0)
    lw t2, 60(s1)
    sw t2, 20(s0)

    # 3. Test CMO clean, flush, inval operations
    cbo.clean (s1)
    cbo.flush (s1)
    cbo.inval (s1)

    # Record post-cmo status
    li t3, 0x00c0ffee
    sw t3, 24(s0)

    # 4. Test prefetch operations (Zicbop)
    prefetch.i 0(s1)
    prefetch.r 0(s1)
    prefetch.w 0(s1)

    li t4, 0x00b007ed
    sw t4, 28(s0)

RVTEST_CODE_END

RVTEST_DATA_BEGIN
    .space 32
RVTEST_DATA_END

.section .data
    .align 6
test_cache_block:
    .space 64
