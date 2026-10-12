/**
 * @file VirtioBlockQueue.hpp
 * @brief Shared VirtIO Block virtqueue processing helper across MMIO and PCI transports.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device::virtio {

template <typename DmaRead, typename DmaWrite, typename ScheduleDeferred, typename NotifyComplete>
void process_block_queue(QueueState& q, BlockBackend& backend, std::vector<std::byte>& io_buffer,
                         bool defer_completion, DmaRead&& dma_read, DmaWrite&& dma_write,
                         ScheduleDeferred&& schedule_deferred, NotifyComplete&& notify_complete) {
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
        if ((desc0.flags & kVirtqDescFNext) != 0) {
            VirtqDesc desc1{};
            if (dma_read(q.desc_addr + desc0.next * sizeof(VirtqDesc), &desc1, sizeof(desc1))) {
                uint64_t status_addr = 0;
                uint32_t data_written = 0;
                bool status_descriptor_valid = false;
                if (hdr.type == 4) {  // FLUSH uses desc1 itself for the status byte.
                    status_addr = desc1.addr;
                    status_descriptor_valid =
                        desc1.len >= 1 && (desc1.flags & kVirtqDescFWrite) != 0;
                    request_status = backend.flush() ? 0 : 1;
                } else if (hdr.type == 0) {  // READ
                    io_buffer.resize(desc1.len);
                    if ((desc1.flags & kVirtqDescFWrite) != 0 &&
                        backend.read_sectors(hdr.sector, std::span<std::byte>(io_buffer)) &&
                        dma_write(desc1.addr, io_buffer.data(), desc1.len)) {
                        request_status = 0;
                        data_written = desc1.len;
                        transfer_bytes = desc1.len;
                    } else {
                        request_status = 1;  // VIRTIO_BLK_S_IOERR
                    }
                } else if (hdr.type == 1) {  // WRITE
                    io_buffer.resize(desc1.len);
                    if ((desc1.flags & kVirtqDescFWrite) == 0 &&
                        dma_read(desc1.addr, io_buffer.data(), desc1.len) &&
                        backend.write_sectors(hdr.sector, std::span<const std::byte>(io_buffer))) {
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
                    if ((desc1.flags & kVirtqDescFWrite) != 0 || desc1.len != sizeof(range) ||
                        !dma_read(desc1.addr, &range, sizeof(range))) {
                        request_status = 2;
                    } else {
                        transfer_bytes = sizeof(range);
                        if ((hdr.type == 11 && range.flags != 0) ||
                            (hdr.type == 13 && (range.flags & ~1U) != 0) ||
                            range.num_sectors > BlockBackend::kMaxRangeSectors) {
                            request_status = 2;
                        } else {
                            const bool success =
                                hdr.type == 11
                                    ? backend.discard_sectors(range.sector, range.num_sectors)
                                    : backend.write_zeroes(range.sector, range.num_sectors);
                            request_status = success ? 0 : 1;
                        }
                    }
                }

                if (hdr.type != 4 && (desc1.flags & kVirtqDescFNext) != 0) {
                    VirtqDesc desc2{};
                    if (dma_read(q.desc_addr + desc1.next * sizeof(VirtqDesc), &desc2,
                                 sizeof(desc2))) {
                        status_addr = desc2.addr;
                        status_descriptor_valid =
                            desc2.len >= 1 && (desc2.flags & kVirtqDescFWrite) != 0;
                    }
                }
                if (status_descriptor_valid && dma_write(status_addr, &request_status, 1)) {
                    total_written = data_written + 1;
                    transfer_bytes += 1;
                }
            }
        }

        if (defer_completion) {
            schedule_deferred(transfer_bytes, head_desc_idx, total_written);
        } else {
            uint16_t used_idx = 0;
            dma_read(q.device_addr + 2, &used_idx, sizeof(used_idx));
            const VirtqUsedElem elem{head_desc_idx, total_written};
            dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem, sizeof(elem));
            ++used_idx;
            dma_write(q.device_addr + 2, &used_idx, sizeof(used_idx));
            processed_any = true;
        }

        ++q.last_avail_idx;
    }

    if (!defer_completion && processed_any) {
        notify_complete();
    }
}

}  // namespace simrv::device::virtio
