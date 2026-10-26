#include "model_test.h"
#include "arch_test.h"

RVTEST_ISA("RV64I_Zba_Zbb_Zbs")
RVTEST_CODE_BEGIN

    la s0, begin_signature

    # 1. Zba: sh1add, sh2add, sh3add
    li a1, 10
    li a2, 3
    sh1add t0, a1, a2   # 10*2 + 3 = 23
    sw t0, 0(s0)

    sh2add t0, a1, a2   # 10*4 + 3 = 43
    sw t0, 4(s0)

    sh3add t0, a1, a2   # 10*8 + 3 = 83
    sw t0, 8(s0)

    # 2. Zbb: clz, ctz, cpop
    li a1, 0x0000000000010000
    clz t0, a1          # 47
    sw t0, 12(s0)

    ctz t0, a1          # 16
    sw t0, 16(s0)

    li a1, 0x00000000000F0F0F
    cpop t0, a1         # 12
    sw t0, 20(s0)

    # 3. Zbb: min, max, minu, maxu
    li a1, -50
    li a2, 25
    min t0, a1, a2      # -50
    sw t0, 24(s0)

    max t0, a1, a2      # 25
    sw t0, 28(s0)

    minu t0, a1, a2     # 25 (since -50 as uint is huge)
    sw t0, 32(s0)

    maxu t0, a1, a2     # -50 (huge uint)
    sw t0, 36(s0)

    # 4. Zbs: bset, bclr, binv, bext
    li a1, 0
    li a2, 7
    bset t0, a1, a2     # 1 << 7 = 128
    sw t0, 40(s0)

    li a1, 0xFF
    bclr t0, a1, a2     # 0xFF & ~(1 << 7) = 0x7F
    sw t0, 44(s0)

    li a1, 0
    binv t0, a1, a2     # 1 << 7 = 128
    sw t0, 48(s0)

    li a1, 0x80
    bext t0, a1, a2     # 1
    sw t0, 52(s0)

RVTEST_CODE_END

RVTEST_DATA_BEGIN
    .space 56
RVTEST_DATA_END
