/**
 * @file Machine.cpp
 * @brief SimRV implementation unit.
 */
#include "Machine.hpp"

#include <fstream>
#include <iomanip>
#include <string>

#include "Module.hpp"
void set_options(Machine* m, int argc, char* argv[]);

extern Machine mm; /* class machine                     */
extern Microcn cc; /* I/O controller (micro-controller) */

Machine::Machine() : mmio_router_(*this) {}

namespace simrv::machine_detail {
constexpr size_t D_SIZE_DRAM = (9u * 1024u * 1024u);   // 9MB of bbl + kernel
constexpr size_t D_SIZE_DEVT = (4u * 1024u);           // 4KB of device tree
constexpr size_t D_SIZE_DISK = (16u * 1024u * 1024u);  // 16MB of disk image
constexpr Address D_DEVT_OFFSET = static_cast<Address>(16u * 1024u * 1024u);
constexpr Address D_QUEUE_STRIDE = static_cast<Address>(0x24u);
constexpr Address D_QUEUE_READY_OFFSET = static_cast<Address>(0x0u);
constexpr Address D_QUEUE_NOTIFY_OFFSET = static_cast<Address>(0x4u);
constexpr Address D_QUEUE_DESC_LOW_OFFSET = static_cast<Address>(0x8u);
constexpr Address D_QUEUE_DESC_HIGH_OFFSET = static_cast<Address>(0xCu);
constexpr Address D_QUEUE_AVAIL_LOW_OFFSET = static_cast<Address>(0x10u);
constexpr Address D_QUEUE_AVAIL_HIGH_OFFSET = static_cast<Address>(0x14u);
constexpr Address D_QUEUE_USED_LOW_OFFSET = static_cast<Address>(0x18u);
constexpr Address D_QUEUE_USED_HIGH_OFFSET = static_cast<Address>(0x1Cu);
constexpr Address D_QUEUE_LAST_AVAIL_OFFSET = static_cast<Address>(0x20u);

constexpr Word pte_access_index(PteAccess access) { return static_cast<Word>(access); }

void binfile_gen(CPU* /*s*/, Byte* ram, Byte* sector) {
    std::ofstream out("inits.bin", std::ios::binary);
    if (!out.is_open()) {
        printf("__ Error: cannot create inits.bin\n");
        exit(1);
    }
    out.write(reinterpret_cast<const char*>(ram), D_SIZE_DRAM);
    out.write(reinterpret_cast<const char*>(ram + D_DEVT_OFFSET), D_SIZE_DEVT);
    out.write(reinterpret_cast<const char*>(sector), D_SIZE_DISK);
    out.close();
    printf("__ File inits.bin was generated.\n");

    std::ifstream in("inits.bin", std::ios::binary);
    if (!in.is_open()) {
        printf("__ Error: cannot reopen inits.bin\n");
        exit(1);
    }
    int i = 0;
    Word sum = 0;
    Word buf;
    while (in.read(reinterpret_cast<char*>(&buf), sizeof(buf))) {
        sum += buf;
        i++;
    }
    printf("__ %8d byte file, checksum %08x\n\n", i * 4, sum);
    exit(0);
}

void load_initram(char* fname, Byte* ram) {
    std::ifstream in(fname, std::ios::binary);
    if (!in.is_open()) {
        fprintf(stdout, "__ Error: image_file %s cannot be found\n", fname);
        exit(0);
    }
    uint8_t tmp = 0;
    int i = 0;
    while (in.read(reinterpret_cast<char*>(&tmp), sizeof(tmp))) {
        ram[i++] = static_cast<Byte>(tmp);
    }
}

void load_initmem(std::string file, Byte* ram) {
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open()) {
        fprintf(stdout, "__ Error: image_file cannot be found\n");
        exit(-1);
    }
    uint8_t tmp = 0;
    int file_size = 0;
    while (in.read(reinterpret_cast<char*>(&tmp), sizeof(tmp))) {
        ram[file_size++] = static_cast<Byte>(tmp);
    }
}

Word ram_read(Address addr, Instruction funct3, Byte* ram) {
    Word rdata = 0;
    int n = (1 << (funct3 & 0x3));
    for (int i = 0; i < n; i++) {
        rdata |= static_cast<Word>(std::to_integer<uint8_t>(ram[(addr + i) & simrv::memory::kDramMask]))
                 << (8 * i);
    }

    if ((funct3 & 0x4) == 0) { /* signed extension */
        Word sign_mask = (~((Word)0)) << (8 * n - 1);
        rdata |= ((sign_mask & rdata) ? sign_mask : 0);
    }
    return rdata;
}

Word disk_read(Address addr, Word n, Byte* dsk) {
    if (n != 1 && n != 2 && n != 4) {
        printf("__ Error: dsk_r() not supported n=%d\n", n);
        exit(0);
    }
    Word data = 0;

    for (int i = 0; i < (int)n; i++) {
        data |= static_cast<Word>(std::to_integer<uint8_t>(dsk[(addr + i)])) << (8 * i);
    }
    return data;
}

Word queue_read(Address addr, QueueState* q) {
    Word rdata = 0;
    int idx = static_cast<int>(addr / D_QUEUE_STRIDE);
    switch (addr % D_QUEUE_STRIDE) {
        case D_QUEUE_READY_OFFSET:
            rdata = q[idx].Ready;
            break;
        case D_QUEUE_NOTIFY_OFFSET:
            rdata = q[idx].Notify;
            break;
        case D_QUEUE_DESC_LOW_OFFSET:
            rdata = q[idx].DescLow;
            break;
        case D_QUEUE_DESC_HIGH_OFFSET:
            rdata = q[idx].DescHigh;
            break;
        case D_QUEUE_AVAIL_LOW_OFFSET:
            rdata = q[idx].AvailLow;
            break;
        case D_QUEUE_AVAIL_HIGH_OFFSET:
            rdata = q[idx].AvailHigh;
            break;
        case D_QUEUE_USED_LOW_OFFSET:
            rdata = q[idx].UsedLow;
            break;
        case D_QUEUE_USED_HIGH_OFFSET:
            rdata = q[idx].UsedHigh;
            break;
        case D_QUEUE_LAST_AVAIL_OFFSET:
            rdata = q[idx].last_avail_idx;
            break;
        default:
            rdata = 0;
            break;
    }
    return rdata;
}

void queue_write(Address addr, Word wdata, QueueState* q) {
    int idx = static_cast<int>(addr / D_QUEUE_STRIDE);
    switch (addr % D_QUEUE_STRIDE) {
        case D_QUEUE_READY_OFFSET:
            q[idx].Ready = wdata;
            break;
        case D_QUEUE_NOTIFY_OFFSET:
            q[idx].Notify = wdata;
            break;
        case D_QUEUE_DESC_LOW_OFFSET:
            q[idx].DescLow = wdata;
            break;
        case D_QUEUE_DESC_HIGH_OFFSET:
            q[idx].DescHigh = wdata;
            break;
        case D_QUEUE_AVAIL_LOW_OFFSET:
            q[idx].AvailLow = wdata;
            break;
        case D_QUEUE_AVAIL_HIGH_OFFSET:
            q[idx].AvailHigh = wdata;
            break;
        case D_QUEUE_USED_LOW_OFFSET:
            q[idx].UsedLow = wdata;
            break;
        case D_QUEUE_USED_HIGH_OFFSET:
            q[idx].UsedHigh = wdata;
            break;
        case D_QUEUE_LAST_AVAIL_OFFSET:
            q[idx].last_avail_idx = wdata;
            break;
        default:
            break;
    }
    return;
}

int page_walk(Address v_addr, Address* p_addr, PteAccess access, CPU* cpu, Byte* mmem) {
    /* level 1 */
    Word vpn1 = (v_addr >> 22) & 0x3FF;
    Word L1_pte_addr = ((cpu->satp & 0x3FFFFF) << 12) + vpn1 * 4;
    Word L1_pte = ram_read(L1_pte_addr, static_cast<Instruction>(Funct3::Lw), mmem);
    Word L1_xwr =
        (cpu->mstatus & enum_mask(MstatusBit::Mxr) ? L1_pte >> 1 | L1_pte >> 3 : L1_pte >> 1) & 7;
    Word L1_p_addr = (v_addr & 0x3FFFFF) | (((L1_pte >> 10) << 12) & ~0x3FFFFF);
    Word L1_write = !(L1_pte & enum_mask(PteFlag::A)) ||
                    (!(L1_pte & enum_mask(PteFlag::D)) && access == PteAccess::Write);
    Word L1_success =
        !(L1_xwr == 2 || L1_xwr == 6 ||
          (cpu->priv == kPrivSupervisor &&
           ((L1_pte & enum_mask(PteFlag::U)) && !(cpu->mstatus & enum_mask(MstatusBit::Sum)))) ||
          (cpu->priv == kPrivUser && (!(L1_pte & enum_mask(PteFlag::U)))) ||
          ((L1_xwr >> pte_access_index(access)) & 1) == 0);
    /* level 0 */
    Word vpn0 = (v_addr >> 12) & 0x3FF;
    Word L0_pte_addr = ((L1_pte >> 10) << 12) + vpn0 * 4;
    Word L0_pte = ram_read(L0_pte_addr, static_cast<Instruction>(Funct3::Lw), mmem);
    Word L0_xwr =
        (cpu->mstatus & enum_mask(MstatusBit::Mxr) ? L0_pte >> 1 | L0_pte >> 3 : L0_pte >> 1) & 7;
    Word L0_p_addr = (v_addr & 0xFFF) | (((L0_pte >> 10) << 12) & ~0xFFF);
    Word L0_write = !(L0_pte & enum_mask(PteFlag::A)) ||
                    (!(L0_pte & enum_mask(PteFlag::D)) && access == PteAccess::Write);
    Word L0_success =
        !(L0_xwr == 2 || L0_xwr == 6 ||
          ((cpu->priv == kPrivSupervisor) &&
           ((L0_pte & enum_mask(PteFlag::U)) && !(cpu->mstatus & enum_mask(MstatusBit::Sum)))) ||
          ((cpu->priv == kPrivUser) && (!(L0_pte & enum_mask(PteFlag::U)))) ||
          ((L0_xwr >> pte_access_index(access)) & 1) == 0);
    /* success */
    int ret = 0;
    if (!(L1_pte & enum_mask(PteFlag::V)))
        ret = -1;
    else if (L1_xwr != 0)
        ret = L1_success ? 0 : -1;
    else if (!(L0_pte & enum_mask(PteFlag::V)))
        ret = -1;
    else if (L0_xwr != 0)
        ret = L0_success ? 0 : -1;
    else
        ret = -1;

    /* phys_addr */
    if (ret)
        *p_addr = 0;
    else if (L1_success)
        *p_addr = L1_p_addr;
    else if (L0_success)
        *p_addr = L0_p_addr;

    /* update pte */
    Word L1_pte_write =
        L1_pte | enum_mask(PteFlag::A) | (access == PteAccess::Write ? enum_mask(PteFlag::D) : 0);
    Word L0_pte_write =
        L0_pte | enum_mask(PteFlag::A) | (access == PteAccess::Write ? enum_mask(PteFlag::D) : 0);
    int we =
        ((L1_xwr != 0 && L1_success) && (L1_write)) || ((L0_xwr != 0 && L0_success) && (L0_write));
    Word w_addr = (L1_xwr != 0 && L1_success) ? L1_pte_addr : L0_pte_addr;
    Word w_data = (L1_xwr != 0 && L1_success) ? L1_pte_write : L0_pte_write;
    if (we) {
        for (int i = 0; i < 4; i++) {
            mmem[(w_addr + i) & simrv::memory::kDramMask] =
                static_cast<Byte>(static_cast<uint8_t>((w_data >> (8 * i)) & 0xFF));
        }
    }
    return ret;
}

}  // namespace simrv::machine_detail

