/**
 * @file simrv_h_mmu.c
 * @brief Two-stage MMU (G-stage) verification test for the H extension.
 *
 * Sets up SV39x4 G-stage page tables mapping GPA 0xC0000000-page
 * to the HPA of test_var.  Enters VS-mode via mret, loads through
 * the G-stage mapping, traps back to M-mode on ebreak, and checks
 * that the value read equals the expected magic number.
 */
#include "common.h"

/* ---------- page-table storage ---------------------------------------- */
__attribute__((aligned(16384))) uint64_t gstage_root_pt[2048];   /* SV39x4 root */
__attribute__((aligned(4096)))  uint64_t gstage_l1_pt[512];
__attribute__((aligned(4096)))  uint64_t gstage_l0_pt[512];

/* ---------- test data ------------------------------------------------- */
volatile uint32_t test_var   = 0x12345678;
volatile uint64_t result_val = 0;
volatile uint64_t trap_cause = 0;

/* ---------- VS-mode round-trip ---------------------------------------- */
void run_vs_test(uint64_t gpa, uint64_t hgatp_val) {
    __asm__ volatile(
        /* ---- 1. Set mtvec to trap handler (label 1f) ---- */
        "la   t0, 1f\n\t"
        "csrw mtvec, t0\n\t"

        /* ---- 2. Program hgatp ---- */
        "csrw 0x680, %1\n\t"

        /* ---- 3. Set mstatus MPP=S, MPV=1 ---- */
        "csrr t1, mstatus\n\t"
        "li   t2, ~(0x3 << 11)\n\t"
        "and  t1, t1, t2\n\t"
        "li   t2, (1 << 11)\n\t"
        "or   t1, t1, t2\n\t"
        "li   t2, (1ULL << 39)\n\t"
        "or   t1, t1, t2\n\t"
        "csrw mstatus, t1\n\t"

        /* ---- 4. Set mepc → VS-mode code (label 2f) ---- */
        "la   t0, 2f\n\t"
        "csrw mepc, t0\n\t"

        /* ---- 5. Enter VS-mode ---- */
        "mret\n"

        /* ---- VS-mode code ---- */
        ".balign 4\n"
        "2:\n\t"
        "lw   %0, 0(%0)\n\t"
        "ebreak\n\t"

        /* ---- M-mode trap handler ---- */
        ".balign 4\n"
        "1:\n\t"
        "csrr t0, mcause\n\t"
        "la   t1, trap_cause\n\t"
        "sd   t0, 0(t1)\n\t"

        /* Restore M-mode: clear MPV, set MPP=0 (so priv stays M on fall-through) */
        "csrr t1, mstatus\n\t"
        "li   t2, ~(1ULL << 39)\n\t"
        "and  t1, t1, t2\n\t"
        "li   t2, ~(0x3 << 11)\n\t"
        "and  t1, t1, t2\n\t"
        "csrw mstatus, t1\n\t"
        : "+r"(gpa)
        : "r"(hgatp_val)
        : "t0", "t1", "t2", "memory"
    );
    result_val = gpa;
}

/* ---------- main ------------------------------------------------------ */
int main(void) {
    puts("Two-stage MMU verification test start");

    /* clear page tables */
    for (int i = 0; i < 2048; i++) gstage_root_pt[i] = 0;
    for (int i = 0; i < 512;  i++) { gstage_l1_pt[i] = 0; gstage_l0_pt[i] = 0; }

    /* Identity-map 0x80000000–0xBFFFFFFF (code/data region, 1 GiB superpage) */
    gstage_root_pt[2] = (0x80000ULL << 10) | 0xDF;

    /* Map GPA 0xC0000000-page → HPA of test_var (3-level walk) */
    gstage_root_pt[3] = (((uint64_t)gstage_l1_pt >> 12) << 10) | 0x1;
    gstage_l1_pt[0]   = (((uint64_t)gstage_l0_pt >> 12) << 10) | 0x1;
    gstage_l0_pt[0]   = (((uint64_t)&test_var >> 12) << 10) | 0xDF;

    /* hgatp: Mode=8 (SV39x4), VMID=1, PPN */
    uint64_t root_ppn  = (uint64_t)gstage_root_pt >> 12;
    uint64_t hgatp_val = (8ULL << 60) | (1ULL << 44) | root_ppn;

    uint64_t page_offset = (uint64_t)&test_var & 0xFFF;
    printf("hgatp=0x%lx  test_var HPA=0x%lx  page_offset=0x%lx\n",
           hgatp_val, (uint64_t)&test_var, page_offset);

    /* Load from GPA 0xC0000000 + page_offset (same intra-page offset as test_var) */
    run_vs_test(0xC0000000ULL | page_offset, hgatp_val);

    printf("trap_cause=0x%lx  result_val=0x%lx (expected 0x12345678)\n",
           trap_cause, result_val);

    if (result_val == 0x12345678 && trap_cause == 0x3) {
        puts("PASS simrv_h_mmu");
        return 0;
    }
    puts("FAIL simrv_h_mmu");
    return 1;
}
