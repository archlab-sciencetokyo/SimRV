/**
 * @file cfu_plugin.c
 * @brief Reference Custom Function Unit (CFU) plugin demonstrating SIMD acceleration.
 *
 * Implements:
 *   funct3 = 0: 4x int8 SIMD dot product (rs1 . rs2)
 *   funct3 = 1: 4x uint8 Sum of Absolute Differences (SAD)
 *   funct3 = 2: Bitwise Hamming distance (popcount(rs1 ^ rs2))
 *   funct3 = 3: Multiply-accumulate into internal accumulator
 *
 * Compatible with both SimRV CFU v2 ABI and CFU-Proving-Ground / CFU-Playground.
 */
#include <stdint.h>
#include <stdlib.h>

#include "simrv/execute/ExternalCfu.h"

static uint64_t s_accumulator = 0;

/* Helper: 4x int8 dot product */
static inline int32_t simd_dot_product(uint32_t a, uint32_t b) {
    int8_t a0 = (int8_t)(a & 0xff);
    int8_t a1 = (int8_t)((a >> 8) & 0xff);
    int8_t a2 = (int8_t)((a >> 16) & 0xff);
    int8_t a3 = (int8_t)((a >> 24) & 0xff);

    int8_t b0 = (int8_t)(b & 0xff);
    int8_t b1 = (int8_t)((b >> 8) & 0xff);
    int8_t b2 = (int8_t)((b >> 16) & 0xff);
    int8_t b3 = (int8_t)((b >> 24) & 0xff);

    return ((int32_t)a0 * b0) + ((int32_t)a1 * b1) + ((int32_t)a2 * b2) + ((int32_t)a3 * b3);
}

/* Helper: 4x uint8 sum of absolute differences */
static inline uint32_t simd_sad(uint32_t a, uint32_t b) {
    uint32_t sum = 0;
    for (int i = 0; i < 4; ++i) {
        uint8_t ba = (uint8_t)((a >> (i * 8)) & 0xff);
        uint8_t bb = (uint8_t)((b >> (i * 8)) & 0xff);
        sum += (ba > bb) ? (ba - bb) : (bb - ba);
    }
    return sum;
}

/* CFU-Proving-Ground / CFU-Playground compatible entry point */
uint32_t cfu_op(int funct3, int funct7, uint32_t rs1, uint32_t rs2) {
    (void)funct7;
    switch (funct3) {
        case 0:
            return (uint32_t)simd_dot_product(rs1, rs2);
        case 1:
            return simd_sad(rs1, rs2);
        case 2:
            return (uint32_t)__builtin_popcount(rs1 ^ rs2);
        case 3:
            s_accumulator += (uint64_t)rs1 * (uint64_t)rs2;
            return (uint32_t)s_accumulator;
        default:
            return rs1 | rs2;
    }
}

void cfu_reset(void) { s_accumulator = 0; }

/* SimRV CFU v2 ABI entry points */
static uint32_t cfu_v2_execute(uint32_t funct7, uint32_t funct3, uint32_t src1, uint32_t src2,
                               uint32_t* latency_cycles) {
    if (latency_cycles != nullptr) {
        // Dot product and popcount take 1 cycle; SAD takes 2 cycles
        *latency_cycles = (funct3 == 1) ? 2 : 1;
    }
    return cfu_op((int)funct3, (int)funct7, src1, src2);
}

static uint64_t cfu_v2_execute64(uint32_t funct7, uint32_t funct3, uint64_t src1, uint64_t src2,
                                 uint32_t* latency_cycles) {
    if (latency_cycles != nullptr) {
        *latency_cycles = (funct3 == 1) ? 2 : 1;
    }
    if (funct3 == 2) {
        return (uint64_t)__builtin_popcountll(src1 ^ src2);
    }
    if (funct3 == 3) {
        s_accumulator += src1 * src2;
        return s_accumulator;
    }
    // For 32-bit SIMD operations in 64-bit mode, operate on lower 32 bits
    return (uint64_t)cfu_op((int)funct3, (int)funct7, (uint32_t)src1, (uint32_t)src2);
}

static uint32_t cfu_v2_query_latency(uint32_t funct7, uint32_t funct3) {
    (void)funct7;
    return (funct3 == 1) ? 2 : 1;
}

static const struct SimRvCfuPlugin s_plugin_descriptor = {
    .api_version = SIMRV_CFU_PLUGIN_API_VERSION,
    .execute = cfu_v2_execute,
    .reset = cfu_reset,
    .execute64 = cfu_v2_execute64,
    .query_latency = cfu_v2_query_latency,
    .name = "reference-simd-cfu",
};

const struct SimRvCfuPlugin* simrv_cfu_init(void) { return &s_plugin_descriptor; }
