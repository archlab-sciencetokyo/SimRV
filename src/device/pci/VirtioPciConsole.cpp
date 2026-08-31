/**
 * @file VirtioPciConsole.cpp
 * @brief Implementation of VirtIO-PCI Console Endpoint.
 */
#include "simrv/device/pci/VirtioPciConsole.hpp"

#include <vector>

namespace simrv::device {

VirtioPciConsole::VirtioPciConsole() : VirtioPciDevice(virtio::kDevIdConsole, 0x070000, 2) {}

auto VirtioPciConsole::get_device_features(uint32_t select) -> uint32_t {
    if (select == 1) {
        return (1U << 0);  // VIRTIO_F_VERSION_1
    }
    return 0;
}

auto VirtioPciConsole::read_device_config(Address offset, uint8_t size) -> uint32_t {
    (void)size;
    if (offset == 0) {
        return 80 | (24 << 16);  // cols=80, rows=24
    }
    return 0;
}

void VirtioPciConsole::send_input(uint8_t ch) {
    backend_.push_rx(ch);
    on_queue_notify(0);  // Trigger receive queue processing
}

void VirtioPciConsole::on_queue_notify(uint16_t queue_index) {
    if (queue_index >= queues_.size()) return;
    auto& q = queues_[queue_index];
    if (!q.ready || !q.driver_addr || !q.device_addr || !q.desc_addr) return;

    uint16_t avail_idx = 0;
    if (!dma_read(q.driver_addr + 2, &avail_idx, 2)) return;

    bool processed_any = false;
    if (queue_index == 0) {  // RX Queue
        while (q.last_avail_idx != avail_idx && backend_.has_rx()) {
            const uint16_t ring_idx = q.last_avail_idx % q.num;
            uint16_t head_desc_idx = 0;
            if (!dma_read(q.driver_addr + 4 + ring_idx * 2, &head_desc_idx, 2)) break;

            virtio::VirtqDesc desc{};
            if (!dma_read(q.desc_addr + head_desc_idx * sizeof(virtio::VirtqDesc), &desc,
                          sizeof(desc)))
                break;

            uint32_t written = 0;
            while (written < desc.len && backend_.has_rx()) {
                uint8_t b = backend_.pop_rx();
                dma_write(desc.addr + written, &b, 1);
                written++;
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
    } else if (queue_index == 1) {  // TX Queue
        while (q.last_avail_idx != avail_idx) {
            const uint16_t ring_idx = q.last_avail_idx % q.num;
            uint16_t head_desc_idx = 0;
            if (!dma_read(q.driver_addr + 4 + ring_idx * 2, &head_desc_idx, 2)) break;

            virtio::VirtqDesc desc{};
            if (!dma_read(q.desc_addr + head_desc_idx * sizeof(virtio::VirtqDesc), &desc,
                          sizeof(desc)))
                break;

            std::vector<uint8_t> tx_buf(desc.len);
            dma_read(desc.addr, tx_buf.data(), desc.len);
            for (uint8_t ch : tx_buf) {
                backend_.write_tx(ch);
            }

            uint16_t used_idx = 0;
            dma_read(q.device_addr + 2, &used_idx, 2);
            virtio::VirtqUsedElem elem{head_desc_idx, desc.len};
            dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem, sizeof(elem));
            used_idx++;
            dma_write(q.device_addr + 2, &used_idx, 2);

            q.last_avail_idx++;
            processed_any = true;
        }
    }

    if (processed_any) {
        isr_status_ |= 0x1;
        trigger_irq();
    }
}

}  // namespace simrv::device