using namespace simrv::machine_detail;

Word Machine::target_read(Address v_addr, Instruction funct3) {
    Word rdata = 0;
    Address p_addr;
    TLBEntry* entry = &cpu->TLB_data_r[(v_addr >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1)];

    if (cpu->priv == kPrivMachine || (cpu->satp >> 31) == 0) {
        p_addr = v_addr;
    } else if (entry->v_addr == (v_addr & ~simrv::memory::kPageMask)) {
        p_addr = entry->p_addr + (v_addr & simrv::memory::kPageMask);
    } else {
        if (page_walk(v_addr, &p_addr, PteAccess::Read, cpu, mmem)) {
            cpu->pending_exception = enum_mask(ExceptionCode::LoadPageFault);
            cpu->pending_tval = v_addr;
        } else {
            entry->v_addr = v_addr & ~simrv::memory::kPageMask;
            entry->p_addr = p_addr & ~simrv::memory::kPageMask;
        }
    }

    if (cpu->pending_exception == ~0u) {
        if (mmio_router_.read(p_addr, rdata)) return rdata;

        switch (p_addr & 0xF0000000) {
            case 0x10000000:
                break;
            case 0x20000000:
                break;
            case 0x30000000:
                break;
            case 0x70000000:
                break;

            default: {  ///// RAM (0x80000000 ~ )
                rdata = ram_read(p_addr, funct3, mmem);
                break;
            }
        }
    }
    return rdata;
}

void Machine::target_write(Address v_addr, Word wdata, Instruction funct3) {
    Address p_addr;
    TLBEntry* entry = &cpu->TLB_data_w[(v_addr >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1)];

    if (cpu->priv == kPrivMachine || (cpu->satp >> 31) == 0) {
        p_addr = v_addr;
    } else if (entry->v_addr == (v_addr & ~simrv::memory::kPageMask)) {
        p_addr = entry->p_addr + (v_addr & simrv::memory::kPageMask);
    } else {
        if (page_walk(v_addr, &p_addr, PteAccess::Write, cpu, mmem)) {
            cpu->pending_exception = enum_mask(ExceptionCode::StorePageFault);
            cpu->pending_tval = v_addr;
        } else {
            entry->v_addr = v_addr & ~simrv::memory::kPageMask;
            entry->p_addr = p_addr & ~simrv::memory::kPageMask;
        }
    }

    if (cpu->pending_exception == ~0u) {
        if (mmio_router_.write(p_addr, wdata)) return;

        switch (p_addr & 0xF0000000) {
            case 0x10000000:
                break;
            case 0x20000000:
                break;
            case 0x30000000:
                break;
            case 0x70000000:
                break;

            default: {  ///// RAM (0x80000000 ~ )
                for (int i = 0; i < (1 << funct3); i++) {
                    mmem[(p_addr + i) & simrv::memory::kDramMask] =
                        static_cast<Byte>(static_cast<uint8_t>((wdata >> (8 * i)) & 0xFF));
                }
                if (s_isatest && funct3 == static_cast<Instruction>(Funct3::Sw) &&
                    p_addr == s_isatest_tohost) {
                    tohost = wdata;
                }
                //            mem->ram_write(p_addr, wdata, funct3);
                break;
            }
        }
    }
}

// void initfile_gen(CPU *s, Byte*ram){
//     FILE *f = fopen("init_mem.txt", "w"); /* memory initialize file
//     */ for(int i=0; i<simrv::memory::kDramSize; i++) fprintf(f, "%x\n", std::to_integer<uint8_t>(ram[i]));
//     fclose(f);
//     printf("\n__ file init_mem.txt was generated after %ld cycle\n",
//     s->mtime);

//     f = fopen("init_reg.txt", "wb"); /* register initialize file */
//     fprintf(f, "p.pc=32'h%08x;\n",  s->pc);
//     for(int i=1; i<32; i++) fprintf(f, "p.regs.mem[%d]=32'h%08x;\n", i,
//     s->reg[i]); fprintf(f, "p.mstatus     =32'h%08x;\n", s->mstatus);
//     fprintf(f, "p.mtvec       =32'h%08x;\n", s->mtvec);
//     fprintf(f, "p.mscratch    =32'h%08x;\n", s->mscratch);
//     fprintf(f, "p.mepc        =32'h%08x;\n", s->mepc);
//     fprintf(f, "p.mcause      =32'h%08x;\n", s->mcause);
//     fprintf(f, "p.mtval       =32'h%08x;\n", s->mtval);
//     fprintf(f, "p.mhartid     =32'h%08x;\n", s->mhartid);
//     fprintf(f, "p.misa        =32'h%08x;\n", s->misa);
//     fprintf(f, "p.mie         =32'h%08x;\n", s->mie);
//     fprintf(f, "p.mip         =32'h%08x;\n", s->mip);
//     fprintf(f, "p.medeleg     =32'h%08x;\n", s->medeleg);
//     fprintf(f, "p.mideleg     =32'h%08x;\n", s->mideleg);
//     fprintf(f, "p.mcounteren  =32'h%08x;\n", s->mcounteren);
//     fprintf(f, "p.stvec       =32'h%08x;\n", s->stvec);
//     fprintf(f, "p.sscratch    =32'h%08x;\n", s->sscratch);
//     fprintf(f, "p.sepc        =32'h%08x;\n", s->sepc);
//     fprintf(f, "p.scause      =32'h%08x;\n", s->scause);
//     fprintf(f, "p.stval       =32'h%08x;\n", s->stval);
//     fprintf(f, "p.satp        =32'h%08x;\n", s->satp);
//     fprintf(f, "p.scounteren  =32'h%08x;\n", s->scounteren);
//     fprintf(f, "p.priv        =32'h%08x;\n", s->priv);

//     fprintf(f, "p.mtime       =64'h%016lx;\n", s->mtime);
//     fprintf(f, "p.mtimecmp    =64'h%016lx;\n", s->mtimecmp);

//     fprintf(f, "p.load_res    =32'h%08x;\n", s->load_res);
//     fprintf(f, "p.pending_exception   =32'h%08x;\n", s->pending_exception);
//     fprintf(f, "p.pending_tval=32'h%08x;\n", s->pending_tval);

//     for(int i=0; i<simrv::memory::kTlbSize; i++) {
//         fprintf(f, "mmu.TLB_inst_r.r_valid[%d] =%d;\n",
//                 i, !(s->TLB_inst_r[i].p_addr == -1u));
//         fprintf(f, "mmu.TLB_inst_r.mem[%d][39:22] =18'h%05x;\n",
//                 i, s->TLB_inst_r[i].v_addr >> 14);
//         fprintf(f, "mmu.TLB_inst_r.mem[%d][21:0] =22'h%06x;\n",
//                 i, s->TLB_inst_r[i].p_addr >> 10);
//     }
//     for(int i=0; i<simrv::memory::kTlbSize; i++) {
//         fprintf(f, "mmu.TLB_data_r.r_valid[%d] =%d;\n",
//                 i, !(s->TLB_data_r[i].p_addr == -1u));
//         fprintf(f, "mmu.TLB_data_r.mem[%d][39:22] =18'h%05x;\n",
//                 i, s->TLB_data_r[i].v_addr >> 14);
//         fprintf(f, "mmu.TLB_data_r.mem[%d][21:0] =22'h%06x;\n",
//                 i, s->TLB_data_r[i].p_addr >> 10);
//     }
//     for(int i=0; i<simrv::memory::kTlbSize; i++) {
//         fprintf(f, "mmu.TLB_data_w.r_valid[%d] =%d;\n",
//                 i, !(s->TLB_data_w[i].p_addr == -1u));
//         fprintf(f, "mmu.TLB_data_w.mem[%d][39:22] =18'h%05x;\n",
//                 i, s->TLB_data_w[i].v_addr >> 14);
//         fprintf(f, "mmu.TLB_data_w.mem[%d][21:0] =22'h%06x;\n",
//                 i, s->TLB_data_w[i].p_addr >> 10);
//     }
//     fclose(f);
//     printf("__ file init_reg.txt was generated after %ld cycle\n", s->mtime);
// }

// void initfile_genS(CPU *s, Byte*ram, Console *cons, Disk *disk, uint8_t
// *sector){
//     FILE *f = fopen("xinitmem.bin", "wb"); /* memory initialize file
//     */ fwrite(ram, sizeof(Byte), simrv::memory::kDramSize, f); fclose(f); f =
//     fopen("xinitdisk.bin", "wb"); /* memory initialize file  */
//     fwrite(sector, sizeof(Byte), simrv::virtio::kDiskSize, f);
//     fclose(f);

//     printf("\n__ file initmem.bin and initdisk.bin were generated after %ld
//     cycle\n", s->mtime);

