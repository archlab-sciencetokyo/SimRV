#include "simrv/execute/ExternalCfu.h"

static uint64_t execute64(uint32_t f7, uint32_t f3, uint64_t a, uint64_t b, uint32_t* latency) {
    (void)f7;
    (void)f3;
    *latency = 4;
    return a ^ b;
}

static const SimRvCfuPlugin plugin = {
    .api_version = SIMRV_CFU_PLUGIN_API_VERSION,
    .execute64 = execute64,
};

const SimRvCfuPlugin* simrv_cfu_init(void) { return &plugin; }
