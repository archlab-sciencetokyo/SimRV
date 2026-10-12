/** @file VirtioInputQueue.hpp */
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>

#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device::virtio {

template <typename DmaRead, typename DmaWrite, typename TriggerIrq>
void process_input_event_queue(QueueState& queue, InputBackend& backend, DmaRead&& dma_read,
                               DmaWrite&& dma_write, TriggerIrq&& trigger_irq) {
    if (queue.ready == 0 || queue.driver_addr == 0 || queue.device_addr == 0 ||
        queue.desc_addr == 0) {
        return;
    }

    std::scoped_lock lock(backend.event_mutex);
    if (backend.event_queue.empty()) return;

    uint16_t avail_idx = 0;
    if (!dma_read(queue.driver_addr + 2, &avail_idx, sizeof(avail_idx))) return;

    bool processed_any = false;
    while (queue.last_avail_idx != avail_idx && !backend.event_queue.empty()) {
        const uint16_t ring_idx = queue.last_avail_idx % queue.num;
        uint16_t head_desc_idx = 0;
        if (!dma_read(queue.driver_addr + 4 + ring_idx * 2, &head_desc_idx,
                      sizeof(head_desc_idx))) {
            break;
        }

        VirtqDesc desc{};
        if (!dma_read(queue.desc_addr + head_desc_idx * sizeof(desc), &desc, sizeof(desc))) break;
        if (desc.len < sizeof(VirtioInputEvent)) break;

        const auto event = backend.event_queue.front();
        backend.event_queue.pop_front();
        (void)dma_write(desc.addr, &event, sizeof(event));

        uint16_t used_idx = 0;
        (void)dma_read(queue.device_addr + 2, &used_idx, sizeof(used_idx));
        const VirtqUsedElem elem{head_desc_idx, sizeof(event)};
        (void)dma_write(queue.device_addr + 4 + (used_idx % queue.num) * sizeof(elem), &elem,
                        sizeof(elem));
        ++used_idx;
        (void)dma_write(queue.device_addr + 2, &used_idx, sizeof(used_idx));

        ++queue.last_avail_idx;
        processed_any = true;
    }

    if (processed_any) trigger_irq();
}

}  // namespace simrv::device::virtio
