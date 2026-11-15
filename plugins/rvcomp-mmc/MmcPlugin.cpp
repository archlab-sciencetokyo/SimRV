/**
 * @file MmcPlugin.cpp
 * @brief External device simulation plugin for rvcomp-mmc (isct,rvcomp-mmc).
 */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#include "simrv/device/ExternalDevice.h"

namespace {

constexpr uint64_t kWindowOffset = 0x1000ULL;
constexpr size_t kWindowSize = 4096U;
constexpr uint64_t kRawPartitionCutoffBytes = 0x2000000ULL;

constexpr auto is_valid_access_size(uint32_t size) noexcept -> bool {
    return size == 1U || size == 2U || size == 4U || size == 8U;
}

struct MmcContext {
    FILE* file{nullptr};
    uint64_t file_size{0};
    bool is_raw_partition{false};
    uint32_t sector{0};
    std::array<uint8_t, kWindowSize> buffer{};
    bool buffer_dirty{false};
    uint32_t buffered_sector{0xffffffffU};
    bool flush_done{true};
};

void sync_buffer(MmcContext* ctx) noexcept {
    if (ctx == nullptr || ctx->file == nullptr || !ctx->buffer_dirty) return;
    const uint32_t block = (ctx->is_raw_partition && ctx->buffered_sector >= 8192U)
                               ? (ctx->buffered_sector - 8192U)
                               : ctx->buffered_sector;
    const uint64_t file_offset = static_cast<uint64_t>(block) * kWindowSize;
    if (!ctx->is_raw_partition && file_offset < kRawPartitionCutoffBytes) {
        // Protect firmware/kernel region (first 32 MiB) from accidental overwrites
        ctx->buffer_dirty = false;
        return;
    }
    if (file_offset <= static_cast<uint64_t>(std::numeric_limits<off_t>::max()) &&
        fseeko(ctx->file, static_cast<off_t>(file_offset), SEEK_SET) == 0) {
        (void)fwrite(ctx->buffer.data(), 1, ctx->buffer.size(), ctx->file);
        (void)fflush(ctx->file);
    }
    ctx->buffer_dirty = false;
}

void load_buffer(MmcContext* ctx, uint32_t sector) noexcept {
    if (ctx == nullptr) return;
    if (ctx->buffered_sector == sector) return;
    sync_buffer(ctx);
    ctx->buffered_sector = sector;
    ctx->buffer.fill(0);
    if (ctx->file != nullptr) {
        const uint32_t block =
            (ctx->is_raw_partition && sector >= 8192U) ? (sector - 8192U) : sector;
        const uint64_t file_offset = static_cast<uint64_t>(block) * kWindowSize;
        if (file_offset < ctx->file_size &&
            file_offset <= static_cast<uint64_t>(std::numeric_limits<off_t>::max()) &&
            fseeko(ctx->file, static_cast<off_t>(file_offset), SEEK_SET) == 0) {
            (void)fread(ctx->buffer.data(), 1, ctx->buffer.size(), ctx->file);
        }
    }
}

int mmc_init(void** out_ctx, const char* args) noexcept {
    if (out_ctx == nullptr) return -1;
    *out_ctx = nullptr;
    auto* ctx = new (std::nothrow) MmcContext();
    if (ctx == nullptr) return -1;
    if (args != nullptr && *args != '\0') {
        ctx->file = fopen(args, "r+b");
        if (ctx->file == nullptr) {
            ctx->file = fopen(args, "rb");
        }
        if (ctx->file != nullptr) {
            if (fseeko(ctx->file, 0, SEEK_END) == 0) {
                const off_t end_pos = ftello(ctx->file);
                if (end_pos >= 0) {
                    ctx->file_size = static_cast<uint64_t>(end_pos);
                }
            }
            (void)fseeko(ctx->file, 0, SEEK_SET);

            // Check if image is a standalone partition or whole disk with 32 MiB offset
            if (ctx->file_size < kRawPartitionCutoffBytes) {
                ctx->is_raw_partition = true;
            } else {
                uint16_t magic_start = 0;
                uint16_t magic_32m = 0;
                if (fseeko(ctx->file, 0x438, SEEK_SET) == 0) {
                    (void)fread(&magic_start, sizeof(magic_start), 1, ctx->file);
                }
                if (fseeko(ctx->file, 0x2000438, SEEK_SET) == 0) {
                    (void)fread(&magic_32m, sizeof(magic_32m), 1, ctx->file);
                }
                if (magic_start == 0xef53U && magic_32m != 0xef53U) {
                    ctx->is_raw_partition = true;
                }
            }
        }
    }
    *out_ctx = ctx;
    return 0;
}

void mmc_reset(void* raw_ctx) noexcept {
    auto* ctx = static_cast<MmcContext*>(raw_ctx);
    if (ctx == nullptr) return;
    sync_buffer(ctx);
    ctx->sector = 0;
    ctx->buffered_sector = 0xffffffffU;
    ctx->buffer_dirty = false;
}

int mmc_read(void* raw_ctx, uint64_t offset, uint32_t size, uint64_t* out_val) noexcept {
    auto* ctx = static_cast<MmcContext*>(raw_ctx);
    if (ctx == nullptr || out_val == nullptr || !is_valid_access_size(size)) return -1;
    *out_val = 0;
    if (offset == 0) {
        *out_val = ctx->sector;
        return 0;
    }
    if (offset == 0x1c) {
        *out_val = ctx->flush_done ? 1U : 0U;
        return 0;
    }
    if (offset >= kWindowOffset && offset < kWindowOffset + kWindowSize) {
        load_buffer(ctx, ctx->sector);
        const uint64_t win_offset = offset - kWindowOffset;
        if (size <= ctx->buffer.size() - win_offset) {
            uint64_t val = 0;
            std::memcpy(&val, ctx->buffer.data() + win_offset, size);
            *out_val = val;
            return 0;
        }
    }
    return 0;
}

int mmc_write(void* raw_ctx, uint64_t offset, uint32_t size, uint64_t val) noexcept {
    auto* ctx = static_cast<MmcContext*>(raw_ctx);
    if (ctx == nullptr || !is_valid_access_size(size)) return -1;
    if (offset == 0) {
        sync_buffer(ctx);
        ctx->sector = static_cast<uint32_t>(val);
        return 0;
    }
    if (offset == 0x18) {
        sync_buffer(ctx);
        ctx->flush_done = true;
        return 0;
    }
    if (offset == 0x20) {
        ctx->flush_done = false;
        return 0;
    }
    if (offset >= kWindowOffset && offset < kWindowOffset + kWindowSize) {
        load_buffer(ctx, ctx->sector);
        const uint64_t win_offset = offset - kWindowOffset;
        if (size <= ctx->buffer.size() - win_offset) {
            std::memcpy(ctx->buffer.data() + win_offset, &val, size);
            ctx->buffer_dirty = true;
            return 0;
        }
    }
    return 0;
}

void mmc_destroy(void* raw_ctx) noexcept {
    auto* ctx = static_cast<MmcContext*>(raw_ctx);
    if (ctx == nullptr) return;
    sync_buffer(ctx);
    if (ctx->file != nullptr) {
        (void)fclose(ctx->file);
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
const SimRvExternalDevice* simrv_device_plugin_init() noexcept { return &kDevice; }
}