//     f = fopen("xinitreg.txt", "wb"); /* register initialize file */
//     fprintf(f, "cpu->pc=0x%08x;\n",  s->pc);
//     for(int i=1; i<32; i++) fprintf(f, "cpu->reg[%d]=0x%08x;\n", i,
//     s->reg[i]); fprintf(f, "cpu->mstatus     =0x%08x;\n", s->mstatus);
//     fprintf(f, "cpu->mtvec       =0x%08x;\n", s->mtvec);
//     fprintf(f, "cpu->mscratch    =0x%08x;\n", s->mscratch);
//     fprintf(f, "cpu->mepc        =0x%08x;\n", s->mepc);
//     fprintf(f, "cpu->mcause      =0x%08x;\n", s->mcause);
//     fprintf(f, "cpu->mtval       =0x%08x;\n", s->mtval);
//     fprintf(f, "cpu->mhartid     =0x%08x;\n", s->mhartid);
//     fprintf(f, "cpu->misa        =0x%08x;\n", s->misa);
//     fprintf(f, "cpu->mie         =0x%08x;\n", s->mie);
//     fprintf(f, "cpu->mip         =0x%08x;\n", s->mip);
//     fprintf(f, "cpu->medeleg     =0x%08x;\n", s->medeleg);
//     fprintf(f, "cpu->mideleg     =0x%08x;\n", s->mideleg);
//     fprintf(f, "cpu->mcounteren  =0x%08x;\n", s->mcounteren);
//     fprintf(f, "cpu->stvec       =0x%08x;\n", s->stvec);
//     fprintf(f, "cpu->sscratch    =0x%08x;\n", s->sscratch);
//     fprintf(f, "cpu->sepc        =0x%08x;\n", s->sepc);
//     fprintf(f, "cpu->scause      =0x%08x;\n", s->scause);
//     fprintf(f, "cpu->stval       =0x%08x;\n", s->stval);
//     fprintf(f, "cpu->satp        =0x%08x;\n", s->satp);
//     fprintf(f, "cpu->scounteren  =0x%08x;\n", s->scounteren);
//     fprintf(f, "cpu->priv        =0x%08x;\n", s->priv);

//     fprintf(f, "cpu->mtime       =0x%016lx;\n", s->mtime);
//     fprintf(f, "cpu->mtimecmp    =0x%016lx;\n", s->mtimecmp);

//     fprintf(f, "cpu->load_res    =0x%08x;\n", s->load_res);
//     fprintf(f, "cpu->pending_exception   =0x%08x;\n", s->pending_exception);
//     fprintf(f, "cpu->pending_tval=0x%08x;\n", s->pending_tval);

//     for(int i=0; i<simrv::memory::kTlbSize; i++) {
//         fprintf(f, "cpu->TLB_inst_r[%d].p_addr =0x%08x;\n",
//                 i, s->TLB_inst_r[i].p_addr);
//         fprintf(f, "cpu->TLB_inst_r[%d].v_addr =0x%08x;\n",
//                 i, s->TLB_inst_r[i].v_addr);
//     }
//     for(int i=0; i<simrv::memory::kTlbSize; i++) {
//         fprintf(f, "cpu->TLB_data_r[%d].p_addr =0x%08x;\n",
//                 i, s->TLB_data_r[i].p_addr);
//         fprintf(f, "cpu->TLB_data_r[%d].v_addr =0x%08x;\n",
//                 i, s->TLB_data_r[i].v_addr);
//     }
//     for(int i=0; i<simrv::memory::kTlbSize; i++) {
//         fprintf(f, "cpu->TLB_data_w[%d].p_addr =0x%08x;\n",
//                 i, s->TLB_data_w[i].p_addr);
//         fprintf(f, "cpu->TLB_data_w[%d].v_addr =0x%08x;\n",
//                 i, s->TLB_data_w[i].v_addr);
//     }

//     fprintf(f, "console->QueueSel       =0x%08x;\n", cons->QueueSel);
//     fprintf(f, "console->QueueNum       =0x%08x;\n", cons->QueueNum);
//     for(int i=0; i<simrv::virtio::kConsoleMaxQueueNum; i++){
//         fprintf(f, "console->Queue[%d].Ready          =0x%08x;\n", i,
//         cons->Queue[i].Ready); fprintf(f, "console->Queue[%d].Notify
//         =0x%08x;\n", i, cons->Queue[i].Notify); fprintf(f,
//         "console->Queue[%d].DescLow        =0x%08x;\n", i,
//         cons->Queue[i].DescLow); fprintf(f, "console->Queue[%d].DescHigh
//         =0x%08x;\n", i, cons->Queue[i].DescHigh); fprintf(f,
//         "console->Queue[%d].AvailLow       =0x%08x;\n", i,
//         cons->Queue[i].AvailLow); fprintf(f, "console->Queue[%d].AvailHigh
//         =0x%08x;\n", i, cons->Queue[i].AvailHigh); fprintf(f,
//         "console->Queue[%d].UsedLow        =0x%08x;\n", i,
//         cons->Queue[i].UsedLow); fprintf(f, "console->Queue[%d].UsedHigh
//         =0x%08x;\n", i, cons->Queue[i].UsedHigh); fprintf(f,
//         "console->Queue[%d].last_avail_idx =0x%08x;\n", i,
//         cons->Queue[i].last_avail_idx);
//     }
//     fprintf(f, "console->InterruptStatus=0x%08x;\n", cons->InterruptStatus);
//     fprintf(f, "console->Status         =0x%08x;\n", cons->Status);

//     fprintf(f, "disk->QueueSel       =0x%08x;\n", disk->QueueSel);
//     fprintf(f, "disk->QueueNum       =0x%08x;\n", disk->QueueNum);
//     for(int i=0; i<simrv::virtio::kDiskMaxQueueNum; i++){
//         fprintf(f, "disk->Queue[%d].Ready          =0x%08x;\n", i,
//         disk->Queue[i].Ready); fprintf(f, "disk->Queue[%d].Notify
//         =0x%08x;\n", i, disk->Queue[i].Notify); fprintf(f,
//         "disk->Queue[%d].DescLow        =0x%08x;\n", i,
//         disk->Queue[i].DescLow); fprintf(f, "disk->Queue[%d].DescHigh
//         =0x%08x;\n", i, disk->Queue[i].DescHigh); fprintf(f,
//         "disk->Queue[%d].AvailLow       =0x%08x;\n", i,
//         disk->Queue[i].AvailLow); fprintf(f, "disk->Queue[%d].AvailHigh
//         =0x%08x;\n", i, disk->Queue[i].AvailHigh); fprintf(f,
//         "disk->Queue[%d].UsedLow        =0x%08x;\n", i,
//         disk->Queue[i].UsedLow); fprintf(f, "disk->Queue[%d].UsedHigh
//         =0x%08x;\n", i, disk->Queue[i].UsedHigh); fprintf(f,
//         "disk->Queue[%d].last_avail_idx =0x%08x;\n", i,
//         disk->Queue[i].last_avail_idx);
//     }
//     fprintf(f, "disk->InterruptStatus=0x%08x;\n", disk->InterruptStatus);
//     fprintf(f, "disk->Status         =0x%08x;\n", disk->Status);

//     fclose(f);
//     printf("__ file initreg.txt was generated after %ld cycle\n", s->mtime);
// }

