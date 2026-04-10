/**
 * @file Disk.cpp
 * @brief SimRV implementation unit.
 */
#include "Disk.hpp"

#include <algorithm>

#include "Machine.hpp"

constexpr Word DISK_MAGIC_VALUE = 0x74726976;
constexpr Word DISK_VERSION = 2;
constexpr Word DISK_DEVICE_ID = 2;
constexpr Word DISK_VENDOR_ID = 0xffff;
constexpr Word DISK_DEVICE_FEATURES = 1;
constexpr Word DISK_CONFIG_GENERATION = 0;
constexpr Word DISK_QUEUE_NUM_MAX = 4;

using DescriptorSize = std::integral_constant<std::size_t, 16>;

namespace {
void reset_micro_controller_state(IoController& controller) {
    controller.pc = 0;
    std::fill(std::begin(controller.reg), std::end(controller.reg), Register{0});
    controller.reg[11] = 0x8000;
}
}  // namespace

constexpr Word byte_to_word(Byte b) { return static_cast<Word>(std::to_integer<uint8_t>(b)); }
constexpr Byte word_to_byte(Word w) { return static_cast<Byte>(static_cast<uint8_t>(w & 0xffu)); }

Word load_from_ram(Address addr, int n, Byte* ram) {
    if (n != 1 && n != 2 && n != 4) {
        printf("__ Error: ram_r() not supported n=%d\n", n);
        exit(0);
    }
    Word data = 0;
    for (int i = 0; i < n; i++) {
        data |= byte_to_word(ram[(addr + i) & simrv::memory::kDramMask]) << (8 * i);
    }
    return data;
}

void store_to_ram(Address addr, Word data, int n, Byte* ram) {
    if (n != 1 && n != 2 && n != 4) {
        printf("__ Error: dsk_w() not supported n=%d\n", n);
        exit(0);
    }
    if (n == 1) {
        ram[addr & simrv::memory::kDramMask] = word_to_byte(data);
    } else if (n == 2) {
        ram[addr & simrv::memory::kDramMask] = word_to_byte(data);
        ram[(addr + 1) & simrv::memory::kDramMask] = word_to_byte(data >> 8);
    } else if (n == 4) {
        ram[addr & simrv::memory::kDramMask] = word_to_byte(data);
        ram[(addr + 1) & simrv::memory::kDramMask] = word_to_byte(data >> 8);
        ram[(addr + 2) & simrv::memory::kDramMask] = word_to_byte(data >> 16);
        ram[(addr + 3) & simrv::memory::kDramMask] = word_to_byte(data >> 24);
    }
}

Word load_from_disk(Address addr, int n, Byte* dsk) {
    if (n != 4) {
        printf("__ Error: dsk_ld()\n");
        exit(0);
    }
    Word* dsk_tmp = reinterpret_cast<Word*>(dsk);
    return dsk_tmp[addr / 4];
}

void store_to_disk(Address addr, Word data, int n, Byte* dsk) {
    if (n != 4) {
        printf("__ Error: dsk_st()\n");
        exit(0);
    }
    Word* dsk_tmp = reinterpret_cast<Word*>(dsk);
    dsk_tmp[addr / 4] = data;
}

void update_descriptor(Word desc_idx, Word desc_len, int q_num, QueueState* qs, Byte* mmem) {
    Address addr_used_idx = qs->UsedLow + 2;
    Word index = static_cast<uint16_t>(load_from_ram(addr_used_idx, 2, mmem));

    store_to_ram(addr_used_idx, index + 1, 2, mmem);

    Address addr_used_entry = qs->UsedLow + 4 + (index & (q_num - 1)) * 8;
    store_to_ram(addr_used_entry, desc_idx, 4, mmem);
    store_to_ram(addr_used_entry + 4, desc_len, 4, mmem);
}

