#include <riscv_vector.h>       /* RVV intrinsic declarations. */
#include "common.h"             /* Bare-metal printf/puts and shared helpers. */

/*
 * Reproducer for SimRV beta13 `vluxei32.v`.
 * It converts an index vector to byte offsets and gathers from table[].
 * The correct result is 1000, 1030, 1050, 1070.
 */
enum {
    TABLE_N = 8, /* Keep eight table entries. */
    N = 4,       /* Gather four indexes. */
};

NOINLINE /* Keep this function visible in the focused objdump. */
void lookup_vluxei32(const int32_t *table, const uint32_t *index, int32_t *out) /* Gather table[] by index[]. */
{
    size_t vl = __riscv_vsetvl_e32m1(N);                    /* Read all four indexes at once. */
    vuint32m1_t idx = __riscv_vle32_v_u32m1(index, vl);      /* Load index[] into a vector. */
    vuint32m1_t offset = __riscv_vsll_vx_u32m1(idx, 2, vl);  /* Convert int32 indexes to byte offsets. */
    vint32m1_t x = __riscv_vluxei32_v_i32m1(table, offset, vl); /* Gather load by byte offset. */
    __riscv_vse32_v_i32m1(out, x, vl);                      /* Store the gathered result. */
}

int main(void) /* Bare-metal entry point called by the runtime. */
{
    static int32_t table[TABLE_N];             /* Source table for the gather. */
    static uint32_t index[N] = {0, 3, 5, 7};   /* Non-consecutive indexes expose offset handling. */
    static int32_t out[N];                     /* Gathered output values. */

    for (size_t i = 0; i < TABLE_N; i++) {     /* Fill table with easy-to-read values. */
        table[i] = (int32_t)(1000 + i * 10);   /* table[3] becomes 1030. */
    }
    for (size_t i = 0; i < N; i++) {           /* Initialize output. */
        out[i] = -1;                           /* Sentinel value before the gather. */
    }

    puts("vluxei32 repro begin");             /* Marker showing the program reached the test. */
    lookup_vluxei32(table, index, out);        /* Execute the function containing `vluxei32.v`. */

    int fail = 0;                              /* Track any lane mismatch. */
    for (size_t i = 0; i < N; i++) {           /* Verify every gathered lane. */
        int32_t expected = table[index[i]];    /* Scalar reference value. */
        printf("i=%zu idx=%u got=%d expected=%d\n", i, index[i], out[i], expected); /* Print the lane result. */
        if (out[i] != expected) {              /* Detect a bad indexed-load result. */
            fail = 1;                          /* Remember that the test failed. */
        }
    }

    if (fail) {                /* Report failure through stdout and the test finisher. */
        puts("FAIL vluxei32"); /* SimRV beta13 reaches this path. */
        return 1;              /* Nonzero status becomes a finisher failure. */
    }

    puts("PASS vluxei32"); /* Expected on a simulator with correct `vluxei32.v`. */
    return 0;              /* Zero status becomes a finisher pass. */
}
