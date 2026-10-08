/**
 * @file VirtioPciBlock.cpp
 * @brief Implementation of VirtIO-PCI Block Storage Endpoint.
 */
#include "simrv/device/pci/VirtioPciBlock.hpp"

#include <vector>

#include "simrv/core/Machine.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"

namespace simrv::device {

VirtioPciBlock::VirtioPciBlock(const std::string& disk_path)
    : VirtioPciDevice(virtio::kDevIdBlock, 0x010000, 1), backend_(disk_path) {}

auto VirtioPciBlock::get_device_features(uint32_t select) -> uint32_t {
    if (select == 0) {
        return (1U << 9) | (1U << 13) | (1U << 14);  // FLUSH, DISCARD, WRITE_ZEROES
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
    if (offset == 36 || offset == 48) return virtio::BlockBackend::kMaxRangeSectors;
    if (offset == 40 || offset == 52) return 1;  // One range per request
    if (offset == 56) return 1;                  // write_zeroes_may_unmap
    return 0;
}

void VirtioPciBlock::on_queue_notify(uint16_t queue_index) {
    if (queue_index >= queues_.size()) return;
    auto& q = queues_[queue_index];
    if (!q.ready || !q.driver_addr || !q.device_addr || !q.desc_addr) return;

    uint16_t avail_idx = 0;
    if (!dma_read(q.driver_addr + 2, &avail_idx, 2)) return;

    core::Machine* machine = root_complex_ != nullptr ? root_complex_->machine() : nullptr;
    const bool defer_completion = machine != nullptr && machine->runtime_profile.is_cycle_mode() &&
                                  machine->dma_engine().is_enabled();

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
        size_t transfer_bytes = 0;
        uint8_t request_status = 2;  // VIRTIO_BLK_S_UNSUPP
        if ((desc0.flags & virtio::kVirtqDescFNext) != 0) {
            virtio::VirtqDesc desc1{};
            if (dma_read(q.desc_addr + desc0.next * sizeof(virtio::VirtqDesc), &desc1,
                         sizeof(desc1))) {
                uint64_t status_addr = 0;
                uint32_t data_written = 0;
                bool status_descriptor_valid = false;
                if (hdr.type == 4) {  // FLUSH uses desc1 itself for the status byte.
                    status_addr = desc1.addr;
                    status_descriptor_valid =
                        desc1.len >= 1 && (desc1.flags & virtio::kVirtqDescFWrite) != 0;
                    request_status = backend_.flush() ? 0 : 1;
                } else if (hdr.type == 0) {  // READ
                    io_buffer_.resize(desc1.len);
                    if ((desc1.flags & virtio::kVirtqDescFWrite) != 0 &&
                        backend_.read_sectors(hdr.sector, std::span<std::byte>(io_buffer_)) &&
                        dma_write(desc1.addr, io_buffer_.data(), desc1.len)) {
                        request_status = 0;
                        data_written = desc1.len;
                        transfer_bytes = desc1.len;
                    } else {
                        request_status = 1;  // VIRTIO_BLK_S_IOERR
                    }
                } else if (hdr.type == 1) {  // WRITE
                    io_buffer_.resize(desc1.len);
                    if ((desc1.flags & virtio::kVirtqDescFWrite) == 0 &&
                        dma_read(desc1.addr, io_buffer_.data(), desc1.len) &&
                        backend_.write_sectors(hdr.sector,
                                               std::span<const std::byte>(io_buffer_))) {
                        request_status = 0;
                        transfer_bytes = desc1.len;
                    } else {
                        request_status = 1;  // VIRTIO_BLK_S_IOERR
                    }
                } else if (hdr.type == 11 || hdr.type == 13) {  // DISCARD / WRITE_ZEROES
                    struct Range {
                        uint64_t sector;
                        uint32_t num_sectors;
                        uint32_t flags;
                    } range{};
                    if ((desc1.flags & virtio::kVirtqDescFWrite) != 0 ||
                        desc1.len != sizeof(range) ||
                        !dma_read(desc1.addr, &range, sizeof(range))) {
                        request_status = 2;
                    } else {
                        transfer_bytes = sizeof(range);
                        if ((hdr.type == 11 && range.flags != 0) ||
                            (hdr.type == 13 && (range.flags & ~1U) != 0) ||
                            range.num_sectors > virtio::BlockBackend::kMaxRangeSectors) {
                            request_status = 2;
                        } else {
                            const bool success =
                                hdr.type == 11
                                    ? backend_.discard_sectors(range.sector, range.num_sectors)
                                    : backend_.write_zeroes(range.sector, range.num_sectors);
                            request_status = success ? 0 : 1;
                        }
                    }
                }

                if (hdr.type != 4 && (desc1.flags & virtio::kVirtqDescFNext) != 0) {
                    virtio::VirtqDesc desc2{};
                    if (dma_read(q.desc_addr + desc1.next * sizeof(virtio::VirtqDesc), &desc2,
                                 sizeof(desc2))) {
                        status_addr = desc2.addr;
                        status_descriptor_valid =
                            desc2.len >= 1 && (desc2.flags & virtio::kVirtqDescFWrite) != 0;
                    }
                }
                if (status_descriptor_valid && dma_write(status_addr, &request_status, 1)) {
                    total_written = data_written + 1;
                    transfer_bytes += 1;
                }
            }
        }

        if (defer_completion) {
            const auto current_cycle = machine->memory().system_bus().cycle();
            machine->dma_engine().schedule_transfer(
                transfer_bytes, current_cycle,
                [this, queue_index, head_desc_idx, total_written]() {
                    auto& q = queues_[queue_index];
                    uint16_t used_idx = 0;
                    dma_read(q.device_addr + 2, &used_idx, 2);
                    virtio::VirtqUsedElem elem{head_desc_idx, total_written};
                    dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem,
                              sizeof(elem));
                    used_idx++;
                    dma_write(q.device_addr + 2, &used_idx, 2);
                    isr_status_ |= 0x1;
                    trigger_irq();
                },
                "virtio-pci-block");
        } else {
            uint16_t used_idx = 0;
            dma_read(q.device_addr + 2, &used_idx, 2);
            virtio::VirtqUsedElem elem{head_desc_idx, total_written};
            dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem, sizeof(elem));
            used_idx++;
            dma_write(q.device_addr + 2, &used_idx, 2);
            processed_any = true;
        }

        q.last_avail_idx++;
    }

    if (!defer_completion && processed_any) {
        isr_status_ |= 0x1;
        trigger_irq();
    }
}

}  // namespace simrv::device
