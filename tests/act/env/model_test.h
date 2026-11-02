#ifndef _COMPLIANCE_MODEL_H
#define _COMPLIANCE_MODEL_H

#define RVMODEL_DATA_SECTION \
    .section .tohost,"aw",@progbits; \
    .align 6; \
    .globl tohost; \
    tohost: .dword 0; \
    .globl fromhost; \
    fromhost: .dword 0; \
    .section .data.signature,"aw",@progbits; \
    .align 4; \
    .globl rvtest_sig_begin; \
    .globl begin_signature; \
    rvtest_sig_begin: \
    begin_signature:

#define RVMODEL_BOOT \
    .section .text.init; \
    .globl _start; \
    _start: \
        la sp, stack_top;

#define RVMODEL_HALT \
    la t0, tohost; \
    li t1, 1; \
    sw t1, 0(t0); \
1:  wfi; \
    j 1b;

#define RVMODEL_DATA_BEGIN \
    RVMODEL_DATA_SECTION

#define RVMODEL_DATA_END \
    .globl rvtest_sig_end; \
    .globl end_signature; \
    rvtest_sig_end: \
    end_signature:

#endif // _COMPLIANCE_MODEL_H
