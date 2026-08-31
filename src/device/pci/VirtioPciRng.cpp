/**
 * @file VirtioPciRng.cpp
 * @brief Implementation of VirtIO-PCI Random Number Generator Endpoint.
 */
#include "simrv/device/pci/VirtioPciRng.hpp"

#include <vector>

namespace simrv::device {

VirtioPciRng::VirtioPciRng() : VirtioPciDevice(virtio::kDevIdRng, 0x00FF00, 1) {}

auto VirtioPciRng::get_device_features(uint32_t select) -> uint32_t {
    if (select == 1) {
        return (1U << 0);  // VIRTIO_F_VERSION_1
    }
    return 0;
}

void VirtioPciRng::on_queue_notify(uint16_t queue_index) {
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

        virtio::VirtqDesc desc{};
        if (!dma_read(q.desc_addr + head_desc_idx * sizeof(virtio::VirtqDesc), &desc, sizeof(desc)))
            break;

        std::vector<std::byte> rand_buf(desc.len);
        backend_.fill_random(rand_buf.data(), desc.len);
        dma_write(desc.addr, rand_buf.data(), desc.len);

        uint16_t used_idx = 0;
        dma_read(q.device_addr + 2, &used_idx, 2);
        virtio::VirtqUsedElem elem{head_desc_idx, desc.len};
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
