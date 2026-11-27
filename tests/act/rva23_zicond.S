#include "model_test.h"
#include "arch_test.h"

RVTEST_ISA("RV64I_Zicond")
RVTEST_CODE_BEGIN

    la s0, begin_signature

    # Test 1: czero.eqz with rs2 == 0 (result = 0)
    li a1, 0x12345678
    li a2, 0
    czero.eqz t0, a1, a2
    sw t0, 0(s0)

    # Test 2: czero.eqz with rs2 != 0 (result = rs1)
    li a2, 1
    czero.eqz t0, a1, a2
    sw t0, 4(s0)

    # Test 3: czero.nez with rs2 == 0 (result = rs1)
    li a2, 0
    czero.nez t0, a1, a2
    sw t0, 8(s0)

    # Test 4: czero.nez with rs2 != 0 (result = 0)
    li a2, 1
    czero.nez t0, a1, a2
    sw t0, 12(s0)

    # Test 5: czero.eqz with negative / sign-extended rs1
    li a1, -1
    li a2, 0
    czero.eqz t0, a1, a2
    sw t0, 16(s0)

    li a2, 42
    czero.eqz t0, a1, a2
    sw t0, 20(s0)

    # Test 6: czero.nez with negative / sign-extended rs1
    li a2, 0
    czero.nez t0, a1, a2
    sw t0, 24(s0)

    li a2, -99
    czero.nez t0, a1, a2
    sw t0, 28(s0)

    # Test 7: rs1 == rs2 == rd register reuse
    li t0, 0x55aa55aa
    czero.eqz t0, t0, t0
    sw t0, 32(s0)

    li t0, 0x55aa55aa
    czero.nez t0, t0, t0
    sw t0, 36(s0)

RVTEST_CODE_END

RVTEST_DATA_BEGIN
    .space 40
RVTEST_DATA_END
