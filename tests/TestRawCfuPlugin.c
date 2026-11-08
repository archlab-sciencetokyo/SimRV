/**
 * @file TestRawCfuPlugin.c
 * @brief Standalone CFU-Proving-Ground / CFU-Playground shared library fixture.
 *
 * Contains NO SimRV-specific headers or symbols. Exports only standard `cfu_op` and `cfu_reset`.
 */
#include <stdint.h>

static uint32_t s_reset_calls = 0;

uint32_t cfu_op(int funct3, int funct7, uint32_t rs1, uint32_t rs2) {
    if (funct3 == 0) {
        return (rs1 + rs2) ^ (uint32_t)funct7;
    }
    return (rs1 * rs2) + (uint32_t)funct7 + s_reset_calls;
}

void cfu_reset(void) { s_reset_calls++; }
