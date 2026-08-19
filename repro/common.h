#ifndef RVV_HANDSON_COMMON_H
#define RVV_HANDSON_COMMON_H

#include <stddef.h>
#include <stdint.h>

#ifdef RVV_BAREMETAL
#include "runtime.h"
#else
#include <stdio.h>
#endif

#ifndef RVV_N
#define RVV_N 4099
#endif

#ifndef RVV_ROWS
#define RVV_ROWS 257
#endif

#ifndef RVV_COLS
#define RVV_COLS 67
#endif

#ifndef RVV_PRINT_SAMPLES
#define RVV_PRINT_SAMPLES 1
#endif

#ifndef RVV_SAMPLE_COUNT
#define RVV_SAMPLE_COUNT 4
#endif

#define NOINLINE __attribute__((noinline, noclone))

#define DISASM_MARK(label) \
    __asm__ volatile(".balign 4\n.global " #label "\n" #label ":\n" ::: "memory")

static inline uint32_t rvv_mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline uint32_t rvv_runtime_salt(int argc)
{
    volatile uint32_t seed = (uint32_t)argc * 747796405U + 2891336453U;
    return rvv_mix32(seed ^ 0x9e3779b9U);
}

static inline int32_t rvv_i32(size_t i, uint32_t salt)
{
    uint32_t x = rvv_mix32((uint32_t)i * 2246822519U + salt);
    return (int32_t)(x & 0xffffU) - 32768;
}

static inline int16_t rvv_i16(size_t i, uint32_t salt)
{
    return (int16_t)(rvv_i32(i, salt) & 0x3fff);
}

static inline int8_t rvv_i8(size_t i, uint32_t salt)
{
    return (int8_t)(rvv_mix32((uint32_t)i + salt) & 0x7f);
}

static inline float rvv_f32(size_t i, uint32_t salt)
{
    int32_t v = rvv_i32(i, salt) % 2048;
    return (float)v * 0.03125f;
}

#ifdef RVV_ENABLE_F16
static inline _Float16 rvv_f16(size_t i, uint32_t salt)
{
    int32_t v = rvv_i32(i, salt) % 256;
    return (_Float16)v * (_Float16)0.125f;
}
#endif

static inline uint64_t rvv_checksum_u64_step(uint64_t h, uint64_t x)
{
    h ^= x;
    h *= 1099511628211ULL;
    return h;
}

static inline uint64_t rvv_checksum_i32(const int32_t *x, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) {
        h = rvv_checksum_u64_step(h, (uint32_t)x[i]);
    }
    return h;
}

static inline uint64_t rvv_checksum_i8(const int8_t *x, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) {
        h = rvv_checksum_u64_step(h, (uint8_t)x[i]);
    }
    return h;
}

static inline uint64_t rvv_checksum_f32(const float *x, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) {
        union {
            float f;
            uint32_t u;
        } bits = { .f = x[i] };
        h = rvv_checksum_u64_step(h, bits.u);
    }
    return h;
}

#ifdef RVV_ENABLE_F16
static inline uint64_t rvv_checksum_f16(const _Float16 *x, size_t n)
{
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) {
        union {
            _Float16 f;
            uint16_t u;
        } bits = { .f = x[i] };
        h = rvv_checksum_u64_step(h, bits.u);
    }
    return h;
}
#endif

static inline void rvv_print_checksum(const char *name, uint64_t checksum)
{
    printf("%s checksum=0x%016llx\n", name, (unsigned long long)checksum);
}

static inline size_t rvv_sample_head_count(size_t n)
{
    return n < RVV_SAMPLE_COUNT ? n : RVV_SAMPLE_COUNT;
}

static inline void rvv_print_sample_gap(size_t head, size_t n)
{
    if (n > head + 1U) {
        printf(" ...");
    }
}

static inline void rvv_print_sample_i32(const char *name, const int32_t *x, size_t n)
{
#if RVV_PRINT_SAMPLES
    size_t head = rvv_sample_head_count(n);
    printf("%s n=%zu sample:", name, n);
    for (size_t i = 0; i < head; i++) {
        printf(" [%zu]=%d", i, x[i]);
    }
    if (n > head) {
        rvv_print_sample_gap(head, n);
        printf(" [%zu]=%d", n - 1U, x[n - 1U]);
    }
    putchar('\n');
#else
    (void)name;
    (void)x;
    (void)n;
#endif
}

static inline void rvv_print_sample_i8(const char *name, const int8_t *x, size_t n)
{
#if RVV_PRINT_SAMPLES
    size_t head = rvv_sample_head_count(n);
    printf("%s n=%zu sample:", name, n);
    for (size_t i = 0; i < head; i++) {
        printf(" [%zu]=%d", i, (int)x[i]);
    }
    if (n > head) {
        rvv_print_sample_gap(head, n);
        printf(" [%zu]=%d", n - 1U, (int)x[n - 1U]);
    }
    putchar('\n');
#else
    (void)name;
    (void)x;
    (void)n;
#endif
}

static inline void rvv_print_sample_f32(const char *name, const float *x, size_t n)
{
#if RVV_PRINT_SAMPLES
    size_t head = rvv_sample_head_count(n);
    printf("%s n=%zu sample:", name, n);
    for (size_t i = 0; i < head; i++) {
        printf(" [%zu]=%f", i, x[i]);
    }
    if (n > head) {
        rvv_print_sample_gap(head, n);
        printf(" [%zu]=%f", n - 1U, x[n - 1U]);
    }
    putchar('\n');
#else
    (void)name;
    (void)x;
    (void)n;
#endif
}

#ifdef RVV_ENABLE_F16
static inline void rvv_print_sample_f16(const char *name, const _Float16 *x, size_t n)
{
#if RVV_PRINT_SAMPLES
    size_t head = rvv_sample_head_count(n);
    printf("%s n=%zu sample:", name, n);
    for (size_t i = 0; i < head; i++) {
        printf(" [%zu]=%f", i, (double)x[i]);
    }
    if (n > head) {
        rvv_print_sample_gap(head, n);
        printf(" [%zu]=%f", n - 1U, (double)x[n - 1U]);
    }
    putchar('\n');
#else
    (void)name;
    (void)x;
    (void)n;
#endif
}
#endif

#endif