void initfile_gen2(CPU* s, Byte* ram, Console* cons, Disk* disk, Byte* sector) {
    FILE* f = fopen("init_mem.txt", "w"); /* memory initialize file  */
    for (Address i = 0; i < simrv::memory::kDramSize; i++) fprintf(f, "%x\n", std::to_integer<uint8_t>(ram[i]));
    fclose(f);
    printf("__ file init_mem.txt was generated after %ld cycle\n", s->mtime);
    f = fopen("init_dsk.txt", "w"); /* memory initialize file  */
    for (Word i = 0; i < simrv::virtio::kDiskSize; i++) fprintf(f, "%x\n", std::to_integer<uint8_t>(sector[i]));
    fclose(f);
    printf("__ file init_dsk.txt was generated after %ld cycle\n", s->mtime);

    f = fopen("init_reg.txt", "wb"); /* register initialize file */
    fprintf(f, "p.pc=32'h%08x;\n", s->pc);
    for (int i = 1; i < 32; i++) fprintf(f, "p.regs.mem[%d]=32'h%08x;\n", i, s->reg[i]);
    fprintf(f, "p.mstatus     =32'h%08x;\n", s->mstatus);
    fprintf(f, "p.mtvec       =32'h%08x;\n", s->mtvec);
    fprintf(f, "p.mscratch    =32'h%08x;\n", s->mscratch);
    fprintf(f, "p.mepc        =32'h%08x;\n", s->mepc);
    fprintf(f, "p.mcause      =32'h%08x;\n", s->mcause);
    fprintf(f, "p.mtval       =32'h%08x;\n", s->mtval);
    fprintf(f, "p.mhartid     =32'h%08x;\n", s->mhartid);
    fprintf(f, "p.misa        =32'h%08x;\n", s->misa);
    fprintf(f, "p.mie         =32'h%08x;\n", s->mie);
    fprintf(f, "p.mip         =32'h%08x;\n", s->mip);
    fprintf(f, "p.medeleg     =32'h%08x;\n", s->medeleg);
    fprintf(f, "p.mideleg     =32'h%08x;\n", s->mideleg);
    fprintf(f, "p.mcounteren  =32'h%08x;\n", s->mcounteren);
    fprintf(f, "p.stvec       =32'h%08x;\n", s->stvec);
    fprintf(f, "p.sscratch    =32'h%08x;\n", s->sscratch);
    fprintf(f, "p.sepc        =32'h%08x;\n", s->sepc);
    fprintf(f, "p.scause      =32'h%08x;\n", s->scause);
    fprintf(f, "p.stval       =32'h%08x;\n", s->stval);
    fprintf(f, "p.satp        =32'h%08x;\n", s->satp);
    fprintf(f, "p.scounteren  =32'h%08x;\n", s->scounteren);
    fprintf(f, "p.priv        =32'h%08x;\n", s->priv);

    fprintf(f, "p.mtime       =64'h%016lx;\n", s->mtime);
    fprintf(f, "p.mtimecmp    =64'h%016lx;\n", s->mtimecmp);

    fprintf(f, "p.load_res    =32'h%08x;\n", s->load_res);
    fprintf(f, "p.reserved    = 1'h%01x;\n", s->reserved);
    fprintf(f, "p.pending_exception   =32'h%08x;\n", s->pending_exception);
    fprintf(f, "p.pending_tval=32'h%08x;\n", s->pending_tval);

    for (Word i = 0; i < simrv::memory::kTlbSize; i++) {
        fprintf(f, "mmu.TLB_inst_r.r_valid[%d] =%d;\n", i, !(s->TLB_inst_r[i].p_addr == -1u));
        fprintf(f, "mmu.TLB_inst_r.mem[%d][39:22] =18'h%05x;\n", i, s->TLB_inst_r[i].v_addr >> 14);
        fprintf(f, "mmu.TLB_inst_r.mem[%d][21:0] =22'h%06x;\n", i, s->TLB_inst_r[i].p_addr >> 10);
    }
    for (Word i = 0; i < simrv::memory::kTlbSize; i++) {
        fprintf(f, "mmu.TLB_data_r.r_valid[%d] =%d;\n", i, !(s->TLB_data_r[i].p_addr == -1u));
        fprintf(f, "mmu.TLB_data_r.mem[%d][39:22] =18'h%05x;\n", i, s->TLB_data_r[i].v_addr >> 14);
        fprintf(f, "mmu.TLB_data_r.mem[%d][21:0] =22'h%06x;\n", i, s->TLB_data_r[i].p_addr >> 10);
    }
    for (Word i = 0; i < simrv::memory::kTlbSize; i++) {
        fprintf(f, "mmu.TLB_data_w.r_valid[%d] =%d;\n", i, !(s->TLB_data_w[i].p_addr == -1u));
        fprintf(f, "mmu.TLB_data_w.mem[%d][39:22] =18'h%05x;\n", i, s->TLB_data_w[i].v_addr >> 14);
        fprintf(f, "mmu.TLB_data_w.mem[%d][21:0] =22'h%06x;\n", i, s->TLB_data_w[i].p_addr >> 10);
    }

    fprintf(f, "mmu.console.QueueSel       =32'h%08x;\n", cons->QueueSel);
    fprintf(f, "mmu.console.QueueNum       =32'h%08x;\n", cons->QueueNum);
    for (Word i = 0; i < simrv::virtio::kConsoleMaxQueueNum; i++) {
        fprintf(f, "mmu.console.Queue[%d*9+0] =32'h%08x;\n", i, cons->Queue[i].Ready);
        fprintf(f, "mmu.console.Queue[%d*9+1] =32'h%08x;\n", i, cons->Queue[i].Notify);
        fprintf(f, "mmu.console.Queue[%d*9+2] =32'h%08x;\n", i, cons->Queue[i].DescLow);
        fprintf(f, "mmu.console.Queue[%d*9+3] =32'h%08x;\n", i, cons->Queue[i].DescHigh);
        fprintf(f, "mmu.console.Queue[%d*9+4] =32'h%08x;\n", i, cons->Queue[i].AvailLow);
        fprintf(f, "mmu.console.Queue[%d*9+5] =32'h%08x;\n", i, cons->Queue[i].AvailHigh);
        fprintf(f, "mmu.console.Queue[%d*9+6] =32'h%08x;\n", i, cons->Queue[i].UsedLow);
        fprintf(f, "mmu.console.Queue[%d*9+7] =32'h%08x;\n", i, cons->Queue[i].UsedHigh);
        fprintf(f, "mmu.console.Queue[%d*9+8] =32'h%08x;\n", i, cons->Queue[i].last_avail_idx);
    }
    fprintf(f, "mmu.console.InterruptStatus=32'h%08x;\n", cons->InterruptStatus);
    fprintf(f, "mmu.console.Status         =32'h%08x;\n", cons->Status);

    fprintf(f, "mmu.disk.QueueSel       =32'h%08x;\n", disk->QueueSel);
    fprintf(f, "mmu.disk.QueueNum       =32'h%08x;\n", disk->QueueNum);
    for (Word i = 0; i < simrv::virtio::kDiskMaxQueueNum; i++) {
        fprintf(f, "mmu.disk.Queue[%d*9+0] =32'h%08x;\n", i, disk->Queue[i].Ready);
        fprintf(f, "mmu.disk.Queue[%d*9+1] =32'h%08x;\n", i, disk->Queue[i].Notify);
        fprintf(f, "mmu.disk.Queue[%d*9+2] =32'h%08x;\n", i, disk->Queue[i].DescLow);
        fprintf(f, "mmu.disk.Queue[%d*9+3] =32'h%08x;\n", i, disk->Queue[i].DescHigh);
        fprintf(f, "mmu.disk.Queue[%d*9+4] =32'h%08x;\n", i, disk->Queue[i].AvailLow);
        fprintf(f, "mmu.disk.Queue[%d*9+5] =32'h%08x;\n", i, disk->Queue[i].AvailHigh);
        fprintf(f, "mmu.disk.Queue[%d*9+6] =32'h%08x;\n", i, disk->Queue[i].UsedLow);
        fprintf(f, "mmu.disk.Queue[%d*9+7] =32'h%08x;\n", i, disk->Queue[i].UsedHigh);
        fprintf(f, "mmu.disk.Queue[%d*9+8] =32'h%08x;\n", i, disk->Queue[i].last_avail_idx);
    }
    fprintf(f, "mmu.disk.InterruptStatus=32'h%08x;\n", disk->InterruptStatus);
    fprintf(f, "mmu.disk.Status         =32'h%08x;\n", disk->Status);

    fclose(f);
    printf("__ file init_reg.txt was generated after %ld cycle\n", s->mtime);
}

void Machine::instmix_output() {
    std::ofstream out("instmix.txt");
    if (!out.is_open()) {
        printf("__ Error: cannot open instmix.txt\n");
        return;
    }
    out << "INSTRUCTION MIX\n";
    int total = 0;
    for (int i = 0; i < OperationIdCount; i++) {
        out << simrv::module::OPERATION_NAME[i] << " : " << std::setw(10) << e_instmix[i] << '\n';
        total += e_instmix[i];
    }
    out << "TOTAL_____ : " << std::setw(10) << total << '\n';
    printf("__ file instmix.txt was generated after %ld cycle\n", cpu->mtime);
}

void Machine::display_result() {
    struct timeval t;
    gettimeofday(&t, NULL);
    Counter etime = (t.tv_sec - s_stime.tv_sec) * 1000000ul + t.tv_usec - s_stime.tv_usec;
    printf("__ Elapsed clocks (mtime)   : %11ld\n", cpu->mtime);
    printf("__ Executed instructions    : %11ld\n", e_icount);
    printf("__ Executed uc_instructions : %11ld\n", e_uc_cnt);
    printf("__ Fetched compressed insns : %11ld\n", e_ccount);
    printf("__ Elapsed time (usec)      : %11ld\n", etime);
    printf("__ Simulation speed (KIPS)  : %11ld\n", e_icount * 1000ul / etime);
    if (s_use_mix) instmix_output();
}

void Machine::exec() {
    if (s_gen_binfile) {
        binfile_gen(cpu, mmem, disk->sector);
    }
    while (r_running) {
        INI();  /* Initialize                    */
        IFA();  /* IF (Instruction Fetch)  Stage */
        IFB(1); /* IF (Instruction Fetch)  Stage */
        IFB(2); /* IF (Instruction Fetch)  Stage */
        IFC();  /* IF (Instruction Fetch)  Stage */
        CVT();  /* CVT(Convert)            Stage */
        ID_();  /* ID (Instruction Decode) Stage */
        OF_();  /* OF (Operand Fetch)      Stage */
        EX1();  /* EX1(Execution 1)        Stage */
        LD_();  /* LD (Load Data)          Stage */
        EX2();  /* EX2(Execution 2)        Stage */
        SD_();  /* SD (Store Data)         Stage */
        WB_();  /* WB (Write Back)         Stage */
        COM();  /* COM(Complete)           Stage */
        FIN();  /* Finalize                      */
        cpu->mtime++;
    }
}

void Machine::INI() {  ///// the first stage
    //    if(cpu->mtime==s_memimg)  { initfile_gen(cpu, mmem); }
    if (cpu->mtime == s_memimg) {
        initfile_gen2(cpu, mmem, console, disk, disk->sector);
    }

    Byte buf[9] = {static_cast<Byte>('r'), static_cast<Byte>('o'),  static_cast<Byte>('o'),
                   static_cast<Byte>('t'), static_cast<Byte>('\n'), static_cast<Byte>('t'),
                   static_cast<Byte>('o'), static_cast<Byte>('p'),  static_cast<Byte>('\n')};
    static int adr = 0;

    if (cpu->mtime > s_enabletimer) { /* enable timer after linux boot */
        console->fifo_en = static_cast<Byte>(1);
        console->cons_fifo = buf[adr % 9];

        if ((cpu->mtime & (Counter)0xfffff) == 0 &&
            console->fifo_en != static_cast<Byte>(0)) {                   // 2019-08-30
            int ret = console->MC_recieve_input();                        /* Keyboard */
            if (ret > 0) {
                cpu->plic_set_irq(simrv::virtio::kConsoleIrq, 1);
            }
            if (ret == -1) r_running = 0; /* break by Ctrl+q */
            adr++;
        } else if (cpu->mtimecmp < cpu->mtime) { /* Timer */
            cpu->mip |= enum_mask(MipBit::Mtip);
        }
    }

    cpu->pending_exception = ~0u; /* initialize regs */
    cpu->pending_tval = 0;        /* initialize regs */
}

constexpr Counter D_TRACEPC_INTERVAL = 1000;
void gen_traces(Register cpc) {
    static int flag = 0;
    static std::ofstream out;
    if ((mm.cpu->mtime % D_TRACEPC_INTERVAL) == 0) {
        if (flag == 0) {
            flag = 1;
            out.open("tracepc.txt");
            printf("__ generate trace file: tracepc.txt\n\n");
        }
        out << std::setfill('0') << std::setw(8) << std::dec
            << static_cast<int>(mm.cpu->mtime / D_TRACEPC_INTERVAL) << ' ' << std::hex
            << std::setw(8) << cpc << '\n';
        out.flush();
    }
}

void gen_bp_traces(Register cpc, Register jmp_pc, int r_opcode, int r_tkn) {
    static int flag = 0;
    static std::ofstream out;
    if (flag == 0) {
        flag = 1;
        out.open("bpred.txt");
        printf("__ generate trace file: bpred.txt\n\n");
        //        fprintf(f, "TC PC jump_or_branch b_taken jump branch\n");
    }

    const Opcode opcode = static_cast<Opcode>(r_opcode);
    int ir_jb = (opcode == Opcode::Jal) || (opcode == Opcode::Jalr) || (opcode == Opcode::Branch);
    int ir_jump = (opcode == Opcode::Jal) ? 2 : (opcode == Opcode::Jalr) ? 3 : 0;
    int ir_branch = (opcode == Opcode::Branch);

    unsigned int targ = (ir_jump | ir_branch) ? jmp_pc : 0;
    out << std::setfill('0') << std::setw(8) << std::dec << static_cast<int>(mm.cpu->mtime) << ' '
        << std::hex << std::setw(8) << cpc << ' ' << std::setw(8) << targ << std::dec << ' '
        << ir_jb << ' ' << r_tkn << ' ' << ir_jump << ' ' << ir_branch << '\n';
    out.flush();
}

