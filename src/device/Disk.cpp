/**
 * @file Disk.cpp
 * @brief VirtIO block disk device implementation.
 */
#include "simrv/device/Disk.hpp"
#include "simrv/core/Logger.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <print>

#include "simrv/Define.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/Virtio.hpp"
#include "simrv/device/VirtioUtil.hpp"
#include "simrv/xlen/Types.hpp"

using simrv::virtio_detail::load_from_ram;
using simrv::virtio_detail::store_to_ram;
using simrv::virtio_detail::update_descriptor;

namespace simrv::device {

void Disk::process_queue(Word q_idx) {
    if (q_idx >= virtio::kDiskMaxQueueNum) return;
    virtio::QueueState& qs = Queue[q_idx];
    if (qs.Ready == 0) return;
    const auto avail_idx = static_cast<uint16_t>(load_from_ram(qs.AvailLow + 2, 2, mmem));
    while (qs.last_avail_idx != avail_idx) { /* header -> sector -> footer */

        // (1) header
        const auto desc_idx_header =
            simrv::virtio_detail::next_avail_desc_idx(&qs, QueueNum, mmem);
        const auto desc_adr_header = simrv::virtio_detail::get_desc_addr(desc_idx_header, &qs);
        auto desc =
            simrv::virtio_detail::read_struct_from_ram<virtio::Descriptor>(desc_adr_header, mmem);

        const auto header =
            simrv::virtio_detail::read_struct_from_ram<virtio::BlockRequestHeader>(
                static_cast<Address>(desc.adr), mmem, desc.len);

        if (desc.len != 16) {
            simrv::log::error("disk_request() desc.len!=16");
            std::exit(EXIT_FAILURE);
        }

        // (2) sector
        const auto desc_idx_sector = desc.next;
        const auto desc_adr_sector = simrv::virtio_detail::get_desc_addr(desc_idx_sector, &qs);
        desc =
            simrv::virtio_detail::read_struct_from_ram<virtio::Descriptor>(desc_adr_sector, mmem);

        const auto sector_len = desc.len;
        const auto sector_adr = static_cast<Address>(desc.adr);

        // (3) footer
        const auto desc_idx_footer = desc.next;
        const auto desc_adr_footer = simrv::virtio_detail::get_desc_addr(desc_idx_footer, &qs);
        desc =
            simrv::virtio_detail::read_struct_from_ram<virtio::Descriptor>(desc_adr_footer, mmem);

        const auto footer_adr = static_cast<Address>(desc.adr);

        Word request_size = 0;
        switch (header.type) {
            case enum_mask(virtio::VirtioBlkType::In): {  /////  disk -> dram
                request_size = sector_len + 1;
                const auto disk_offset =
                    static_cast<Address>(header.sector_num * simrv::virtio::kDiskSectorSize);
                const auto start_idx = sector_adr & simrv::memory::kDramMask;
                if (start_idx + sector_len <= simrv::memory::kDramSize) {
                    std::memcpy(mmem + start_idx, sector + disk_offset, sector_len);
                } else {
                    const std::size_t first_part = simrv::memory::kDramSize - start_idx;
                    std::memcpy(mmem + start_idx, sector + disk_offset, first_part);
                    std::memcpy(mmem, sector + disk_offset + first_part, sector_len - first_part);
                }
                store_to_ram(footer_adr, 0, 1, mmem);  //  VIRTIO_BLK_S_OK
                break;
            }
            case enum_mask(virtio::VirtioBlkType::Out): {  ///// dram -> disk
                request_size = 1;
                const auto disk_offset =
                    static_cast<Address>(header.sector_num * simrv::virtio::kDiskSectorSize);
                const auto start_idx = sector_adr & simrv::memory::kDramMask;
                if (start_idx + sector_len <= simrv::memory::kDramSize) {
                    std::memcpy(sector + disk_offset, mmem + start_idx, sector_len);
                } else {
                    const std::size_t first_part = simrv::memory::kDramSize - start_idx;
                    std::memcpy(sector + disk_offset, mmem + start_idx, first_part);
                    std::memcpy(sector + disk_offset + first_part, mmem, sector_len - first_part);
                }
                store_to_ram(footer_adr, 0, 1, mmem);  //  VIRTIO_BLK_S_OK
                break;
            }
            default: {
                simrv::log::error("disk unknown header {:x}", header.type);
                std::exit(EXIT_FAILURE);
            }
        }

        update_descriptor(desc_idx_header, request_size, static_cast<int>(QueueNum), &qs, mmem);
        qs.last_avail_idx++;

        trigger_interrupt();
    }
}

Disk::Disk(simrv::core::Machine& machine)
    : VirtioDevice(machine, virtio::kDiskIrq, virtio::kDiskMaxQueueNum) {}

}  // namespace simrv::device
