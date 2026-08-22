/**
 * @file VirtioMmioRng.cpp
 * @brief Implementation of VirtIO-MMIO v2 Random Number Generator Endpoint.
 */
#include "simrv/device/mmio/VirtioMmioRng.hpp"

#include <vector>

namespace simrv::device {

VirtioMmioRng::VirtioMmioRng(Address base_address, uint32_t irq_num, core::Machine* machine)
    : VirtioMmioDevice("virtio-rng-mmio", base_address, 0x1000, virtio::kDevIdRng, irq_num,
                       machine) {
    add_queue(64);
}

void VirtioMmioRng::on_queue_notify(uint32_t q_idx) {
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

        virtio::VirtqDesc desc{};
        if (!dma_read_bytes(q.desc_addr + head_desc_idx * sizeof(virtio::VirtqDesc),
                            reinterpret_cast<std::byte*>(&desc), sizeof(desc)))
            break;

        std::vector<std::byte> rand_buf(desc.len);
        backend_.fill_random(rand_buf.data(), desc.len);
        dma_write_bytes(desc.addr, rand_buf.data(), desc.len);

        uint16_t used_idx = 0;
        dma_read_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx), 2);
        virtio::VirtqUsedElem elem{head_desc_idx, desc.len};
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
