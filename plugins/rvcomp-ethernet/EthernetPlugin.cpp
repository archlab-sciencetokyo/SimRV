/**
 * @file EthernetPlugin.cpp
 * @brief External device simulation plugin for rvcomp-ethernet (isct,rvcomp-ethernet).
 */
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "simrv/device/ExternalDevice.h"

namespace {

struct EthernetContext {
    const SimRvHostServices* services{nullptr};
    std::array<uint32_t, 8> csr{};
    std::vector<uint8_t> rx_buffer;
    std::vector<uint8_t> tx_buffer;
    uint32_t rx_size{16384};
    uint32_t tx_size{16384};
};

int ethernet_init(void** out_ctx, const char* /*args*/) {
    auto* ctx = new EthernetContext();
    ctx->rx_buffer.assign(ctx->rx_size, 0);
    ctx->tx_buffer.assign(ctx->tx_size, 0);
    ctx->csr[1] = static_cast<uint32_t>(ctx->rx_buffer.size() - 1);
    ctx->csr[3] = static_cast<uint32_t>(ctx->tx_buffer.size() - 1);
    *out_ctx = ctx;
    return 0;
}

void ethernet_reset(void* raw_ctx) {
    auto* ctx = static_cast<EthernetContext*>(raw_ctx);
    if (ctx == nullptr) return;
    ctx->csr.fill(0);
    ctx->csr[1] = static_cast<uint32_t>(ctx->rx_buffer.size() - 1);
    ctx->csr[3] = static_cast<uint32_t>(ctx->tx_buffer.size() - 1);
    ctx->rx_buffer.assign(ctx->rx_size, 0);
    ctx->tx_buffer.assign(ctx->tx_size, 0);
    if (ctx->services != nullptr && ctx->services->set_irq != nullptr) {
        ctx->services->set_irq(ctx->services->host_ctx, 2, 0);
    }
}

int ethernet_read(void* raw_ctx, uint64_t offset, uint32_t size, uint64_t* out_val) {
    auto* ctx = static_cast<EthernetContext*>(raw_ctx);
    if (ctx == nullptr || out_val == nullptr) return -1;
    *out_val = 0;

    // Window 0: CSRs (0x0 .. 0x4000)
    if (offset < 0x04000000ULL) {
        const size_t idx = static_cast<size_t>(offset / 4);
        if (idx < ctx->csr.size()) {
            if (idx == 4) {
                const auto start = ctx->csr[5] & (ctx->tx_size - 1);
                const auto end = ctx->csr[6] & (ctx->tx_size - 1);
                *out_val = start != end ? 1U : 0U;
            } else {
                *out_val = ctx->csr[idx];
            }
        }
        return 0;
    }

    // Window 1: RX Buffer (0x04000000 .. 0x08000000)
    if (offset >= 0x04000000ULL && offset < 0x08000000ULL) {
        const uint64_t buf_off = offset - 0x04000000ULL;
        if (buf_off + size <= ctx->rx_buffer.size()) {
            uint64_t val = 0;
            std::memcpy(&val, ctx->rx_buffer.data() + buf_off, size);
            *out_val = val;
        }
        return 0;
    }

    // Window 2: TX Buffer (0x08000000 .. 0x0c000000)
    if (offset >= 0x08000000ULL && offset < 0x0c000000ULL) {
        const uint64_t buf_off = offset - 0x08000000ULL;
        if (buf_off + size <= ctx->tx_buffer.size()) {
            uint64_t val = 0;
            std::memcpy(&val, ctx->tx_buffer.data() + buf_off, size);
            *out_val = val;
        }
        return 0;
    }

    return 0;
}

int ethernet_write(void* raw_ctx, uint64_t offset, uint32_t size, uint64_t val) {
    auto* ctx = static_cast<EthernetContext*>(raw_ctx);
    if (ctx == nullptr) return -1;

    // Window 0: CSRs
    if (offset < 0x04000000ULL) {
        const size_t idx = static_cast<size_t>(offset / 4);
        switch (idx) {
            case 0:
                ctx->csr[0] = static_cast<uint32_t>(val) & (ctx->rx_size - 1);
                break;
            case 2:
                ctx->csr[2] = static_cast<uint32_t>(val);
                break;
            case 6:
                ctx->csr[6] = static_cast<uint32_t>(val) & (ctx->tx_size - 1) & ~3U;
                ctx->csr[5] = ctx->csr[6];  // Immediate loopback / drain
                break;
            default:
                break;
        }
        return 0;
    }

    // Window 1: RX Buffer
    if (offset >= 0x04000000ULL && offset < 0x08000000ULL) {
        const uint64_t buf_off = offset - 0x04000000ULL;
        if (buf_off + size <= ctx->rx_buffer.size()) {
            std::memcpy(ctx->rx_buffer.data() + buf_off, &val, size);
        }
        return 0;
    }

    // Window 2: TX Buffer
    if (offset >= 0x08000000ULL && offset < 0x0c000000ULL) {
        const uint64_t buf_off = offset - 0x08000000ULL;
        if (buf_off + size <= ctx->tx_buffer.size()) {
            std::memcpy(ctx->tx_buffer.data() + buf_off, &val, size);
        }
        return 0;
    }

    return 0;
}

void ethernet_destroy(void* raw_ctx) {
    auto* ctx = static_cast<EthernetContext*>(raw_ctx);
    if (ctx != nullptr) {
        if (ctx->services != nullptr && ctx->services->set_irq != nullptr) {
            ctx->services->set_irq(ctx->services->host_ctx, 2, 0);
        }
        delete ctx;
    }
}

void ethernet_set_services(void* raw_ctx, const SimRvHostServices* services) {
    auto* ctx = static_cast<EthernetContext*>(raw_ctx);
    if (ctx != nullptr) {
        ctx->services = services;
    }
}

const SimRvExternalDevice kDevice = {
    .api_version = SIMRV_EXTERNAL_DEVICE_API_VERSION,
    .name = "rvcomp-ethernet",
    .compatible = "isct,rvcomp-ethernet",
    .default_base = 0x14000000ULL,
    .default_size = 0x0c000000ULL,
    .init = ethernet_init,
    .reset = ethernet_reset,
    .read = ethernet_read,
    .write = ethernet_write,
    .destroy = ethernet_destroy,
    .set_host_services = ethernet_set_services,
};

}  // namespace

extern "C" {
const SimRvExternalDevice* simrv_device_plugin_init() { return &kDevice; }
}