constexpr Word CMD_PRINT_CHAR = 1; /* command for application mode using tohost */
constexpr Word CMD_POWER_OFF = 2;  /* command for application mode using tohost */
void Machine::FIN() {  ///// the last stage
    if (s_strace != 0 && cpu->mtime >= s_strace) gen_traces(pipeline_context_.cpc);
    if (cpu->mtime >= s_trace_begin && cpu->mtime <= s_trace_end) trace_output();
    if (cpu->mtime >= s_fincnt - 1) {
        printf("\n__finished by -e option\n");
        r_running = 0;
    }
    if (s_bp_trace) {
        gen_bp_traces(pipeline_context_.cpc, pipeline_context_.jmp_pc, pipeline_context_.opcode,
                      pipeline_context_.tkn);
    }

    if (s_isatest && tohost != 0) {
        if (tohost == 1) {
            printf("\n__ ISA TEST PASS\n");
        } else if (tohost & 1) {
            printf("\n__ ISA TEST FAIL code=%u (tohost=0x%08x)\n", tohost >> 1, tohost);
        } else {
            printf("\n__ ISA TEST TOHOST update=0x%08x\n", tohost);
        }
        r_running = 0;
    }

    if ((tohost >> 16) == CMD_POWER_OFF) {
        printf("\n__ Power off\n");
        r_running = 0;
    }
    if ((tohost >> 16) == CMD_PRINT_CHAR) {
        printf("%c", (char)(tohost & 0xff));
        tohost = 0;
        fflush(stdout);
    }
}

/* IF_(Instruction Fetch) stages                                                          */
void Machine::IFA() { /* address translation */
    Word w_padr1 = ~0u;
    Word w_padr2 = ~0u;
    Word w_vadr1 = cpu->pc;
    Word w_vadr2 = cpu->pc + 2;

    pipeline_context_.cpc = cpu->pc;

    if (cpu->priv == kPrivMachine || (cpu->satp >> 31) == 0) { /** No translation or protection **/
        w_padr1 = w_vadr1;
        w_padr2 = w_vadr2;
    } else {
        TLBEntry* tlb_e1 = &cpu->TLB_inst_r[(w_vadr1 >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1)];
        TLBEntry* tlb_e2 = &cpu->TLB_inst_r[(w_vadr2 >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1)];
        if (tlb_e1->v_addr == (w_vadr1 & ~simrv::memory::kPageMask)) {  ///// TLB hit for w_vadr1
            w_padr1 = tlb_e1->p_addr + (w_vadr1 & simrv::memory::kPageMask);
        }
        if (tlb_e2->v_addr == (w_vadr2 & ~simrv::memory::kPageMask)) {  ///// TLB hit for w_vadr2
            w_padr2 = tlb_e2->p_addr + (w_vadr2 & simrv::memory::kPageMask);
        }
    }
    pipeline_context_.padr1 = w_padr1;
    pipeline_context_.padr2 = w_padr2;
}

void Machine::IFB(int state) { /* page walk and TLB update */
    if (cpu->pending_exception != ~0u) return;

    Word w_padr = (state == 1) ? pipeline_context_.padr1 : pipeline_context_.padr2;
    Word* r_padr = (state == 1) ? &pipeline_context_.padr1 : &pipeline_context_.padr2;
    Word w_vadr = (state == 1) ? cpu->pc : cpu->pc + 2;
    if (w_padr == ~0u) {
        int pf = page_walk(w_vadr, &w_padr, PteAccess::Code, cpu, mmem);  // Page Walk
        if (pf) {
            cpu->pending_exception = enum_mask(ExceptionCode::FetchPageFault);
            cpu->pending_tval = w_vadr;
        } else {
            TLBEntry* tlb_e1 = &cpu->TLB_inst_r[(w_vadr >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1)];
            tlb_e1->v_addr = w_vadr & ~simrv::memory::kPageMask;  // update TLB entry
            tlb_e1->p_addr = w_padr & ~simrv::memory::kPageMask;  // update TLB entry
        }
    }
    *r_padr = w_padr;
}

void Machine::IFC() {
    if (cpu->pending_exception != ~0u) return;

    Word ir_l = ram_read(pipeline_context_.padr1, static_cast<Instruction>(Funct3::Lhu),
                         mmem); /* Note !! */
    Word ir_h = ram_read(pipeline_context_.padr2, static_cast<Instruction>(Funct3::Lhu),
                         mmem); /* Note !! */
    pipeline_context_.ir_org = (ir_h << 16) | (ir_l & 0xFFFF);
}

/* CVT(Convert) stage, OK                                                                 */
void Machine::CVT() {
    Instruction w_ir_tmp = decode_unit_.decompress(pipeline_context_.ir_org);
    bool w_compressed = decode_unit_.isCompressed(pipeline_context_.ir_org);

    // Check if decoded instruction is enabled by current MISA
    if (!instruction_enabled_by_misa(cpu->misa, w_ir_tmp, w_compressed)) {
        // Raise illegal instruction exception
        cpu->raise_exception(enum_mask(ExceptionCode::IllegalInstruction),
                             pipeline_context_.ir_org);
        pipeline_context_.ir = RV32_NOP;
    } else {
        pipeline_context_.ir = w_ir_tmp;
    }

    pipeline_context_.cinsn = w_compressed ? 1u : 0u;
    if (s_use_mix) e_instmix[decode_unit_.decodeOp(pipeline_context_.ir)]++;
}

/* ID_(Instruction Decode) stage, OK                                                      */
void Machine::ID_() {
    pipeline_context_.opcode = (pipeline_context_.ir >> 0) & 0x7F;
    pipeline_context_.rd = (pipeline_context_.ir >> 7) & 0x1f;
    pipeline_context_.rs1 = (pipeline_context_.ir >> 15) & 0x1f;
    pipeline_context_.rs2 = (pipeline_context_.ir >> 20) & 0x1f;
    pipeline_context_.funct3 = (pipeline_context_.ir >> 12) & 0x7;
    pipeline_context_.funct5 = (pipeline_context_.ir >> 27) & 0x1F;
    pipeline_context_.funct7 = (pipeline_context_.ir >> 25);
    pipeline_context_.funct12 = (pipeline_context_.ir >> 20);
    pipeline_context_.imm = decode_unit_.immGen(pipeline_context_.ir);
}

/* OF_(Operand Fetch) stage                                                               */
void Machine::OF_() {
    const Funct3 funct3 = static_cast<Funct3>(pipeline_context_.funct3);
    const Instruction funct12 = pipeline_context_.funct12;
    CSRAddress w_csr_addr =
        (funct3 != Funct3::Priv) ? static_cast<CSRAddress>(funct12)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Ecall)) ? csr_addr(Csr::Mtvec)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Uret))  ? csr_addr(Csr::Uepc)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Sret))  ? csr_addr(Csr::Sepc)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Mret))  ? csr_addr(Csr::Mepc)
                                                                    : 0;
    pipeline_context_.rrs1 = cpu->reg[pipeline_context_.rs1]; /* regfile read port 1 */
    pipeline_context_.rrs2 = cpu->reg[pipeline_context_.rs2]; /* regfile read port 2 */
    pipeline_context_.rcsr = cpu->read_csr(w_csr_addr);       /* note !! */
}

