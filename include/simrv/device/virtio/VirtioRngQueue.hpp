/**
 * @file VirtioRngQueue.hpp
 * @brief Shared VirtIO RNG virtqueue processing helper across MMIO and PCI transports.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device::virtio {

inline constexpr uint32_t kMaxVirtioRngRequestBytes = 64U * 1024U;

template <typename DmaRead, typename DmaWrite, typename TriggerIrq>
void process_rng_queue(QueueState& q, RngBackend& backend, DmaRead&& dma_read, DmaWrite&& dma_write,
                       TriggerIrq&& trigger_irq) {
    if (!q.usable()) return;

    uint16_t avail_idx = 0;
    if (!dma_read(q.driver_addr + 2, &avail_idx, sizeof(avail_idx))) {
        return;
    }

    bool processed_any = false;
    while (q.last_avail_idx != avail_idx) {
        const uint16_t ring_idx = q.last_avail_idx % q.num;
        uint16_t head_desc_idx = 0;
        if (!dma_read(q.driver_addr + 4 + ring_idx * 2, &head_desc_idx, sizeof(head_desc_idx))) {
            break;
        }

        if (head_desc_idx >= q.num) break;
        VirtqDesc desc{};
        if (!dma_read(q.desc_addr + head_desc_idx * sizeof(VirtqDesc), &desc, sizeof(desc))) {
            break;
        }

        uint32_t written = 0;
        if (desc.len > 0) {
            const uint32_t total = std::min(desc.len, kMaxVirtioRngRequestBytes);
            std::array<std::byte, 256> chunk{};
            while (written < total) {
                const uint32_t step =
                    std::min<uint32_t>(total - written, static_cast<uint32_t>(chunk.size()));
                if (desc.addr > std::numeric_limits<uint64_t>::max() - written) {
                    break;
                }
                backend.fill_random(std::span<std::byte>(chunk.data(), step));
                if (!dma_write(desc.addr + written, chunk.data(), step)) {
                    break;
                }
                written += step;
            }
        }

        uint16_t used_idx = 0;
        (void)dma_read(q.device_addr + 2, &used_idx, sizeof(used_idx));
        const VirtqUsedElem elem{.id = head_desc_idx, .len = written};
        (void)dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem, sizeof(elem));
        ++used_idx;
        (void)dma_write(q.device_addr + 2, &used_idx, sizeof(used_idx));

        ++q.last_avail_idx;
        processed_any = true;
    }

    if (processed_any) {
        trigger_irq();
    }
}

}  // namespace simrv::device::virtio
