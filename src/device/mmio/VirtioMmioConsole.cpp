/**
 * @file VirtioMmioConsole.cpp
 * @brief Implementation of VirtIO-MMIO v2 Console Endpoint.
 */
#include "simrv/device/mmio/VirtioMmioConsole.hpp"

#include <vector>

namespace simrv::device {

VirtioMmioConsole::VirtioMmioConsole(Address base_address, uint32_t irq_num, core::Machine* machine)
    : VirtioMmioDevice("virtio-console-mmio", base_address, 0x1000, virtio::kDevIdConsole, irq_num,
                       machine) {
    add_queue(64);  // rx
    add_queue(64);  // tx
}

auto VirtioMmioConsole::read_device_config(Address offset, std::size_t size) -> uint64_t {
    (void)size;
    if (offset == 0) return 80 | (24 << 16);  // cols=80, rows=24
    return 0;
}

void VirtioMmioConsole::send_input(uint8_t ch) {
    backend_.push_rx(ch);
    on_queue_notify(0);
}

void VirtioMmioConsole::on_queue_notify(uint32_t q_idx) {
    if (q_idx >= queues_.size()) return;
    auto& q = queues_[q_idx];
    if (q.ready == 0 || q.driver_addr == 0 || q.device_addr == 0 || q.desc_addr == 0) return;

    uint16_t avail_idx = 0;
    if (!dma_read_bytes(q.driver_addr + 2, reinterpret_cast<std::byte*>(&avail_idx), 2)) return;

    bool processed_any = false;
    if (q_idx == 0) {  // RX
        while (q.last_avail_idx != avail_idx && backend_.has_rx()) {
            const uint16_t ring_idx = q.last_avail_idx % q.num;
            uint16_t head_desc_idx = 0;
            if (!dma_read_bytes(q.driver_addr + 4 + ring_idx * 2,
                                reinterpret_cast<std::byte*>(&head_desc_idx), 2))
                break;

            virtio::VirtqDesc desc{};
            if (!dma_read_bytes(q.desc_addr + head_desc_idx * sizeof(virtio::VirtqDesc),
                                reinterpret_cast<std::byte*>(&desc), sizeof(desc)))
                break;

            uint32_t written = 0;
            while (written < desc.len && backend_.has_rx()) {
                uint8_t b = backend_.pop_rx();
                dma_write_bytes(desc.addr + written, reinterpret_cast<std::byte*>(&b), 1);
                written++;
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
    } else if (q_idx == 1) {  // TX
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

            std::vector<uint8_t> tx_buf(desc.len);
            dma_read_bytes(desc.addr, reinterpret_cast<std::byte*>(tx_buf.data()), desc.len);
            for (uint8_t ch : tx_buf) {
                backend_.write_tx(ch);
            }

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
    }

    if (processed_any) {
        trigger_irq();
    }
}

}  // namespace simrv::device
