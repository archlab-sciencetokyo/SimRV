#include <riscv_vector.h>       /* RVV intrinsic declarations. */
#include "common.h"             /* Bare-metal printf/puts and shared helpers. */

/*
 * Reproducer for SimRV beta13 `vlse32.v`.
 * It gathers column 2 from a 4x4 matrix. The correct result is
 * 2, 102, 202, 302; beta13 appears to load contiguous elements instead.
 */
enum {
    ROWS = 4,      /* Use four matrix rows. */
    COLS = 4,      /* Use four int32_t values per row. */
    COL_INDEX = 2, /* Gather the third column from each row. */
};

NOINLINE /* Keep this function visible in the focused objdump. */
void gather_vlse32(const int32_t *matrix, int32_t *out) /* Gather one column into out[]. */
{
    /* RVV strided-load intrinsics take the stride in bytes. */
    ptrdiff_t stride = (ptrdiff_t)(COLS * sizeof(int32_t)); /* One full row in bytes. */
    size_t vl = __riscv_vsetvl_e32m1(ROWS);                 /* Read all four rows at once. */
    const int32_t *base = &matrix[COL_INDEX];               /* Start at matrix[0][2]. */
    vint32m1_t x = __riscv_vlse32_v_i32m1(base, stride, vl); /* Load one row apart per lane. */
    __riscv_vse32_v_i32m1(out, x, vl);                      /* Store the gathered lanes. */
}

int main(void) /* Bare-metal entry point called by the runtime. */
{
    static int32_t matrix[ROWS * COLS]; /* 4x4 row-major input matrix. */
    static int32_t out[ROWS];           /* One gathered value per row. */

    /* Distinct row ranges make wrong contiguous loads easy to spot. */
    for (size_t r = 0; r < ROWS; r++) {       /* Fill each row. */
        for (size_t c = 0; c < COLS; c++) {   /* Fill each column. */
            matrix[r * COLS + c] = (int32_t)(r * 100 + c); /* Row 1 becomes 100..103. */
        }
        out[r] = -1;                          /* Initialize output before the RVV load. */
    }

    puts("vlse32 repro begin"); /* Marker showing the program reached the test. */
    gather_vlse32(matrix, out);  /* Execute the function containing `vlse32.v`. */

    int fail = 0;                               /* Track any lane mismatch. */
    for (size_t r = 0; r < ROWS; r++) {         /* Verify every gathered row. */
        int32_t expected = matrix[r * COLS + COL_INDEX]; /* Scalar reference value. */
        printf("row=%zu got=%d expected=%d\n", r, out[r], expected); /* Print the lane result. */
        if (out[r] != expected) {               /* Detect a bad strided-load result. */
            fail = 1;                           /* Remember that the test failed. */
        }
    }

    if (fail) {             /* Report failure through stdout and the test finisher. */
        puts("FAIL vlse32"); /* SimRV beta13 reaches this path. */
        return 1;           /* Nonzero status becomes a finisher failure. */
    }

    puts("PASS vlse32"); /* Expected on a simulator with correct `vlse32.v`. */
    return 0;            /* Zero status becomes a finisher pass. */
}
