#include "simrv/rtl/ParityPlugin.h"

static int prepare(const SimRvRtlContext* context) { return context == 0; }

static int run_benchmark(const SimRvRtlContext* context, const char* name, const char* elf,
                         SimRvParityResult* result) {
    if (context == 0 || name == 0 || elf == 0 || result == 0) return 1;
    result->benchmark_name = name;
    result->rtl_cycles = 17;
    result->rtl_instructions = 3;
    result->simrv_cycles = 19;
    result->simrv_instructions = 3;
    result->matched = 1;
    return 0;
}

static void cleanup(const SimRvRtlContext* context) { (void)context; }

const SimRvRtlTarget* simrv_rtl_plugin_init(void) {
    static const SimRvRtlTarget target = {
        .abi_version = SIMRV_RTL_PLUGIN_ABI_VERSION,
        .struct_size = sizeof(SimRvRtlTarget),
        .name = "test-target",
        .description = "generic parity ABI test target",
        .prepare = prepare,
        .run_benchmark = run_benchmark,
        .cleanup = cleanup,
    };
    return &target;
}
