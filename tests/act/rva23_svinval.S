#include "model_test.h"
#include "arch_test.h"

RVTEST_ISA("RV64I_Svinval")
RVTEST_CODE_BEGIN

    la s0, begin_signature

    # 1. Test sfence.w.inval
    sfence.w.inval
    li t0, 0x11112222
    sw t0, 0(s0)

    # 2. Test sinval.vma with virtual address & ASID
    li a0, 0x80000000
    li a1, 0x0
    sinval.vma a0, a1
    li t1, 0x33334444
    sw t1, 4(s0)

    # 3. Test sinval.vma with all-zero registers (all addresses, all ASIDs)
    sinval.vma zero, zero
    li t2, 0x55556666
    sw t2, 8(s0)

    # 4. Test sfence.inval.ir
    sfence.inval.ir
    li t3, 0x77778888
    sw t3, 12(s0)

RVTEST_CODE_END

RVTEST_DATA_BEGIN
    .space 16
RVTEST_DATA_END