void process_disk_queue_requests(Byte* mmem, Byte* mdsk, int q_num, QueueState* qs) {
    Descriptor desc;
    BlockRequestHeader header;
    Byte* p;

    uint16_t avail_idx = static_cast<uint16_t>(load_from_ram(qs->AvailLow + 2, 2, mmem));
    while (qs->last_avail_idx != avail_idx) { /* header -> sector -> footer */

        // (1) header
        Address adr = qs->AvailLow + 4 + (qs->last_avail_idx & (q_num - 1)) * 2;
        uint16_t desc_idx_header = load_from_ram(adr, 2, mmem);
        Address desc_adr_header = desc_idx_header * DescriptorSize::value + qs->DescLow;

        p = reinterpret_cast<Byte*>(&desc);
        for (std::size_t i = 0; i < DescriptorSize::value; i++) {
            *p = static_cast<Byte>(
                static_cast<uint8_t>(load_from_ram(desc_adr_header + i, 1, mmem)));
            p++;
        }

        p = reinterpret_cast<Byte*>(&header);
        for (int i = 0; i < static_cast<int>(desc.len); i++) {
            *p = static_cast<Byte>(static_cast<uint8_t>(load_from_ram(desc.adr + i, 1, mmem)));
            p++;
        }
        if (desc.len != 16) {
            printf("__ ERROR: disk_request() desc.len!=16\n");
            exit(0);
        }

        // (2) sector
        uint16_t desc_idx_sector = desc.next;
        Address desc_adr_sector = desc_idx_sector * DescriptorSize::value + qs->DescLow;
        p = reinterpret_cast<Byte*>(&desc);
        for (std::size_t i = 0; i < DescriptorSize::value; i++) {
            *p = static_cast<Byte>(
                static_cast<uint8_t>(load_from_ram(desc_adr_sector + i, 1, mmem)));
            p++;
        }

        Word sector_len = desc.len;
        Address sector_adr = static_cast<Address>(desc.adr);

        // (3) footer
        uint16_t desc_idx_footer = desc.next;
        Address desc_adr_footer = desc_idx_footer * DescriptorSize::value + qs->DescLow;
        p = reinterpret_cast<Byte*>(&desc);
        for (std::size_t i = 0; i < DescriptorSize::value; i++) {
            *p = static_cast<Byte>(
                static_cast<uint8_t>(load_from_ram(desc_adr_footer + i, 1, mmem)));
            p++;
        }

        Address footer_adr = static_cast<Address>(desc.adr);

        Word request_size = 0;
        switch (header.type) {
            case enum_mask(VirtioBlkType::In): {  /////  disk -> dram
                request_size = sector_len + 1;
                for (int i = 0; i < static_cast<int>(sector_len); i = i + 4) {
                    Word d =
                        load_from_disk(static_cast<Address>(
                                           header.sector_num * simrv::virtio::kDiskSectorSize + i),
                                       4, mdsk);
                    store_to_ram(sector_adr + i, d, 4, mmem);
                }
                store_to_ram(footer_adr, 0, 1, mmem);  //  VIRTIO_BLK_S_OK
                break;
            }
            case enum_mask(VirtioBlkType::Out): {  ///// dram -> disk
                request_size = 1;
                for (int i = 0; i < static_cast<int>(sector_len); i = i + 4) {
                    Word d = load_from_ram(sector_adr, 4, mmem);
                    store_to_disk(header.sector_num * simrv::virtio::kDiskSectorSize + i, d, 4,
                                  mdsk);
                }
                store_to_ram(sector_adr + sector_len - 1, 0, 1, mmem);  //  VIRTIO_BLK_S_OK
                break;
            }
            default: {
                printf("__ ERROR: disk unknown header %x\n", header.type);
                exit(0);
            }
        }

        update_descriptor(desc_idx_header, request_size, q_num, qs, mmem);
        qs->last_avail_idx++;
    }
}

Disk::Disk()
    : mmem(nullptr),
      sector_owner_(std::make_unique<Byte[]>(simrv::virtio::kDiskSize)),
      sector(sector_owner_.get()),  // sector is not a good name, rename this!
      Queue(nullptr),
      DeviceFeaturesSel(0),
      DriverFeatures(0),
      DriverFeaturesSel(0),
      InterruptStatus(0),
      Status(0),
      QueueSel(0),
      QueueNum(0) {}

bool Disk::read(Machine& machine, Address p_addr, Word& rdata) {
    rdata = mmio_read(offset(p_addr));
    if (machine.s_debugmode) {
        printf("__ %10ld VIO mem_read  %08x %08x\n", machine.cpu.mtime, p_addr, rdata);
    }
    if (machine.s_dlog_mode && machine.s_fp_dlog.is_open()) {
        machine.s_fp_dlog << std::hex << rdata << '\n';
        machine.s_fp_dlog.flush();
    }
    return true;
}

bool Disk::write(Machine& machine, Address p_addr, Word wdata) {
    mmio_write(machine, offset(p_addr), wdata);
    if (machine.s_debugmode) {
        printf("__ %10ld VIO mem_write %08x %08x\n", machine.cpu.mtime, p_addr, wdata);
    }
    return true;
}

Word Disk::mmio_read(Address offset) {
    Word rdata = 0;
    switch (offset) {
        case 0x000:
            rdata = DISK_MAGIC_VALUE;
            break;
        case 0x004:
            rdata = DISK_VERSION;
            break;
        case 0x008:
            rdata = DISK_DEVICE_ID;
            break;
        case 0x00c:
            rdata = DISK_VENDOR_ID;
            break;
        case 0x010:
            rdata = DISK_DEVICE_FEATURES;
            break;
        case 0x034:
            rdata = DISK_QUEUE_NUM_MAX;
            break;
        case 0x0fc:
            rdata = DISK_CONFIG_GENERATION;
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
            rdata = 0;
            break;
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
                process_disk_queue_requests(mmem, sector, QueueNum, &Queue[wdata]); /* request */
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
    }
}
