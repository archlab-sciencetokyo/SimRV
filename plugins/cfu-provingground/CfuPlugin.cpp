/**
 * @file CfuPlugin.cpp
 * @brief Reference Custom Function Unit (CFU) plugin for Tokyo Tech Archlab CFU-ProvingGround
 * (rvproc).
 */
#include <cstdint>

#include "simrv/execute/CfuUnit.hpp"

using simrv::execute::SimRvCfuPlugin;

extern "C" {

static auto cfu_pg_execute(uint32_t funct7, uint32_t funct3, uint32_t src1, uint32_t src2,
                           uint32_t* latency_cycles) -> uint32_t {
    (void)funct7;
    (void)funct3;
    if (latency_cycles != nullptr) {
        *latency_cycles = 1;
    }
    // CFU-Proving-Ground Tokyo Tech rvproc_cfu: assign rslt_o = (en_i) ? src1_i | src2_i : 0;
    return src1 | src2;
}

static void cfu_pg_reset() {}

static const SimRvCfuPlugin kPlugin = {
    .api_version = SIMRV_CFU_PLUGIN_API_VERSION,
    .execute = cfu_pg_execute,
    .reset = cfu_pg_reset,
};

const SimRvCfuPlugin* simrv_cfu_init() { return &kPlugin; }
}
