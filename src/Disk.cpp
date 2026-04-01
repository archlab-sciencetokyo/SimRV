/**
 * @file Disk.cpp
 * @brief SimRV implementation unit.
 */
#include "Disk.hpp"

#include "Machine.hpp"
extern Machine mm; /* class machine                     */
extern Microcn cc; /* I/O controller (micro-controller) */

constexpr Word DISK_MAGIC_VALUE = 0x74726976;
constexpr Word DISK_VERSION = 2;
constexpr Word DISK_DEVIDE_ID = 2;
constexpr Word DISK_VENDOR_ID = 0xffff;
constexpr Word DISK_DEVICE_FEATURES = 1;
constexpr Word DISK_CONFIG_GENERATION = 0;
constexpr Word DISK_QUEUE_NUM_MAX = 4;

constexpr Word byte_to_word(Byte b) { return static_cast<Word>(std::to_integer<uint8_t>(b)); }
constexpr Byte word_to_byte(Word w) { return static_cast<Byte>(static_cast<uint8_t>(w & 0xffu)); }

Word ram_ld(Address addr, int n, Byte* ram) {
    if (n != 1 && n != 2 && n != 4) {
        printf("__ Error: ram_r() not supported n=%d\n", n);
        exit(0);
    }
    Word data = 0;
    for (int i = 0; i < n; i++) {
        data |= byte_to_word(ram[(addr + i) & DRAM_MASK]) << (8 * i);
    }
    return data;
}

void ram_st(Address addr, Word data, int n, Byte* ram) {
    if (n != 1 && n != 2 && n != 4) {
        printf("__ Error: dsk_w() not supported n=%d\n", n);
        exit(0);
    }
    if (n == 1) {
        ram[addr & DRAM_MASK] = word_to_byte(data);
    } else if (n == 2) {
        ram[addr & DRAM_MASK] = word_to_byte(data);
        ram[(addr + 1) & DRAM_MASK] = word_to_byte(data >> 8);
    } else if (n == 4) {
        ram[addr & DRAM_MASK] = word_to_byte(data);
        ram[(addr + 1) & DRAM_MASK] = word_to_byte(data >> 8);
        ram[(addr + 2) & DRAM_MASK] = word_to_byte(data >> 16);
        ram[(addr + 3) & DRAM_MASK] = word_to_byte(data >> 24);
    }
}

Word dsk_ld(Address addr, int n, Byte* dsk) {
    if (n != 4) {
        printf("__ Error: dsk_ld()\n");
        exit(0);
    }
    Word* dsk_tmp = reinterpret_cast<Word*>(dsk);
    return dsk_tmp[addr / 4];
}

void dsk_st(Address addr, Word data, int n, Byte* dsk) {
    if (n != 4) {
        printf("__ Error: dsk_st()\n");
        exit(0);
    }
    Word* dsk_tmp = reinterpret_cast<Word*>(dsk);
    dsk_tmp[addr / 4] = data;
}

void update_descriptor(Word desc_idx, Word desc_len, int q_num, QueueState* qs, Byte* mmem) {
    Address addr_used_idx = qs->UsedLow + 2;
    Word index = static_cast<uint16_t>(ram_ld(addr_used_idx, 2, mmem));

    ram_st(addr_used_idx, index + 1, 2, mmem);

    Address addr_used_entry = qs->UsedLow + 4 + (index & (q_num - 1)) * 8;
    ram_st(addr_used_entry, desc_idx, 4, mmem);
    ram_st(addr_used_entry + 4, desc_len, 4, mmem);
}

