#include <riscv_vector.h>
#include "common.h"

/*
 * Reproducer for SimRV beta13 `vfmacc.vf`.
 * This computes y += alpha * x. beta13 prints `before` but does not reach
 * `after`, so the failure looks like a hang rather than a wrong result.
 */
enum {
    N = 4,
};

NOINLINE
void saxpy_vfmacc(float alpha, const float *x, float *y)
{
    size_t vl = __riscv_vsetvl_e32m1(N);
    vfloat32m1_t vx = __riscv_vle32_v_f32m1(x, vl);
    vfloat32m1_t vy = __riscv_vle32_v_f32m1(y, vl);

    /* Fused multiply-add with a scalar float: vy += alpha * vx. */
    vy = __riscv_vfmacc_vf_f32m1(vy, alpha, vx, vl);

    __riscv_vse32_v_f32m1(y, vy, vl);
}

int main(void)
{
    static float x[N];
    static float y[N];
    float alpha = 2.0f;

    for (size_t i = 0; i < N; i++) {
        x[i] = (float)(i + 1);
        y[i] = (float)(100 + i);
    }

    /* These markers distinguish an arithmetic mismatch from a simulator hang. */
    puts("vfmacc repro before");
    saxpy_vfmacc(alpha, x, y);
    puts("vfmacc repro after");

    for (size_t i = 0; i < N; i++) {
        float expected = (float)(100 + i) + alpha * (float)(i + 1);
        printf("i=%zu got=%f expected=%f\n", i, y[i], expected);
        if (y[i] != expected) {
            puts("FAIL vfmacc");
            return 1;
        }
    }

    puts("PASS vfmacc");
    return 0;
}
