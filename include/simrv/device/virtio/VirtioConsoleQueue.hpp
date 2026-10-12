/**
 * @file VirtioConsoleQueue.hpp
 * @brief Shared VirtIO Console virtqueue processing helper across MMIO and PCI transports.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device::virtio {

template <typename DmaRead, typename DmaWrite, typename TriggerIrq>
void process_console_queue(uint32_t q_idx, QueueState& q, ConsoleBackend& backend,
                           DmaRead&& dma_read, DmaWrite&& dma_write, TriggerIrq&& trigger_irq) {
    if (q.ready == 0 || q.driver_addr == 0 || q.device_addr == 0 || q.desc_addr == 0) {
        return;
    }

    uint16_t avail_idx = 0;
    if (!dma_read(q.driver_addr + 2, &avail_idx, sizeof(avail_idx))) {
        return;
    }

    bool processed_any = false;
    if (q_idx == 0) {  // RX Queue
        while (q.last_avail_idx != avail_idx && backend.has_rx()) {
            const uint16_t ring_idx = q.last_avail_idx % q.num;
            uint16_t head_desc_idx = 0;
            if (!dma_read(q.driver_addr + 4 + ring_idx * 2, &head_desc_idx,
                          sizeof(head_desc_idx))) {
                break;
            }

            VirtqDesc desc{};
            if (!dma_read(q.desc_addr + head_desc_idx * sizeof(VirtqDesc), &desc, sizeof(desc))) {
                break;
            }

            uint32_t written = 0;
            while (written < desc.len && backend.has_rx()) {
                uint8_t b = backend.pop_rx();
                (void)dma_write(desc.addr + written, &b, 1);
                ++written;
            }

            uint16_t used_idx = 0;
            (void)dma_read(q.device_addr + 2, &used_idx, sizeof(used_idx));
            const VirtqUsedElem elem{head_desc_idx, written};
            (void)dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem,
                            sizeof(elem));
            ++used_idx;
            (void)dma_write(q.device_addr + 2, &used_idx, sizeof(used_idx));

            ++q.last_avail_idx;
            processed_any = true;
        }
    } else if (q_idx == 1) {  // TX Queue
        while (q.last_avail_idx != avail_idx) {
            const uint16_t ring_idx = q.last_avail_idx % q.num;
            uint16_t head_desc_idx = 0;
            if (!dma_read(q.driver_addr + 4 + ring_idx * 2, &head_desc_idx,
                          sizeof(head_desc_idx))) {
                break;
            }

            VirtqDesc desc{};
            if (!dma_read(q.desc_addr + head_desc_idx * sizeof(VirtqDesc), &desc, sizeof(desc))) {
                break;
            }

            if (desc.len > 0) {
                std::vector<uint8_t> tx_buf(desc.len);
                (void)dma_read(desc.addr, tx_buf.data(), desc.len);
                for (uint8_t ch : tx_buf) {
                    backend.write_tx(ch);
                }
            }

            uint16_t used_idx = 0;
            (void)dma_read(q.device_addr + 2, &used_idx, sizeof(used_idx));
            const VirtqUsedElem elem{head_desc_idx, desc.len};
            (void)dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem,
                            sizeof(elem));
            ++used_idx;
            (void)dma_write(q.device_addr + 2, &used_idx, sizeof(used_idx));

            ++q.last_avail_idx;
            processed_any = true;
        }
    }

    if (processed_any) {
        trigger_irq();
    }
}

}  // namespace simrv::device::virtio