/* EX1(Execution 1) stage                                                                 */
void Machine::EX1() {
    pipeline_context_.fp_wb_enable = 0;
    pipeline_context_.int_wb_from_fp = 0;

    switch (static_cast<Opcode>(pipeline_context_.opcode)) {
        case Opcode::Lui: {
            pipeline_context_.tkn = 0;
            pipeline_context_.wb_data = pipeline_context_.imm << 12;
            break;
        }
        case Opcode::Auipc: {
            pipeline_context_.tkn = 0;
            pipeline_context_.wb_data = cpu->pc + (pipeline_context_.imm << 12);
            break;
        }
        case Opcode::Jal: {
            pipeline_context_.tkn = 1;
            pipeline_context_.wb_data = cpu->pc + (pipeline_context_.cinsn ? 2 : 4);
            pipeline_context_.jmp_pc = cpu->pc + pipeline_context_.imm;
            break;
        }
        case Opcode::Jalr: {
            pipeline_context_.tkn = 1;
            pipeline_context_.wb_data = cpu->pc + (pipeline_context_.cinsn ? 2 : 4);
            pipeline_context_.jmp_pc = pipeline_context_.rrs1 + pipeline_context_.imm;
            break;
        }
        case Opcode::Op: {
            pipeline_context_.tkn = 0;
            pipeline_context_.wb_data =
                execute_unit_.aluInt(pipeline_context_.rrs1, pipeline_context_.rrs2,
                                     pipeline_context_.funct3, pipeline_context_.funct7);
            break;
        }
        case Opcode::Load: {
            pipeline_context_.tkn = 0;
            pipeline_context_.mem_addr = pipeline_context_.rrs1 + pipeline_context_.imm;
            break;
        }
        case Opcode::LoadFp: {
            pipeline_context_.tkn = 0;
            pipeline_context_.mem_addr = pipeline_context_.rrs1 + pipeline_context_.imm;
            break;
        }
        case Opcode::Store: {
            pipeline_context_.tkn = 0;
            pipeline_context_.mem_addr = pipeline_context_.rrs1 + pipeline_context_.imm;
            break;
        }
        case Opcode::StoreFp: {
            pipeline_context_.tkn = 0;
            pipeline_context_.mem_addr = pipeline_context_.rrs1 + pipeline_context_.imm;
            pipeline_context_.fp_mem_wdata = cpu->freg[pipeline_context_.rs2];
            break;
        }
        case Opcode::MiscMem: {
            pipeline_context_.tkn = 0;
            break;
        }
        case Opcode::Branch: {
            pipeline_context_.tkn = execute_unit_.branchTaken(
                pipeline_context_.rrs1, pipeline_context_.rrs2, pipeline_context_.funct3);
            pipeline_context_.jmp_pc = cpu->pc + pipeline_context_.imm;
            break;
        }
        case Opcode::OpImm: {
            pipeline_context_.tkn = 0;
            pipeline_context_.funct7 &=
                (static_cast<Funct3>(pipeline_context_.funct3) == Funct3::Add) ? 0 : 0x20;
            pipeline_context_.wb_data =
                execute_unit_.aluInt(pipeline_context_.rrs1, pipeline_context_.imm,
                                     pipeline_context_.funct3, pipeline_context_.funct7);
            break;
        }
        case Opcode::Amo: {
            pipeline_context_.tkn = 0;
            pipeline_context_.mem_addr = pipeline_context_.rrs1;
            if (static_cast<Funct5Amo>(pipeline_context_.funct5) == Funct5Amo::Sc) {
                pipeline_context_.wb_data =
                    !((pipeline_context_.rrs1 == cpu->load_res) && cpu->reserved);
            }
            break;
        }
        case Opcode::System: {
            if (static_cast<Funct3>(pipeline_context_.funct3) == Funct3::Priv) {
                switch (static_cast<Funct12Priv>(pipeline_context_.funct12)) {
                    case Funct12Priv::Ecall: {
                        pipeline_context_.wb_data_csr =
                            enum_mask(ExceptionCode::UserEcall) + cpu->priv;
                        cpu->pending_exception = enum_mask(ExceptionCode::UserEcall) + cpu->priv;
                        e_icount++;
                        break;
                    }
                    case Funct12Priv::Ebreak: {
                        pipeline_context_.tkn = 0;
                        break;
                    }
                    case Funct12Priv::Uret: {
                        pipeline_context_.tkn = 1;
                        pipeline_context_.jmp_pc = pipeline_context_.rcsr;
                        break;
                    }
                    case Funct12Priv::Sret: {
                        pipeline_context_.tkn = 1;
                        pipeline_context_.jmp_pc = pipeline_context_.rcsr;
                        break;
                    }
                    case Funct12Priv::Mret: {
                        pipeline_context_.tkn = 1;
                        pipeline_context_.jmp_pc = pipeline_context_.rcsr;
                        break;
                    }
                    case Funct12Priv::Wfi: {
                        pipeline_context_.tkn = 0;
                        break;
                    }
                    default: {
                        if (pipeline_context_.funct7 ==
                            static_cast<Instruction>(Funct7Priv::SfenceVma)) {
                            pipeline_context_.tkn = 0;
                        }
                        break;
                    }
                }
            } else {
                pipeline_context_.tkn = 0;
                pipeline_context_.wb_data_csr =
                    execute_unit_.csrWriteValue(pipeline_context_.rcsr, pipeline_context_.rrs1,
                                                pipeline_context_.imm, pipeline_context_.funct3);
            }
            break;
        }
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMAdd:
        case Opcode::NMSub: {
            pipeline_context_.tkn = 0;
            const Word fmt = (pipeline_context_.ir >> 25) & 0x3;
            const Word rs3 = (pipeline_context_.ir >> 27) & 0x1f;
            const FpExecResult fp = execute_unit_.fusedFp(
                static_cast<Opcode>(pipeline_context_.opcode), fmt, pipeline_context_.rs1,
                pipeline_context_.rs2, rs3, pipeline_context_.funct3, cpu->freg, cpu->fcsr);
            pipeline_context_.fp_wb_data = fp.fp_wb_data;
            pipeline_context_.fp_wb_enable = fp.fp_wb_enable;
            break;
        }
        case Opcode::OpFp: {
            pipeline_context_.tkn = 0;
            const FpExecResult fp = execute_unit_.opFp(
                pipeline_context_.funct7, pipeline_context_.funct3, pipeline_context_.rs2,
                pipeline_context_.rs1, pipeline_context_.rs2, pipeline_context_.rrs1, cpu->freg,
                cpu->fcsr);
            pipeline_context_.wb_data = fp.int_wb_data;
            pipeline_context_.int_wb_from_fp = fp.int_wb_enable;
            pipeline_context_.fp_wb_data = fp.fp_wb_data;
            pipeline_context_.fp_wb_enable = fp.fp_wb_enable;
            break;
        }
        default: {
            pipeline_context_.tkn = 0;
            break;
        }
    }
}
/* LD_(Load Data) stage                                                                   */
void Machine::LD_() {
    if (cpu->pending_exception != ~0u) return;

    const Opcode opcode = static_cast<Opcode>(pipeline_context_.opcode);
    const Funct5Amo funct5 = static_cast<Funct5Amo>(pipeline_context_.funct5);

    if (opcode == Opcode::Load || (opcode == Opcode::Amo && funct5 != Funct5Amo::Sc)) {
        pipeline_context_.mem_rdata =
            target_read(pipeline_context_.mem_addr,
                        pipeline_context_.funct3);  //, cpu, mem, console, disk);
    }

    if (opcode == Opcode::LoadFp) {
        const Funct3 funct3 = static_cast<Funct3>(pipeline_context_.funct3);
        if (funct3 == Funct3::Flw) {
            const Word lo =
                target_read(pipeline_context_.mem_addr, static_cast<Instruction>(Funct3::Lw));
            pipeline_context_.fp_mem_rdata = 0xffffffff00000000ull | static_cast<uint64_t>(lo);
        } else if (funct3 == Funct3::Fld) {
            const Word lo =
                target_read(pipeline_context_.mem_addr, static_cast<Instruction>(Funct3::Lw));
            const Word hi =
                target_read(pipeline_context_.mem_addr + 4, static_cast<Instruction>(Funct3::Lw));
            pipeline_context_.fp_mem_rdata =
                static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
        }
    }

    if (opcode == Opcode::Amo && funct5 == Funct5Amo::Lr) {
        cpu->load_res = pipeline_context_.mem_addr;
        cpu->reserved = 1;
    }
}

/* EX2(Execution 2) stage                                                                 */
void Machine::EX2() {
    const Opcode opcode = static_cast<Opcode>(pipeline_context_.opcode);
    pipeline_context_.mem_wdata =
        (opcode != Opcode::Amo)
            ? pipeline_context_.rrs2
            : execute_unit_.aluAmo(pipeline_context_.rrs2, pipeline_context_.mem_rdata,
                                   pipeline_context_.funct5);

    if (opcode == Opcode::StoreFp) {
        pipeline_context_.mem_wdata =
            static_cast<Register>(pipeline_context_.fp_mem_wdata & 0xffffffffu);
    }
}

/* SD_(Store Data) stage                                                                  */
void Machine::SD_() {
    if (cpu->pending_exception != ~0u) return;

    const Opcode opcode = static_cast<Opcode>(pipeline_context_.opcode);
    const Funct5Amo funct5 = static_cast<Funct5Amo>(pipeline_context_.funct5);

    if ((opcode == Opcode::Store) ||
        (opcode == Opcode::Amo &&
         (funct5 == Funct5Amo::Sc && !pipeline_context_.wb_data && cpu->reserved)) ||
        (opcode == Opcode::Amo && funct5 != Funct5Amo::Lr && funct5 != Funct5Amo::Sc)) {
        target_write(pipeline_context_.mem_addr, pipeline_context_.mem_wdata,
                     pipeline_context_.funct3);
    }
    if (opcode == Opcode::Amo && (funct5 == Funct5Amo::Sc && !pipeline_context_.wb_data &&
                                  cpu->reserved && cpu->pending_exception == ~0u)) {
        cpu->reserved = 0;
    }

    if (opcode == Opcode::StoreFp) {
        const Funct3 funct3 = static_cast<Funct3>(pipeline_context_.funct3);
        if (funct3 == Funct3::Fsw) {
            target_write(pipeline_context_.mem_addr,
                         static_cast<Word>(pipeline_context_.fp_mem_wdata & 0xffffffffu),
                         static_cast<Instruction>(Funct3::Sw));
        } else if (funct3 == Funct3::Fsd) {
            target_write(pipeline_context_.mem_addr,
                         static_cast<Word>(pipeline_context_.fp_mem_wdata & 0xffffffffu),
                         static_cast<Instruction>(Funct3::Sw));
            target_write(pipeline_context_.mem_addr + 4,
                         static_cast<Word>((pipeline_context_.fp_mem_wdata >> 32) & 0xffffffffu),
                         static_cast<Instruction>(Funct3::Sw));
        }
    }
}

/* WB_(Write Back) stage                                                                  */
int hogehoge = 0;
void Machine::WB_() {
    if (cpu->pending_exception != ~0u) return;

    e_icount++;

    Word wire_wb_r_data = 0;
    Word wire_wb_r_enable = 0;

    const Opcode opcode = static_cast<Opcode>(pipeline_context_.opcode);
    const Funct5Amo funct5 = static_cast<Funct5Amo>(pipeline_context_.funct5);
    const Funct3 funct3 = static_cast<Funct3>(pipeline_context_.funct3);

    if ((opcode == Opcode::Load) || (opcode == Opcode::Amo && funct5 != Funct5Amo::Sc)) {
        wire_wb_r_data = pipeline_context_.mem_rdata;
        wire_wb_r_enable = 1;
    } else if (opcode == Opcode::System && funct3 != Funct3::Priv) {
        wire_wb_r_data = pipeline_context_.rcsr;
        wire_wb_r_enable = 1;
    } else {
        if ((opcode == Opcode::Amo && funct5 == Funct5Amo::Sc) || (opcode == Opcode::Lui) ||
            (opcode == Opcode::Auipc) || (opcode == Opcode::Jal) || (opcode == Opcode::Jalr) ||
            (opcode == Opcode::Op) || (opcode == Opcode::OpImm) ||
            (opcode == Opcode::OpFp && pipeline_context_.int_wb_from_fp)) {
            wire_wb_r_data = pipeline_context_.wb_data;
            wire_wb_r_enable = 1;
        }
    }

    if (opcode == Opcode::LoadFp) {
        cpu->freg[pipeline_context_.rd] = pipeline_context_.fp_mem_rdata;
    }

    if ((opcode == Opcode::OpFp || opcode == Opcode::MAdd || opcode == Opcode::MSub ||
         opcode == Opcode::NMAdd || opcode == Opcode::NMSub) &&
        pipeline_context_.fp_wb_enable) {
        cpu->freg[pipeline_context_.rd] = pipeline_context_.fp_wb_data;
    }

    if (wire_wb_r_enable && pipeline_context_.rd != 0) {
        cpu->reg[pipeline_context_.rd] = wire_wb_r_data; /* regifile write port 1 */
    }
}

