/**
 * @file VirtioMmioGpu.cpp
 * @brief Implementation of VirtIO-MMIO v2 GPU Display Endpoint.
 */
#include "simrv/device/mmio/VirtioMmioGpu.hpp"

#include <vector>

namespace simrv::device {

VirtioMmioGpu::VirtioMmioGpu(Address base_address, uint32_t irq_num, core::Machine* machine)
    : VirtioMmioDevice("virtio-gpu-mmio", base_address, 0x1000, virtio::kDevIdGpu, irq_num,
                       machine) {
    add_queue(64);  // controlq
    add_queue(64);  // cursorq
}

auto VirtioMmioGpu::read_device_config(Address offset, std::size_t size) -> uint64_t {
    (void)size;
    if (offset == 0) return 0;  // events_read
    if (offset == 4) return 0;  // events_clear
    if (offset == 8) return 1;  // num_scanouts = 1
    return 0;
}

void VirtioMmioGpu::on_queue_notify(uint32_t q_idx) {
    if (q_idx >= queues_.size()) return;
    auto& q = queues_[q_idx];
    if (q.ready == 0 || q.driver_addr == 0 || q.device_addr == 0 || q.desc_addr == 0) return;

    uint16_t avail_idx = 0;
    if (!dma_read_bytes(q.driver_addr + 2, reinterpret_cast<std::byte*>(&avail_idx), 2)) return;

    bool processed_any = false;
    while (q.last_avail_idx != avail_idx) {
        const uint16_t ring_idx = q.last_avail_idx % q.num;
        uint16_t head_desc_idx = 0;
        if (!dma_read_bytes(q.driver_addr + 4 + ring_idx * 2,
                            reinterpret_cast<std::byte*>(&head_desc_idx), 2))
            break;

        virtio::VirtqDesc desc0{};
        if (!dma_read_bytes(q.desc_addr + head_desc_idx * sizeof(virtio::VirtqDesc),
                            reinterpret_cast<std::byte*>(&desc0), sizeof(desc0)))
            break;

        uint32_t written = 0;
        if ((desc0.flags & virtio::kVirtqDescFNext) != 0) {
            virtio::VirtqDesc desc1{};
            if (dma_read_bytes(q.desc_addr + desc0.next * sizeof(virtio::VirtqDesc),
                               reinterpret_cast<std::byte*>(&desc1), sizeof(desc1))) {
                struct GpuResp {
                    uint32_t type;
                    uint32_t flags;
                    uint64_t fence_id;
                } resp{0x1100, 0, 0};
                if (desc1.len >= sizeof(resp)) {
                    dma_write_bytes(desc1.addr, reinterpret_cast<std::byte*>(&resp), sizeof(resp));
                    written = sizeof(resp);
                }
            }
        }

        uint16_t used_idx = 0;
        dma_read_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx), 2);
        virtio::VirtqUsedElem elem{head_desc_idx, written};
        dma_write_bytes(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem),
                        reinterpret_cast<std::byte*>(&elem), sizeof(elem));
        used_idx++;
        dma_write_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx), 2);

        q.last_avail_idx++;
        processed_any = true;
    }

    if (processed_any) {
        trigger_irq();
    }
}

}  // namespace simrv::device
