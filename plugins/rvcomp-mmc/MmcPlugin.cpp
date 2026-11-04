/**
 * @file MmcPlugin.cpp
 * @brief External device simulation plugin for rvcomp-mmc (isct,rvcomp-mmc).
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "simrv/device/ExternalDevice.h"

namespace {

struct MmcContext {
    std::string backing_path;
    FILE* file{nullptr};
    uint32_t sector{0};
    std::vector<uint8_t> buffer;
    bool buffer_dirty{false};
    uint32_t buffered_sector{0xffffffffU};
};

void sync_buffer(MmcContext* ctx) {
    if (ctx == nullptr || ctx->file == nullptr || !ctx->buffer_dirty) return;
    const uint64_t file_offset =
        0x2000000ULL + static_cast<uint64_t>(ctx->buffered_sector) * 512ULL;
    fseek(ctx->file, static_cast<long>(file_offset), SEEK_SET);
    fwrite(ctx->buffer.data(), 1, 4096, ctx->file);
    fflush(ctx->file);
    ctx->buffer_dirty = false;
}

void load_buffer(MmcContext* ctx, uint32_t sector) {
    if (ctx == nullptr) return;
    if (ctx->buffered_sector == sector) return;
    sync_buffer(ctx);
    ctx->buffered_sector = sector;
    ctx->buffer.assign(4096, 0);
    if (ctx->file != nullptr) {
        const uint64_t file_offset = 0x2000000ULL + static_cast<uint64_t>(sector) * 512ULL;
        fseek(ctx->file, static_cast<long>(file_offset), SEEK_SET);
        (void)fread(ctx->buffer.data(), 1, 4096, ctx->file);
    }
}

int mmc_init(void** out_ctx, const char* args) {
    auto* ctx = new MmcContext();
    ctx->buffer.assign(4096, 0);
    if (args != nullptr && *args != '\0') {
        ctx->backing_path = args;
        ctx->file = fopen(args, "r+b");
        if (ctx->file == nullptr) {
            ctx->file = fopen(args, "rb");
        }
    }
    *out_ctx = ctx;
    return 0;
}

void mmc_reset(void* raw_ctx) {
    auto* ctx = static_cast<MmcContext*>(raw_ctx);
    if (ctx == nullptr) return;
    sync_buffer(ctx);
    ctx->sector = 0;
    ctx->buffered_sector = 0xffffffffU;
    ctx->buffer_dirty = false;
}

int mmc_read(void* raw_ctx, uint64_t offset, uint32_t size, uint64_t* out_val) {
    auto* ctx = static_cast<MmcContext*>(raw_ctx);
    if (ctx == nullptr || out_val == nullptr) return -1;
    *out_val = 0;
    if (offset == 0) {
        *out_val = ctx->sector;
        return 0;
    }
    if (offset >= 0x1000 && offset < 0x2000) {
        load_buffer(ctx, ctx->sector);
        const uint64_t win_offset = offset - 0x1000;
        if (win_offset + size <= ctx->buffer.size()) {
            uint64_t val = 0;
            std::memcpy(&val, ctx->buffer.data() + win_offset, size);
            *out_val = val;
            return 0;
        }
    }
    return 0;
}

int mmc_write(void* raw_ctx, uint64_t offset, uint32_t size, uint64_t val) {
    auto* ctx = static_cast<MmcContext*>(raw_ctx);
    if (ctx == nullptr) return -1;
    if (offset == 0) {
        sync_buffer(ctx);
        ctx->sector = static_cast<uint32_t>(val);
        return 0;
    }
    if (offset >= 0x1000 && offset < 0x2000) {
        load_buffer(ctx, ctx->sector);
        const uint64_t win_offset = offset - 0x1000;
        if (win_offset + size <= ctx->buffer.size()) {
            std::memcpy(ctx->buffer.data() + win_offset, &val, size);
            ctx->buffer_dirty = true;
            return 0;
        }
    }
    return 0;
}

void mmc_destroy(void* raw_ctx) {
    auto* ctx = static_cast<MmcContext*>(raw_ctx);
    if (ctx == nullptr) return;
    sync_buffer(ctx);
    if (ctx->file != nullptr) {
        fclose(ctx->file);
    }
    delete ctx;
}

const SimRvExternalDevice kDevice = {
    .api_version = SIMRV_EXTERNAL_DEVICE_API_VERSION,
    .name = "rvcomp-mmc",
    .compatible = "isct,rvcomp-mmc",
    .default_base = 0xa0000000ULL,
    .default_size = 0x2000ULL,
    .init = mmc_init,
    .reset = mmc_reset,
    .read = mmc_read,
    .write = mmc_write,
    .destroy = mmc_destroy,
    .set_host_services = nullptr,
};

}  // namespace

extern "C" {
const SimRvExternalDevice* simrv_device_plugin_init() { return &kDevice; }
}