/* COM(Complete) stage                                                                    */
void Machine::COM() {
    if (pipeline_context_.cinsn) e_ccount++; /** for evaluation **/

    const Opcode opcode = static_cast<Opcode>(pipeline_context_.opcode);
    const Funct3 funct3 = static_cast<Funct3>(pipeline_context_.funct3);

    if (opcode == Opcode::System) {
        if (funct3 == Funct3::Priv) {
            switch (static_cast<Funct12Priv>(pipeline_context_.funct12)) {
                case Funct12Priv::Uret: {
                    break;
                }
                case Funct12Priv::Sret: {
                    cpu->sret();
                    break;
                }
                case Funct12Priv::Mret: {
                    cpu->mret();
                    break;
                }
                default:
                    break;
            }
        } else {
            cpu->write_csr(static_cast<CSRAddress>(pipeline_context_.funct12),
                           pipeline_context_.wb_data_csr);
        }
    }

    Word pending_interrupts = cpu->mip & cpu->mie;
    Word enable_interrupts = 0, mask = 0, irq_num = 32;
    if (pending_interrupts) {
        switch (cpu->priv) {
            case kPrivMachine: {
                if (cpu->mstatus & enum_mask(MstatusBit::Mie)) {
                    enable_interrupts = ~cpu->mideleg;
                }
                break;
            }
            case kPrivSupervisor: {
                enable_interrupts = ~cpu->mideleg;
                if (cpu->mstatus & enum_mask(MstatusBit::Sie)) {
                    enable_interrupts |= cpu->mideleg;
                }
                break;
            }
            case kPrivUser: {
                enable_interrupts = ~0;
                break;
            }
        }
        mask = pending_interrupts & enable_interrupts;
        for (int i = 0; i < 32; i++) {
            if ((1 << i) & mask) {
                irq_num = i;
                break;
            }
        }
    }
    if (cpu->pending_exception != ~0u) {
        cpu->raise_exception(cpu->pending_exception, cpu->pending_tval);
    } else {
        if (pipeline_context_.tkn) {
            cpu->pc = pipeline_context_.jmp_pc;
        } else {
            cpu->pc = cpu->pc + (pipeline_context_.cinsn ? 2 : 4);
        }
        if (mask != 0) {
            cpu->raise_exception(kInterruptCauseBit | irq_num, cpu->pending_tval);
            cpu->pending_exception = kInterruptCauseBit | irq_num;
        }
    }
}

void Machine::trace_output() {
    fprintf(s_fp_trace, "%08ld ", cpu->mtime);
    //    fprintf(s_fp_trace, "%08ld ", e_icount);
    fprintf(s_fp_trace, "%08x ", pipeline_context_.cpc);
    fprintf(s_fp_trace, "%08x", pipeline_context_.ir);
    if (s_rtosmode) {
        fprintf(s_fp_trace, " %08ld", cpu->mtimecmp);
    }
    fprintf(s_fp_trace, "\n");

    for (int i = 0; i < 4; i++) { /* output registers */
        for (int j = 0; j < 8; j++) {
            fprintf(s_fp_trace, "%08x", cpu->reg[i * 8 + j]);
            fprintf(s_fp_trace, "%s", (j != 7 ? " " : "\n"));
        }
    }

    if (!s_appmode) {
        fprintf(s_fp_trace, "%08x ", cpu->mstatus);
        fprintf(s_fp_trace, "%08x ", cpu->mtvec);
        fprintf(s_fp_trace, "%08x ", cpu->mscratch);
        fprintf(s_fp_trace, "%08x ", cpu->mepc);
        fprintf(s_fp_trace, "%08x ", cpu->mcause);
        fprintf(s_fp_trace, "%08x ", cpu->mtval);
        fprintf(s_fp_trace, "%08x ", cpu->mhartid);
        fprintf(s_fp_trace, "%08x ", cpu->misa);
        fprintf(s_fp_trace, "\n");

        fprintf(s_fp_trace, "%08x ", cpu->mie);
        fprintf(s_fp_trace, "%08x ", cpu->mip);
        fprintf(s_fp_trace, "%08x ", cpu->medeleg);
        fprintf(s_fp_trace, "%08x ", cpu->mideleg);
        fprintf(s_fp_trace, "%08x ", cpu->mcounteren);
        if (!s_rtosmode) {
            fprintf(s_fp_trace, "%08x ", cpu->stvec);
            fprintf(s_fp_trace, "%08x ", cpu->sscratch);
            fprintf(s_fp_trace, "%08x ", cpu->sepc);
            fprintf(s_fp_trace, "\n");

            fprintf(s_fp_trace, "%08x ", cpu->scause);
            fprintf(s_fp_trace, "%08x ", cpu->stval);
            fprintf(s_fp_trace, "%08x ", cpu->satp);
            fprintf(s_fp_trace, "%08x ", cpu->scounteren);
            fprintf(s_fp_trace, "%08x ", cpu->load_res);
        }
        fprintf(s_fp_trace, "%08x ", cpu->pending_exception);
        fprintf(s_fp_trace, "%08x ", cpu->pending_tval);
        fprintf(s_fp_trace, "%08x ", cpu->priv);
        fprintf(s_fp_trace, "\n");

        if (!s_rtosmode) {
            for (int i = 0; i < 4; i++) {
                fprintf(s_fp_trace, "%08x ", cpu->TLB_inst_r[i].v_addr);
                fprintf(s_fp_trace, "%08x ", cpu->TLB_inst_r[i].p_addr);
            }
            fprintf(s_fp_trace, "\n");
            for (int i = 0; i < 4; i++) {
                fprintf(s_fp_trace, "%08x ", cpu->TLB_data_r[i].v_addr);
                fprintf(s_fp_trace, "%08x ", cpu->TLB_data_r[i].p_addr);
            }
            fprintf(s_fp_trace, "\n");
            for (int i = 0; i < 4; i++) {
                fprintf(s_fp_trace, "%08x ", cpu->TLB_data_w[i].v_addr);
                fprintf(s_fp_trace, "%08x ", cpu->TLB_data_w[i].p_addr);
            }
            fprintf(s_fp_trace, "\n");
        }
    }
}

void load_devicetree(Byte* ram) {
    Word tbuf[344] = {
        0xedfe0dd0, 0x5a050000, 0x38000000, 0x84040000, 0x28000000, 0x11000000, 0x10000000,
        0x0,        0xd6000000, 0x4c040000, 0x0,        0x0,        0x0,        0x0,
        0x1000000,  0x0,        0x3000000,  0x4000000,  0x0,        0x2000000,  0x3000000,
        0x4000000,  0xf000000,  0x2000000,  0x3000000,  0xf000000,  0x1b000000, 0x6573696b,
        0x62616c2d, 0x6d69732c, 0x7672,     0x3000000,  0xf000000,  0x21000000, 0x6573696b,
        0x62616c5f, 0x6d69732c, 0x7672,     0x1000000,  0x73757063, 0x0,        0x3000000,
        0x4000000,  0x0,        0x1000000,  0x3000000,  0x4000000,  0xf000000,  0x0,
        0x3000000,  0x4000000,  0x2c000000, 0xe1f505,   0x1000000,  0x40757063, 0x30,
        0x3000000,  0x4000000,  0x3f000000, 0x757063,   0x3000000,  0x4000000,  0x4b000000,
        0x0,        0x3000000,  0x5000000,  0x4f000000, 0x79616b6f, 0x0,        0x3000000,
        0x6000000,  0x21000000, 0x63736972, 0x76,       0x3000000,  0x9000000,  0x56000000,
        0x32337672, 0x6d696361, 0x0,        0x3000000,  0xb000000,  0x60000000, 0x63736972,
        0x76732c76, 0x3233,     0x3000000,  0x4000000,  0x69000000, 0xe1f505,   0x1000000,
        0x65746e69, 0x70757272, 0x6f632d74, 0x6f72746e, 0x72656c6c, 0x0,        0x3000000,
        0x4000000,  0x79000000, 0x1000000,  0x3000000,  0x0,        0x8a000000, 0x3000000,
        0xf000000,  0x21000000, 0x63736972, 0x70632c76, 0x6e692d75, 0x6374,     0x3000000,
        0x4000000,  0x9f000000, 0x1000000,  0x2000000,  0x2000000,  0x2000000,  0x1000000,
        0x6f6d656d, 0x38407972, 0x30303030, 0x303030,   0x3000000,  0x7000000,  0x3f000000,
        0x6f6d656d, 0x7972,     0x3000000,  0x10000000, 0x4b000000, 0x0,        0x80,
        0x0,        0x4,        0x2000000,  0x1000000,  0x636f73,   0x3000000,  0x4000000,
        0x0,        0x2000000,  0x3000000,  0x4000000,  0xf000000,  0x2000000,  0x3000000,
        0xb000000,  0x21000000, 0x706d6973, 0x622d656c, 0x7375,     0x3000000,  0x0,
        0xa7000000, 0x1000000,  0x6e696c63, 0x30364074, 0x30303030, 0x3030,     0x3000000,
        0xd000000,  0x21000000, 0x63736972, 0x6c632c76, 0x30746e69, 0x0,        0x3000000,
        0x10000000, 0xae000000, 0x1000000,  0x3000000,  0x1000000,  0x7000000,  0x3000000,
        0x10000000, 0x4b000000, 0x0,        0x60,       0x0,        0x8,        0x2000000,
        0x1000000,  0x63696c70, 0x30303540, 0x30303030, 0x30,       0x3000000,  0x4000000,
        0x79000000, 0x1000000,  0x3000000,  0x0,        0x8a000000, 0x3000000,  0xc000000,
        0x21000000, 0x63736972, 0x6c702c76, 0x306369,   0x3000000,  0x4000000,  0xc2000000,
        0x1f000000, 0x3000000,  0x10000000, 0x4b000000, 0x0,        0x50,       0x0,
        0x8,        0x3000000,  0x10000000, 0xae000000, 0x1000000,  0x9000000,  0x1000000,
        0xb000000,  0x3000000,  0x4000000,  0x9f000000, 0x2000000,  0x2000000,  0x1000000,
        0x74726976, 0x34406f69, 0x30303030, 0x303030,   0x3000000,  0xc000000,  0x21000000,
        0x74726976, 0x6d2c6f69, 0x6f696d,   0x3000000,  0x10000000, 0x4b000000, 0x0,
        0x40,       0x0,        0x8,        0x3000000,  0x8000000,  0xae000000, 0x2000000,
        0x1000000,  0x2000000,  0x1000000,  0x74726976, 0x34406f69, 0x30303038, 0x303030,
        0x3000000,  0xc000000,  0x21000000, 0x74726976, 0x6d2c6f69, 0x6f696d,   0x3000000,
        0x10000000, 0x4b000000, 0x0,        0x48,       0x0,        0x8,        0x3000000,
        0x8000000,  0xae000000, 0x2000000,  0x2000000,  0x2000000,  0x2000000,  0x1000000,
        0x736f6863, 0x6e65,     0x3000000,  0x1e000000, 0xcd000000, 0x736e6f63, 0x3d656c6f,
        0x30637668, 0x6f6f7220, 0x642f3d74, 0x762f7665, 0x72206164, 0x77,       0x2000000,
        0x2000000,  0x9000000,  0x64646123, 0x73736572, 0x6c65632d, 0x2300736c, 0x657a6973,
        0x6c65632d, 0x6d00736c, 0x6c65646f, 0x6d6f6300, 0x69746170, 0x656c62,   0x656d6974,
        0x65736162, 0x6572662d, 0x6e657571, 0x64007963, 0x63697665, 0x79745f65, 0x72006570,
        0x73006765, 0x75746174, 0x69720073, 0x2c766373, 0x617369,   0x2d756d6d, 0x65707974,
        0x6f6c6300, 0x662d6b63, 0x75716572, 0x79636e65, 0x6e692300, 0x72726574, 0x2d747075,
        0x6c6c6563, 0x6e690073, 0x72726574, 0x2d747075, 0x746e6f63, 0x6c6c6f72, 0x70007265,
        0x646e6168, 0x7200656c, 0x65676e61, 0x6e690073, 0x72726574, 0x73747075, 0x7478652d,
        0x65646e65, 0x69720064, 0x2c766373, 0x7665646e, 0x6f6f6200, 0x67726174, 0xff0073,
        0x0};
    int* p = reinterpret_cast<int*>(ram);
    for (int i = 0; i < 344; i++) p[i] = tbuf[i];

    if (0) {
        char* cp = reinterpret_cast<char*>(p);
        for (int i = 0; i < 344 * 4; i++) printf("%02x ", cp[i] & 0xff);
        printf("\n");
        exit(0);
    }
}

