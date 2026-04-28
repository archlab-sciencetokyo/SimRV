/**
 * @file Disk.cpp
 * @brief SimRV implementation unit.
 */
#include "Disk.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ios>
#include <print>
#include <type_traits>
#include <utility>

#include "Define.hpp"
#include "IoController.hpp"
#include "Machine.hpp"
#include "VirtioUtil.hpp"
#include "XLen.hpp"

using DescriptorSize = std::integral_constant<std::size_t, 16>;
using simrv::virtio_detail::byte_to_word;
using simrv::virtio_detail::load_from_ram;
using simrv::virtio_detail::store_to_ram;
using simrv::virtio_detail::update_descriptor;
using simrv::virtio_detail::word_to_byte;

namespace {
void reset_micro_controller_state(IoController& controller) {
    controller.pc = 0;
    std::ranges::fill(controller.reg, Register{0});
    controller.reg[11] = 0x8000;
}
}  // namespace

static auto load_from_disk(Address addr, int n, Byte* dsk) -> Word {
    if (n != 4) {
        std::println("__ Error: dsk_ld()");
        exit(0);
    }
    Word* dsk_tmp = reinterpret_cast<Word*>(dsk);
    return dsk_tmp[addr / 4];
}

static void store_to_disk(Address addr, Word data, int n, Byte* dsk) {
    if (n != 4) {
        std::println("__ Error: dsk_st()");
        exit(0);
    }
    Word* dsk_tmp = reinterpret_cast<Word*>(dsk);
    dsk_tmp[addr / 4] = data;
}

static void process_disk_queue_requests(Byte* mmem, Byte* mdsk, int q_num, QueueState* qs) {
    Descriptor desc{};
    BlockRequestHeader header{};
    Byte* p = nullptr;

    auto avail_idx = static_cast<uint16_t>(load_from_ram(qs->AvailLow + 2, 2, mmem));
    while (qs->last_avail_idx != avail_idx) { /* header -> sector -> footer */

        // (1) header
        Address const adr = qs->AvailLow + 4 + ((qs->last_avail_idx & (q_num - 1)) * 2);
        uint16_t const desc_idx_header = load_from_ram(adr, 2, mmem);
        Address const desc_adr_header = (desc_idx_header * DescriptorSize::value) + qs->DescLow;

        p = reinterpret_cast<Byte*>(&desc);
        for (std::size_t i = 0; i < DescriptorSize::value; i++) {
            *p = static_cast<Byte>(
                static_cast<uint8_t>(load_from_ram(desc_adr_header + i, 1, mmem)));
            p++;
        }

        p = reinterpret_cast<Byte*>(&header);
        for (int i = 0; std::cmp_less(i, desc.len); i++) {
            *p = static_cast<Byte>(static_cast<uint8_t>(load_from_ram(desc.adr + i, 1, mmem)));
            p++;
        }
        if (desc.len != 16) {
            std::println("__ ERROR: disk_request() desc.len!=16");
            exit(0);
        }

        // (2) sector
        uint16_t const desc_idx_sector = desc.next;
        Address const desc_adr_sector = (desc_idx_sector * DescriptorSize::value) + qs->DescLow;
        p = reinterpret_cast<Byte*>(&desc);
        for (std::size_t i = 0; i < DescriptorSize::value; i++) {
            *p = static_cast<Byte>(
                static_cast<uint8_t>(load_from_ram(desc_adr_sector + i, 1, mmem)));
            p++;
        }

        Word const sector_len = desc.len;
        auto sector_adr = static_cast<Address>(desc.adr);

        // (3) footer
        uint16_t const desc_idx_footer = desc.next;
        Address const desc_adr_footer = (desc_idx_footer * DescriptorSize::value) + qs->DescLow;
        p = reinterpret_cast<Byte*>(&desc);
        for (std::size_t i = 0; i < DescriptorSize::value; i++) {
            *p = static_cast<Byte>(
                static_cast<uint8_t>(load_from_ram(desc_adr_footer + i, 1, mmem)));
            p++;
        }

        auto footer_adr = static_cast<Address>(desc.adr);

        Word request_size = 0;
        switch (header.type) {
            case enum_mask(VirtioBlkType::In): {  /////  disk -> dram
                request_size = sector_len + 1;
                for (int i = 0; std::cmp_less(i, sector_len); i = i + 4) {
                    Word const d = load_from_disk(
                        static_cast<Address>((header.sector_num * simrv::virtio::kDiskSectorSize) +
                                             i),
                        4, mdsk);
                    store_to_ram(sector_adr + i, d, 4, mmem);
                }
                store_to_ram(footer_adr, 0, 1, mmem);  //  VIRTIO_BLK_S_OK
                break;
            }
            case enum_mask(VirtioBlkType::Out): {  ///// dram -> disk
                request_size = 1;
                for (int i = 0; std::cmp_less(i, sector_len); i = i + 4) {
                    Word const d = load_from_ram(sector_adr, 4, mmem);
                    store_to_disk((header.sector_num * simrv::virtio::kDiskSectorSize) + i, d, 4,
                                  mdsk);
                }
                store_to_ram(sector_adr + sector_len - 1, 0, 1, mmem);  //  VIRTIO_BLK_S_OK
                break;
            }
            default: {
                std::println("__ ERROR: disk unknown header {:x}", header.type);
                exit(0);
            }
        }

        update_descriptor(desc_idx_header, request_size, q_num, qs, mmem);
        qs->last_avail_idx++;
    }
}