constexpr int DESC_SIZE = 16; /* descriptor size 16 byte */
void disk_request(Byte* mmem, Byte* mdsk, int q_num, QueueState* qs) {
    Descriptor desc;
    BlockRequestHeader header;
    Byte* p;

    uint16_t avail_idx = (uint16_t)ram_ld(qs->AvailLow + 2, 2, mmem);
    while (qs->last_avail_idx != avail_idx) { /* header -> sector -> footer */

        // (1) header
        Address adr = qs->AvailLow + 4 + (qs->last_avail_idx & (q_num - 1)) * 2;
        uint16_t desc_idx_header = ram_ld(adr, 2, mmem);
        Address desc_adr_header = desc_idx_header * DESC_SIZE + qs->DescLow;

        p = reinterpret_cast<Byte*>(&desc);
        for (int i = 0; i < DESC_SIZE; i++) {
            *p = static_cast<Byte>(static_cast<uint8_t>(ram_ld(desc_adr_header + i, 1, mmem)));
            p++;
        }

        p = reinterpret_cast<Byte*>(&header);
        for (int i = 0; i < (int)desc.len; i++) {
            *p = static_cast<Byte>(static_cast<uint8_t>(ram_ld(desc.adr + i, 1, mmem)));
            p++;
        }
        if (desc.len != 16) {
            printf("__ ERROR: disk_request() desc.len!=16\n");
            exit(0);
        }

        // (2) sector
        uint16_t desc_idx_sector = desc.next;
        Address desc_adr_sector = desc_idx_sector * DESC_SIZE + qs->DescLow;
        p = reinterpret_cast<Byte*>(&desc);
        for (int i = 0; i < DESC_SIZE; i++) {
            *p = static_cast<Byte>(static_cast<uint8_t>(ram_ld(desc_adr_sector + i, 1, mmem)));
            p++;
        }

        Word sector_len = desc.len;
        Address sector_adr = static_cast<Address>(desc.adr);

        // (3) footer
        uint16_t desc_idx_footer = desc.next;
        Address desc_adr_footer = desc_idx_footer * DESC_SIZE + qs->DescLow;
        p = reinterpret_cast<Byte*>(&desc);
        for (int i = 0; i < DESC_SIZE; i++) {
            *p = static_cast<Byte>(static_cast<uint8_t>(ram_ld(desc_adr_footer + i, 1, mmem)));
            p++;
        }

        Address footer_adr = static_cast<Address>(desc.adr);

        Word request_size = 0;
        switch (header.type) {
            case VIRTIO_BLK_T_IN: {  /////  disk -> dram
                request_size = sector_len + 1;
                for (int i = 0; i < (int)sector_len; i = i + 4) {  //++){
                    Word d =
                        dsk_ld(static_cast<Address>(header.sector_num * SECTOR_SIZE + i), 4, mdsk);
                    ram_st(sector_adr + i, d, 4, mmem);
                }
                ram_st(footer_adr, 0, 1, mmem);  //  VIRTIO_BLK_S_OK
                break;
            }
            case VIRTIO_BLK_T_OUT: {  ///// dram -> disk
                request_size = 1;
                for (int i = 0; i < (int)sector_len; i = i + 4) {
                    Word d = ram_ld(sector_adr, 4, mmem);
                    dsk_st(header.sector_num * SECTOR_SIZE + i, d, 4, mdsk);
                }
                ram_st(sector_adr + sector_len - 1, 0, 1, mmem);  //  VIRTIO_BLK_S_OK
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
      sector(new Byte[DISK_SIZE]),  // sector is nog good name, rename this!
      Queue(nullptr),
      DeviceFeaturesSel(0),
      DriverFeatures(0),
      DriverFeaturesSel(0),
      InterruptStatus(0),
      Status(0),
      QueueSel(0),
      QueueNum(0) {}

Word Disk::disk_read(Address offset) {
    Word rdata = 0;
    switch (offset) {
        case 0x000:
            rdata = DISK_MAGIC_VALUE;
            break;
        case 0x004:
            rdata = DISK_VERSION;
            break;
        case 0x008:
            rdata = DISK_DEVIDE_ID;
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
    // printf("%8ld:CALL Disk READ mem[%x]->%x\n", cpu->mtime, offset, rdata);
    return rdata;
}

void Disk::disk_write(CPU* cpu, Address offset, Word wdata) {
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
            //        printf("__ disk_request %10ld: %d\n", cpu->mtime, wdata);
            if (mm.s_use_uc) {
                cc.pc = 0;
                cc.Qnum = QueueNum;
                cc.Mode = 2;
                cc.Qsel = wdata;
                for (int i = 0; i < 32; i++) cc.reg[i] = 0;
                cc.reg[11] = 0x8000;
                while (cc.exec());
            } else {
                disk_request(mmem, sector, QueueNum, &Queue[wdata]); /* request */
            }
            InterruptStatus |= 1;
            cpu->plic_set_irq(VIRTIO_DISK_IRQ, 1);
            break;
        }
        case 0x064: {
            InterruptStatus &= ~wdata;
            if (InterruptStatus == 0) {
                cpu->plic_set_irq(VIRTIO_DISK_IRQ, 0);
            }
            break;
        }
    }
}