/* Initialize                                                                             */
int Machine::init(int argc, char* argv[]) {
    set_options(this, argc, argv); /* set options before the object instantiations */

    cpu = new CPU();
    disk = new Disk();
    console = new Console();
    mmem = console->mmem = disk->mmem = new Byte[simrv::memory::kDramSize];

    // MAKE Console QUEUE
    console->Queue = new QueueState[simrv::virtio::kConsoleMaxQueueNum];
    disk->Queue = new QueueState[simrv::virtio::kDiskMaxQueueNum];
    // disk->sector = new Byte[simrv::virtio::kDiskSize];

    if (s_dlog_mode) s_fp_dlog.open("init_virtio.txt");

    cpu->pc = s_start_pc;
    cpu->reg[11] = (s_appmode | s_rtosmode) ? 0 : simrv::boot::kInitDataAddress + simrv::boot::kStartPc;
    cpu->TLB_flush();

    load_initram(s_fn_memimg, mmem);  // load a memory image file

    if (s_fn_dvtree == NULL)
        load_devicetree(mmem + simrv::boot::kInitDataAddress);
    else
        load_initram(s_fn_dvtree, mmem + simrv::boot::kInitDataAddress);

    if (s_use_disk) load_initram(s_fn_dskimg, disk->sector);  // load a disk image file

    if (s_use_mix)
        for (int i = 0; i < OperationIdCount; i++) e_instmix[i] = 0;

    if (s_rtosmode) cpu->misa = misa_profile_bits(MisaProfile::I);

    // #ifdef MIDDLE
    // #include "xinitreg.txt"
    // load_initmem("xinitmem.bin", mmem);
    // disk->load_file("xinitdisk.bin", s_appmode);
    // #endif
    return 0;
}

Machine::~Machine() {
    delete cpu;
    delete disk;
    delete console;
}

void Microcn::init(char* fname) {
    cmem = new Byte[simrv::memory::kLocalCoreMemorySize];
    load_initram(fname, cmem);
    for (int i = 0; i < 32; i++) reg[i] = 0;
    reg[11] = 0x8000; /* simrv::boot::kInitDataAddress + simrv::boot::kStartPc; */
}

int Microcn::exec() {
    DecodeUnit decode_unit;
    ExecuteUnit execute_unit;
    int ret = 1;
    cpc = pc;
    memcpy(&r_ir, &cmem[pc & simrv::memory::kDramMask], 4);
    if ((r_ir & 3) != 3) {
        printf("__ ERROR: this microcn does not support compressed insn!\n");
        exit(0);
    }
    r_opcode = (r_ir >> 0) & 0x7F;
    r_rd = (r_ir >> 7) & 0x1f;
    r_rs1 = (r_ir >> 15) & 0x1f;
    r_rs2 = (r_ir >> 20) & 0x1f;
    r_funct3 = (r_ir >> 12) & 0x7;
    r_funct5 = (r_ir >> 27) & 0x1F;
    r_funct7 = (r_ir >> 25);
    r_funct12 = (r_ir >> 20);
    r_imm = decode_unit.immGen(r_ir);
    r_rrs1 = reg[r_rs1]; /* regfile read port 1 */
    r_rrs2 = reg[r_rs2]; /* regfile read port 2 */
    switch (r_opcode) {
        case static_cast<Instruction>(Opcode::Lui): {
            r_tkn = 0;
            r_wb_data = r_imm << 12;
            break;
        }
        case static_cast<Instruction>(Opcode::Auipc): {
            r_tkn = 0;
            r_wb_data = pc + (r_imm << 12);
            break;
        }
        case static_cast<Instruction>(Opcode::Jal): {
            r_tkn = 1;
            r_wb_data = pc + 4;
            r_jmp_pc = pc + r_imm;
            break;
        }
        case static_cast<Instruction>(Opcode::Jalr): {
            r_tkn = 1;
            r_wb_data = pc + 4;
            r_jmp_pc = r_rrs1 + r_imm;
            break;
        }
        case static_cast<Instruction>(Opcode::Op): {
            r_tkn = 0;
            r_wb_data = execute_unit.aluInt(r_rrs1, r_rrs2, r_funct3, r_funct7);
            break;
        }
        case static_cast<Instruction>(Opcode::Load): {
            r_tkn = 0;
            r_mem_addr = r_rrs1 + r_imm;
            break;
        }
        case static_cast<Instruction>(Opcode::Store): {
            r_tkn = 0;
            r_mem_addr = r_rrs1 + r_imm;
            break;
        }
        case static_cast<Instruction>(Opcode::MiscMem): {
            r_tkn = 0;
            break;
        }
        case static_cast<Instruction>(Opcode::Branch): {
            r_tkn = execute_unit.branchTaken(r_rrs1, r_rrs2, r_funct3);
            r_jmp_pc = pc + r_imm;
            break;
        }
        case static_cast<Instruction>(Opcode::OpImm): {
            r_tkn = 0;
            r_funct7 &= (r_funct3 == static_cast<Instruction>(Funct3::Add)) ? 0 : 0x20;
            r_wb_data = execute_unit.aluInt(r_rrs1, r_imm, r_funct3, r_funct7);
            break;
        }
    }
    int tmp = (1 << (r_funct3 & 0x3));
    if (r_opcode == static_cast<Instruction>(Opcode::Load)) {
        if ((r_mem_addr >> 28) == 0x8) {
            r_mem_rdata = ram_read(r_mem_addr & simrv::memory::kDramMask, r_funct3, mmem);
        } else if ((r_mem_addr >> 28) == 0x9) {
            r_mem_rdata = disk_read(r_mem_addr & DISK_MASK, tmp, disk);
        } else if (r_mem_addr == 0x40009000) {
            r_mem_rdata = Mode;
        } else if (r_mem_addr == 0x40009004) {
            r_mem_rdata = Qnum;
        } else if (r_mem_addr == 0x40009008) {
            r_mem_rdata = Qsel;
        } else if ((r_mem_addr >> 12) == 0x4000a) {
            r_mem_rdata = queue_read(r_mem_addr & 0xff, cons_queue);
        } else if ((r_mem_addr >> 12) == 0x4000b) {
            r_mem_rdata = queue_read(r_mem_addr & 0xff, disk_queue);
        } else if ((r_mem_addr >> 12) == 0x4000c) {
            r_mem_rdata = static_cast<Word>(std::to_integer<uint8_t>(cons_fifo));
        } else
            r_mem_rdata = ram_read(r_mem_addr & simrv::memory::kDramMask, r_funct3, cmem);
    }
    if (r_opcode == static_cast<Instruction>(Opcode::Store)) {
        if (r_mem_addr == 0x40008000) {
            if (r_rrs2 >> 16 == 1) {
                printf("%c", (char)(r_rrs2 & 0xff));
                fflush(stdout);
            }
            if (r_rrs2 >> 16 == 2) {
                ret = 0;
            }
            //            if(r_rrs2>>16==2){ printf("\n__ Power off\n"); ret=0;}
            //            //exit(0);}//ret=0;}
        } else if ((r_mem_addr >> 28) == 0x8) {
            for (int i = 0; i < (1 << r_funct3); i++) {
                mmem[(r_mem_addr + i) & simrv::memory::kDramMask] =
                    static_cast<Byte>(static_cast<uint8_t>((r_rrs2 >> (8 * i)) & 0xFF));
            }
        } else if ((r_mem_addr >> 28) == 0x9) {
            Word* dsk_tmp = reinterpret_cast<Word*>(disk);
            dsk_tmp[(r_mem_addr & DISK_MASK) / 4] = r_rrs2;

            /*for (int i=0; i<(1 << r_funct3); i++) {
                disk[(r_mem_addr+i) & DISK_MASK] =
                    static_cast<Byte>(static_cast<uint8_t>((r_rrs2 >> (8*i)) & 0xFF));
            }*/
        } else if ((r_mem_addr >> 12) == 0x4000a) {
            queue_write(r_mem_addr & 0xff, r_rrs2, cons_queue);
        } else if ((r_mem_addr >> 12) == 0x4000b) {
            queue_write(r_mem_addr & 0xff, r_rrs2, disk_queue);
        } else {
            for (int i = 0; i < (1 << r_funct3); i++) {
                cmem[(r_mem_addr + i) & simrv::memory::kDramMask] =
                    static_cast<Byte>(static_cast<uint8_t>((r_rrs2 >> (8 * i)) & 0xFF));
            }
        }
    }
    Word wire_wb_r_data = 0;
    Word wire_wb_r_enable = 0;
    if (r_opcode == static_cast<Instruction>(Opcode::Load)) {
        wire_wb_r_data = r_mem_rdata;
        wire_wb_r_enable = 1;
    } else if ((r_opcode == static_cast<Instruction>(Opcode::Lui)) ||
               (r_opcode == static_cast<Instruction>(Opcode::Auipc)) ||
               (r_opcode == static_cast<Instruction>(Opcode::Jal)) ||
               (r_opcode == static_cast<Instruction>(Opcode::Jalr)) ||
               (r_opcode == static_cast<Instruction>(Opcode::Op)) ||
               (r_opcode == static_cast<Instruction>(Opcode::OpImm))) {
        wire_wb_r_data = r_wb_data;
        wire_wb_r_enable = 1;
    }

    if (wire_wb_r_enable && r_rd != 0) reg[r_rd] = wire_wb_r_data;
    pc = (r_tkn) ? r_jmp_pc : pc + 4;

    mm.e_uc_cnt++;
    return ret;
}
