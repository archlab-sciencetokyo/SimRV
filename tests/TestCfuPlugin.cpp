/**
 * @file TestCfuPlugin.cpp
 * @brief Dynamic CFU plugin test fixture for CfuProvingGroundTests.
 */
#include <cstdint>

#include "simrv/execute/CfuUnit.hpp"

namespace {

uint32_t test_cfu_execute(uint32_t funct7, uint32_t funct3, uint32_t src1, uint32_t src2,
                          uint32_t* latency_cycles) {
    if (latency_cycles != nullptr) {
        *latency_cycles = (funct3 == 1) ? 3 : 1;
    }
    return (src1 ^ src2) + funct7;
}

void test_cfu_reset() {}

const simrv::execute::SimRvCfuPlugin kPlugin = {
    .execute = test_cfu_execute,
    .reset = test_cfu_reset,
};

}  // namespace

extern "C" {

const simrv::execute::SimRvCfuPlugin* simrv_cfu_init() { return &kPlugin; }

}  // extern "C"