Disk::Disk()
    : mmem(nullptr),
    sector_storage_(simrv::virtio::kDiskSize),
    sector(sector_storage_.data()),  // sector is not a good name, rename this!
      Queue(nullptr),
      DeviceFeaturesSel(0),
      DriverFeatures(0),
      DriverFeaturesSel(0),
      InterruptStatus(0),
      Status(0),
      QueueSel(0),
      QueueNum(0) {}

auto Disk::read(Machine& machine, Address p_addr, Word& rdata) -> bool {
    rdata = mmio_read(offset(p_addr));
    if (machine.s_debugmode) {
        std::println("__ {:10} VIO mem_read  {:08x} {:08x}", machine.cpu.mtime, p_addr, rdata);
    }
    if (machine.s_dlog_mode && machine.s_fp_dlog.is_open()) {
        machine.s_fp_dlog << std::hex << rdata << '\n';
        machine.s_fp_dlog.flush();
    }
    return true;
}

auto Disk::write(Machine& machine, Address p_addr, Word wdata) -> bool {
    mmio_write(machine, offset(p_addr), wdata);
    if (machine.s_debugmode) {
        std::println("__ {:10} VIO mem_write {:08x} {:08x}", machine.cpu.mtime, p_addr, wdata);
    }
    return true;
}

auto Disk::mmio_read(Address offset) const -> Word {
    Word rdata = 0;
    switch (offset) {
        case 0x000:
            rdata = simrv::virtio::kDiskMagicValue;
            break;
        case 0x004:
            rdata = simrv::virtio::kDiskVersion;
            break;
        case 0x008:
            rdata = simrv::virtio::kDiskDeviceId;
            break;
        case 0x00c:
            rdata = simrv::virtio::kDiskVendorId;
            break;
        case 0x010:
            rdata = simrv::virtio::kDiskDeviceFeatures;
            break;
        case 0x034:
            rdata = simrv::virtio::kDiskQueueNumMax;
            break;
        case 0x0fc:
            rdata = simrv::virtio::kDiskConfigGeneration;
            break;
        case 0x044:
            rdata = Queue[QueueSel].Ready;
            break;
        case 0x060:
            rdata = InterruptStatus;
            break;
        case 0x070:
            rdata = Status;
            break;
        case 0x100:
        case 0x104:
            rdata = 0;
            break;
        default:
            break;  //{ printf("__ Error: disk_read() default %x.\n", offset); exit(0); }
    }
    // printf("%8ld:CALL Disk READ mem[%x]->%x\n", cpu.mtime, offset, rdata);
    return rdata;
}

void Disk::mmio_write(Machine& machine, Address offset, Word wdata) {
    switch (offset) {
        case 0x038:
            QueueNum = wdata;
            break;
        case 0x044:
            Queue[QueueSel].Ready = wdata;
            break;
        case 0x070:
            Status = wdata;
            break;
        case 0x080:
            Queue[QueueSel].DescLow = wdata;
            break;
        case 0x090:
            Queue[QueueSel].AvailLow = wdata;
            break;
        case 0x0a0:
            Queue[QueueSel].UsedLow = wdata;
            break;
        case 0x050: {
            Queue[QueueSel].Notify = wdata;
            //        printf("__ disk_request %10ld: %d\n", cpu.mtime, wdata);
            if (machine.s_use_uc && machine.micro_controller != nullptr) {
                reset_micro_controller_state(*machine.micro_controller);
                machine.micro_controller->Qnum = QueueNum;
                machine.micro_controller->Mode = 2;
                machine.micro_controller->Qsel = wdata;
                while (machine.micro_controller->exec()) {
                }
            } else {
                process_disk_queue_requests(mmem, sector, static_cast<int>(QueueNum),
                                            &Queue[wdata]); /* request */
            }
            InterruptStatus |= 1;
            machine.cpu.plic_set_irq(simrv::virtio::kDiskIrq, 1);
            break;
        }
        case 0x064: {
            InterruptStatus &= ~wdata;
            if (InterruptStatus == 0) {
                machine.cpu.plic_set_irq(simrv::virtio::kDiskIrq, 0);
            }
            break;
        }
        default:
            break;
    }
}
