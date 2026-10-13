/**
 * @file VirtioNetQueue.hpp
 * @brief Shared VirtIO Network virtqueue processing helper across MMIO and PCI transports.
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device::virtio {

// VirtIO Net Header (10 or 12 bytes)
#pragma pack(push, 1)
struct VirtioNetHdr {
    uint8_t flags{0};
    uint8_t gso_type{0};
    uint16_t hdr_len{0};
    uint16_t gso_size{0};
    uint16_t csum_start{0};
    uint16_t csum_offset{0};
    uint16_t num_buffers{1};
};
#pragma pack(pop)

template <typename DmaRead, typename DmaWrite, typename ScheduleDeferred, typename NotifyComplete>
void process_net_queue(uint32_t q_idx, QueueState& q, NetBackend& backend, bool defer_completion,
                       DmaRead&& dma_read, DmaWrite&& dma_write,
                       ScheduleDeferred&& schedule_deferred, NotifyComplete&& notify_complete) {
    if (q.ready == 0 || q.driver_addr == 0 || q.device_addr == 0 || q.desc_addr == 0) return;

    uint16_t avail_idx = 0;
    if (!dma_read(q.driver_addr + 2, &avail_idx, sizeof(avail_idx))) return;

    bool processed_any = false;
    if (q_idx == 0) {  // RX Queue
        while (q.last_avail_idx != avail_idx && backend.has_rx_packet()) {
            const uint16_t ring_idx = q.last_avail_idx % q.num;
            uint16_t head_desc_idx = 0;
            if (!dma_read(q.driver_addr + 4 + ring_idx * 2, &head_desc_idx, sizeof(head_desc_idx)))
                break;

            VirtqDesc desc{};
            if (!dma_read(q.desc_addr + head_desc_idx * sizeof(VirtqDesc), &desc, sizeof(desc)))
                break;

            auto pkt = backend.pop_rx_packet();
            VirtioNetHdr hdr{};
            uint32_t written = 0;

            if (desc.len >= sizeof(hdr)) {
                (void)dma_write(desc.addr, &hdr, sizeof(hdr));
                written += sizeof(hdr);
                const uint32_t payload_len = static_cast<uint32_t>(
                    std::min(static_cast<size_t>(desc.len - sizeof(hdr)), pkt.size()));
                if (payload_len > 0) {
                    (void)dma_write(desc.addr + sizeof(hdr), pkt.data(), payload_len);
                    written += payload_len;
                }
            }

            if (defer_completion) {
                schedule_deferred(written, head_desc_idx, written);
            } else {
                uint16_t used_idx = 0;
                (void)dma_read(q.device_addr + 2, &used_idx, sizeof(used_idx));
                const VirtqUsedElem elem{.id = head_desc_idx, .len = written};
                (void)dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem,
                                sizeof(elem));
                ++used_idx;
                (void)dma_write(q.device_addr + 2, &used_idx, sizeof(used_idx));
                processed_any = true;
            }

            ++q.last_avail_idx;
        }
    } else if (q_idx == 1) {  // TX Queue
        while (q.last_avail_idx != avail_idx) {
            const uint16_t ring_idx = q.last_avail_idx % q.num;
            uint16_t head_desc_idx = 0;
            if (!dma_read(q.driver_addr + 4 + ring_idx * 2, &head_desc_idx, sizeof(head_desc_idx)))
                break;

            VirtqDesc desc{};
            if (!dma_read(q.desc_addr + head_desc_idx * sizeof(VirtqDesc), &desc, sizeof(desc)))
                break;

            if (desc.len > sizeof(VirtioNetHdr)) {
                std::vector<uint8_t> pkt(desc.len - sizeof(VirtioNetHdr));
                (void)dma_read(desc.addr + sizeof(VirtioNetHdr), pkt.data(), pkt.size());
                backend.send_tx_packet(pkt.data(), pkt.size());
            }

            if (defer_completion) {
                schedule_deferred(desc.len, head_desc_idx, desc.len);
            } else {
                uint16_t used_idx = 0;
                (void)dma_read(q.device_addr + 2, &used_idx, sizeof(used_idx));
                const VirtqUsedElem elem{.id = head_desc_idx, .len = desc.len};
                (void)dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem,
                                sizeof(elem));
                ++used_idx;
                (void)dma_write(q.device_addr + 2, &used_idx, sizeof(used_idx));
                processed_any = true;
            }

            ++q.last_avail_idx;
        }
    }

    if (!defer_completion && processed_any) {
        notify_complete();
    }
}

}  // namespace simrv::device::virtio
