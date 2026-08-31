/**
 * @file VirtioPciBlock.cpp
 * @brief Implementation of VirtIO-PCI Block Storage Endpoint.
 */
#include "simrv/device/pci/VirtioPciBlock.hpp"

#include <vector>

namespace simrv::device {

VirtioPciBlock::VirtioPciBlock(const std::string& disk_path)
    : VirtioPciDevice(virtio::kDevIdBlock, 0x010000, 1), backend_(disk_path) {}

auto VirtioPciBlock::get_device_features(uint32_t select) -> uint32_t {
    if (select == 0) {
        return (1U << 1);  // SIZE_MAX
    }
    if (select == 1) {
        return (1U << 0);  // VIRTIO_F_VERSION_1 (bit 32)
    }
    return 0;
}

auto VirtioPciBlock::read_device_config(Address offset, uint8_t size) -> uint32_t {
    (void)size;
    const uint64_t capacity = backend_.capacity_sectors();
    if (offset == 0) return static_cast<uint32_t>(capacity & 0xFFFFFFFFULL);
    if (offset == 4) return static_cast<uint32_t>((capacity >> 32) & 0xFFFFFFFFULL);
    return 0;
}

void VirtioPciBlock::on_queue_notify(uint16_t queue_index) {
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

        struct Header {
            uint32_t type;
            uint32_t ioprio;
            uint64_t sector;
        } hdr{};
        if (desc0.len >= sizeof(hdr)) {
            dma_read(desc0.addr, &hdr, sizeof(hdr));
        }

        uint32_t total_written = 0;
        if ((desc0.flags & virtio::kVirtqDescFNext) != 0) {
            virtio::VirtqDesc desc1{};
            if (dma_read(q.desc_addr + desc0.next * sizeof(virtio::VirtqDesc), &desc1,
                         sizeof(desc1))) {
                if (hdr.type == 0) {  // READ
                    io_buffer_.resize(desc1.len);
                    backend_.read_sectors(hdr.sector, std::span<std::byte>(io_buffer_));
                    dma_write(desc1.addr, io_buffer_.data(), desc1.len);
                    total_written = desc1.len;
                } else if (hdr.type == 1) {  // WRITE
                    io_buffer_.resize(desc1.len);
                    dma_read(desc1.addr, io_buffer_.data(), desc1.len);
                    backend_.write_sectors(hdr.sector, std::span<const std::byte>(io_buffer_));
                }

                if ((desc1.flags & virtio::kVirtqDescFNext) != 0) {
                    virtio::VirtqDesc desc2{};
                    if (dma_read(q.desc_addr + desc1.next * sizeof(virtio::VirtqDesc), &desc2,
                                 sizeof(desc2))) {
                        uint8_t status_byte = 0;  // VIRTIO_BLK_S_OK
                        dma_write(desc2.addr, &status_byte, 1);
                    }
                }
            }
        }

        uint16_t used_idx = 0;
        dma_read(q.device_addr + 2, &used_idx, 2);
        virtio::VirtqUsedElem elem{head_desc_idx, total_written};
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
