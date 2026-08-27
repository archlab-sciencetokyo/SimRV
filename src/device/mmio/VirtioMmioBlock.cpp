/**
 * @file VirtioMmioBlock.cpp
 * @brief Implementation of VirtIO-MMIO v2 Block Storage Endpoint.
 */
#include "simrv/device/mmio/VirtioMmioBlock.hpp"

#include <vector>

namespace simrv::device {

VirtioMmioBlock::VirtioMmioBlock(Address base_address, uint32_t irq_num, core::Machine* machine,
                                 const std::string& disk_path)
    : VirtioMmioDevice("virtio-block-mmio", base_address, 0x1000, virtio::kDevIdBlock, irq_num,
                       machine),
      backend_(disk_path) {
    add_queue(64);
}

auto VirtioMmioBlock::read_device_config(Address offset, std::size_t size) -> uint64_t {
    (void)size;
    const uint64_t capacity = backend_.capacity_sectors();
    if (offset == 0) return capacity & 0xFFFFFFFFULL;
    if (offset == 4) return (capacity >> 32) & 0xFFFFFFFFULL;
    return 0;
}

void VirtioMmioBlock::on_queue_notify(uint32_t q_idx) {
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

        struct Header {
            uint32_t type;
            uint32_t ioprio;
            uint64_t sector;
        } hdr{};
        if (desc0.len >= sizeof(hdr)) {
            dma_read_bytes(desc0.addr, reinterpret_cast<std::byte*>(&hdr), sizeof(hdr));
        }

        uint32_t total_written = 0;
        if ((desc0.flags & virtio::kVirtqDescFNext) != 0) {
            virtio::VirtqDesc desc1{};
            if (dma_read_bytes(q.desc_addr + desc0.next * sizeof(virtio::VirtqDesc),
                               reinterpret_cast<std::byte*>(&desc1), sizeof(desc1))) {
                if (hdr.type == 0) {  // READ
                    io_buffer_.resize(desc1.len);
                    backend_.read_sectors(hdr.sector, io_buffer_.data(), desc1.len);
                    dma_write_bytes(desc1.addr, io_buffer_.data(), desc1.len);
                    total_written = desc1.len;
                } else if (hdr.type == 1) {  // WRITE
                    io_buffer_.resize(desc1.len);
                    dma_read_bytes(desc1.addr, io_buffer_.data(), desc1.len);
                    backend_.write_sectors(hdr.sector, io_buffer_.data(), desc1.len);
                }

                if ((desc1.flags & virtio::kVirtqDescFNext) != 0) {
                    virtio::VirtqDesc desc2{};
                    if (dma_read_bytes(q.desc_addr + desc1.next * sizeof(virtio::VirtqDesc),
                                       reinterpret_cast<std::byte*>(&desc2), sizeof(desc2))) {
                        uint8_t status_byte = 0;  // VIRTIO_BLK_S_OK
                        dma_write_bytes(desc2.addr, reinterpret_cast<std::byte*>(&status_byte), 1);
                    }
                }
            }
        }

        uint16_t used_idx = 0;
        dma_read_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx), 2);
        virtio::VirtqUsedElem elem{head_desc_idx, total_written};
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
