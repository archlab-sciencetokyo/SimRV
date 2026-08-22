/**
 * @file VirtioPciGpu.cpp
 * @brief Implementation of VirtIO-PCI GPU Display Endpoint.
 */
#include "simrv/device/pci/VirtioPciGpu.hpp"

namespace simrv::device {

VirtioPciGpu::VirtioPciGpu() : VirtioPciDevice(virtio::kDevIdGpu, 0x030000, 2) {}

auto VirtioPciGpu::get_device_features(uint32_t select) -> uint32_t {
    if (select == 0) {
        return (1U << 1);  // VIRGL support
    }
    if (select == 1) {
        return (1U << 0);  // VIRTIO_F_VERSION_1
    }
    return 0;
}

auto VirtioPciGpu::read_device_config(Address offset, uint8_t size) -> uint32_t {
    (void)size;
    if (offset == 0) return 0;  // events_read
    if (offset == 4) return 0;  // events_clear
    if (offset == 8) return 1;  // num_scanouts = 1
    return 0;
}

void VirtioPciGpu::on_queue_notify(uint16_t queue_index) {
    if (queue_index >= queues_.size()) return;
    auto& q = queues_[queue_index];
    if (!q.ready || !q.driver_addr || !q.device_addr || !q.desc_addr) return;

    uint16_t avail_idx = 0;
    if (!dma_read(q.driver_addr + 2, &avail_idx, 2)) return;

    bool processed_any = false;
    while (q.last_avail_idx != avail_idx) {
        const uint16_t ring_idx = q.last_avail_idx % q.num;
        uint16_t head_desc_idx = 0;
        if (!dma_read(q.driver_addr + 4 + ring_idx * 2, &head_desc_idx, 2)) break;

        virtio::VirtqDesc desc0{};
        if (!dma_read(q.desc_addr + head_desc_idx * sizeof(virtio::VirtqDesc), &desc0,
                      sizeof(desc0)))
            break;

        uint32_t written = 0;
        if ((desc0.flags & virtio::kVirtqDescFNext) != 0) {
            virtio::VirtqDesc desc1{};
            if (dma_read(q.desc_addr + desc0.next * sizeof(virtio::VirtqDesc), &desc1,
                         sizeof(desc1))) {
                struct GpuResp {
                    uint32_t type;
                    uint32_t flags;
                    uint64_t fence_id;
                } resp{0x1100, 0, 0};
                if (desc1.len >= sizeof(resp)) {
                    dma_write(desc1.addr, &resp, sizeof(resp));
                    written = sizeof(resp);
                }
            }
        }

        uint16_t used_idx = 0;
        dma_read(q.device_addr + 2, &used_idx, 2);
        virtio::VirtqUsedElem elem{head_desc_idx, written};
        dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem, sizeof(elem));
        used_idx++;
        dma_write(q.device_addr + 2, &used_idx, 2);

        q.last_avail_idx++;
        processed_any = true;
    }

    if (processed_any) {
        isr_status_ |= 0x1;
        trigger_irq();
    }
}

}  // namespace simrv::device
