/**
 * @file ResetPlugin.cpp
 * @brief External device simulation plugin for rvcomp-reset (isct,rvcomp-reset).
 */
#include <cstdint>

#include "simrv/device/ExternalDevice.h"

namespace {

struct ResetContext {
    const SimRvHostServices* services{nullptr};
};

int reset_init(void** out_ctx, const char* /*args*/) {
    auto* ctx = new ResetContext();
    *out_ctx = ctx;
    return 0;
}

void reset_reset(void* /*raw_ctx*/) {}

int reset_read(void* /*raw_ctx*/, uint64_t /*offset*/, uint32_t /*size*/, uint64_t* out_val) {
    if (out_val != nullptr) *out_val = 0;
    return 0;
}

int reset_write(void* raw_ctx, uint64_t offset, uint32_t /*size*/, uint64_t /*val*/) {
    auto* ctx = static_cast<ResetContext*>(raw_ctx);
    if (offset == 0 && ctx != nullptr && ctx->services != nullptr &&
        ctx->services->request_reboot != nullptr) {
        ctx->services->request_reboot(ctx->services->host_ctx);
    }
    return 0;
}

void reset_destroy(void* raw_ctx) { delete static_cast<ResetContext*>(raw_ctx); }

void reset_set_services(void* raw_ctx, const SimRvHostServices* services) {
    auto* ctx = static_cast<ResetContext*>(raw_ctx);
    if (ctx != nullptr) {
        ctx->services = services;
    }
}

const SimRvExternalDevice kDevice = {
    .api_version = SIMRV_EXTERNAL_DEVICE_API_VERSION,
    .name = "rvcomp-reset",
    .compatible = "isct,rvcomp-reset",
    .default_base = 0x10000100ULL,
    .default_size = 4ULL,
    .init = reset_init,
    .reset = reset_reset,
    .read = reset_read,
    .write = reset_write,
    .destroy = reset_destroy,
    .set_host_services = reset_set_services,
};

}  // namespace

extern "C" {
const SimRvExternalDevice* simrv_device_plugin_init() { return &kDevice; }
}
