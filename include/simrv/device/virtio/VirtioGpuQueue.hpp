/**
 * @file VirtioGpuQueue.hpp
 * @brief Shared VirtIO GPU virtqueue processing helper across MMIO and PCI transports.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device::virtio {

#pragma pack(push, 1)
struct VirtioGpuResp {
    uint32_t type{0x1100};  // VIRTIO_GPU_RESP_OK_NODATA
    uint32_t flags{0};
    uint64_t fence_id{0};
};
#pragma pack(pop)

template <typename DmaRead, typename DmaWrite, typename NotifyComplete>
void process_gpu_queue(QueueState& q, DmaRead&& dma_read, DmaWrite&& dma_write,
                       NotifyComplete&& notify_complete) {
    if (q.ready == 0 || q.driver_addr == 0 || q.device_addr == 0 || q.desc_addr == 0) return;

    uint16_t avail_idx = 0;
    if (!dma_read(q.driver_addr + 2, &avail_idx, sizeof(avail_idx))) return;

    bool processed_any = false;
    while (q.last_avail_idx != avail_idx) {
        const uint16_t ring_idx = q.last_avail_idx % q.num;
        uint16_t head_desc_idx = 0;
        if (!dma_read(q.driver_addr + 4 + ring_idx * 2, &head_desc_idx, sizeof(head_desc_idx)))
            break;

        VirtqDesc desc0{};
        if (!dma_read(q.desc_addr + head_desc_idx * sizeof(VirtqDesc), &desc0, sizeof(desc0)))
            break;

        uint32_t written = 0;
        if ((desc0.flags & kVirtqDescFNext) != 0) {
            VirtqDesc desc1{};
            if (dma_read(q.desc_addr + desc0.next * sizeof(VirtqDesc), &desc1, sizeof(desc1))) {
                VirtioGpuResp resp{};
                if (desc1.len >= sizeof(resp)) {
                    (void)dma_write(desc1.addr, &resp, sizeof(resp));
                    written = sizeof(resp);
                }
            }
        }

        uint16_t used_idx = 0;
        (void)dma_read(q.device_addr + 2, &used_idx, sizeof(used_idx));
        const VirtqUsedElem elem{head_desc_idx, written};
        (void)dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem, sizeof(elem));
        ++used_idx;
        (void)dma_write(q.device_addr + 2, &used_idx, sizeof(used_idx));

        ++q.last_avail_idx;
        processed_any = true;
    }

    if (processed_any) {
        notify_complete();
    }
}

}  // namespace simrv::device::virtio
