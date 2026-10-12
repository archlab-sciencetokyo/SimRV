/**
 * @file VirtioRngQueue.hpp
 * @brief Shared VirtIO RNG virtqueue processing helper across MMIO and PCI transports.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device::virtio {

template <typename DmaRead, typename DmaWrite, typename TriggerIrq>
void process_rng_queue(QueueState& q, RngBackend& backend, DmaRead&& dma_read, DmaWrite&& dma_write,
                       TriggerIrq&& trigger_irq) {
    if (q.ready == 0 || q.driver_addr == 0 || q.device_addr == 0 || q.desc_addr == 0) {
        return;
    }

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

        VirtqDesc desc{};
        if (!dma_read(q.desc_addr + head_desc_idx * sizeof(VirtqDesc), &desc, sizeof(desc))) {
            break;
        }

        if (desc.len > 0) {
            std::vector<std::byte> rand_buf(desc.len);
            backend.fill_random(rand_buf.data(), desc.len);
            (void)dma_write(desc.addr, rand_buf.data(), desc.len);
        }

        uint16_t used_idx = 0;
        (void)dma_read(q.device_addr + 2, &used_idx, sizeof(used_idx));
        const VirtqUsedElem elem{.id = head_desc_idx, .len = desc.len};
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
