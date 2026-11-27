/**
 * @file ExternalCfu.h
 * @brief Versioned C ABI for dynamic Custom Function Unit (CFU) plugins.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIMRV_CFU_PLUGIN_API_VERSION_V1 1
#define SIMRV_CFU_PLUGIN_API_VERSION 2

typedef uint32_t (*SimRvCfuExecuteFn)(uint32_t funct7, uint32_t funct3, uint32_t src1,
                                      uint32_t src2, uint32_t* latency_cycles);
typedef void (*SimRvCfuResetFn)(void);
typedef uint64_t (*SimRvCfuExecute64Fn)(uint32_t funct7, uint32_t funct3, uint64_t src1,
                                        uint64_t src2, uint32_t* latency_cycles);
typedef uint32_t (*SimRvCfuQueryLatencyFn)(uint32_t funct7, uint32_t funct3);

/**
 * @brief Plugin descriptor returned by `simrv_cfu_init()`.
 */
struct SimRvCfuPlugin {
    uint32_t api_version;
    SimRvCfuExecuteFn execute;
    SimRvCfuResetFn reset;
    SimRvCfuExecute64Fn execute64;
    SimRvCfuQueryLatencyFn query_latency;
    const char* name;
};

#ifdef __cplusplus
typedef ::SimRvCfuPlugin SimRvCfuPlugin;
#else
typedef struct SimRvCfuPlugin SimRvCfuPlugin;
#endif

typedef const SimRvCfuPlugin* (*SimRvCfuInitFn)(void);

#ifdef __cplusplus
}
#endif
