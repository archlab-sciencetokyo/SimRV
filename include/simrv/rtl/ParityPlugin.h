/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Archlab, Science Tokyo
 */

#ifndef SIMRV_RTL_PARITY_PLUGIN_H
#define SIMRV_RTL_PARITY_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Execution context provided by SimRV to an RTL target adapter plugin.
 */
typedef struct SimRvRtlContext {
    const char* work_dir;
    const char* simrv_binary;
    const char* target_dir;
    const char* target_binary;
    const char* verilator_binary;
    const char* gcc_binary;
    const char* objcopy_binary;
    const char* objdump_binary;
    const char* trace_dir;
    const char* config_path;
    int verbose;
} SimRvRtlContext;

/**
 * @brief Results of running one benchmark comparison against RTL.
 */
typedef struct SimRvParityResult {
    const char* benchmark_name;
    uint64_t rtl_cycles;
    uint64_t rtl_instructions;
    uint64_t simrv_cycles;
    uint64_t simrv_instructions;
    int matched;
    uint64_t divergence_cycle;
    char error_message[512];
} SimRvParityResult;

/**
 * @brief RTL Parity target descriptor exposed by plugins.
 */
typedef struct SimRvRtlTarget {
    const char* name;
    const char* description;

    /**
     * @brief Build or prepare RTL simulator artifacts.
     * @return 0 on success, non-zero on error.
     */
    int (*prepare)(const SimRvRtlContext* ctx);

    /**
     * @brief Run parity evaluation for a single benchmark ELF.
     * @return 0 on success, non-zero on error.
     */
    int (*run_benchmark)(const SimRvRtlContext* ctx, const char* bench_name, const char* elf_path,
                         SimRvParityResult* out_result);

    /**
     * @brief Cleanup simulator or temporary resources.
     */
    void (*cleanup)(const SimRvRtlContext* ctx);
} SimRvRtlTarget;

/**
 * @brief Standard entrypoint exported by external RTL target plugins (.so).
 */
typedef const SimRvRtlTarget* (*SimRvRtlPluginInitFn)(void);

#ifdef __cplusplus
}
#endif

#endif  // SIMRV_RTL_PARITY_PLUGIN_H
