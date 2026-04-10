/**
 * @file Machine.cpp
 * @brief SimRV implementation unit.
 */
#include "Machine.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <memory>
#include <string>
#include <utility>

#include "Module.hpp"
void set_options(Machine* m, int argc, char* argv[]);

Machine::Machine()
    : micro_controller(std::make_unique<Microcn>()), mmio_router_(*this), memory_(*this) {
    micro_controller->owner = this;
}

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

using QueueField = Word QueueState::*;

constexpr std::array<std::pair<Address, QueueField>, 9> kQueueFields = {{
    {D_QUEUE_READY_OFFSET, &QueueState::Ready},
    {D_QUEUE_NOTIFY_OFFSET, &QueueState::Notify},
    {D_QUEUE_DESC_LOW_OFFSET, &QueueState::DescLow},
    {D_QUEUE_DESC_HIGH_OFFSET, &QueueState::DescHigh},
    {D_QUEUE_AVAIL_LOW_OFFSET, &QueueState::AvailLow},
    {D_QUEUE_AVAIL_HIGH_OFFSET, &QueueState::AvailHigh},
    {D_QUEUE_USED_LOW_OFFSET, &QueueState::UsedLow},
    {D_QUEUE_USED_HIGH_OFFSET, &QueueState::UsedHigh},
    {D_QUEUE_LAST_AVAIL_OFFSET, &QueueState::last_avail_idx},
}};

constexpr QueueField queue_field(Address offset) {
    for (const auto& [field_offset, field] : kQueueFields) {
        if (field_offset == offset) {
            return field;
        }
    }
    return nullptr;
}

constexpr Word pte_access_index(PteAccess access) { return std::to_underlying(access); }

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

void load_image_into_ram(const std::string& file_path, Byte* ram) {
    std::ifstream in(file_path, std::ios::binary);
    if (!in.is_open()) {
        fprintf(stdout, "__ Error: image_file %s cannot be found\n", file_path.c_str());
        exit(0);
    }
    uint8_t tmp = 0;
    int i = 0;
    while (in.read(reinterpret_cast<char*>(&tmp), sizeof(tmp))) {
        ram[i++] = static_cast<Byte>(tmp);
    }
}

void load_image_file_into_ram(std::string file, Byte* ram) {
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
    return simrv::memory_detail::ram_read_fast(addr, funct3, ram);
}

Word disk_read(Address addr, Word n, Byte* dsk) {
    if (n != 1 && n != 2 && n != 4) {
        printf("__ Error: dsk_r() not supported n=%d\n", n);
        exit(0);
    }
    Word data = 0;

    for (int i = 0; i < static_cast<int>(n); i++) {
        data |= static_cast<Word>(std::to_integer<uint8_t>(dsk[(addr + i)])) << (8 * i);
    }
    return data;
}

Word queue_read(Address addr, QueueState* q) {
    int idx = static_cast<int>(addr / D_QUEUE_STRIDE);
    if (QueueField field = queue_field(addr % D_QUEUE_STRIDE); field != nullptr) {
        return q[idx].*field;
    }
    return 0;
}

void queue_write(Address addr, Word wdata, QueueState* q) {
    int idx = static_cast<int>(addr / D_QUEUE_STRIDE);
    if (QueueField field = queue_field(addr % D_QUEUE_STRIDE); field != nullptr) {
        q[idx].*field = wdata;
    }
}

bool page_walk(Address v_addr, Address* p_addr, PteAccess access, CPU* cpu, Byte* mmem) {
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
    bool page_fault = false;
    if (!(L1_pte & enum_mask(PteFlag::V)))
        page_fault = true;
    else if (L1_xwr != 0)
        page_fault = !L1_success;
    else if (!(L0_pte & enum_mask(PteFlag::V)))
        page_fault = true;
    else if (L0_xwr != 0)
        page_fault = !L0_success;
    else
        page_fault = true;

    /* phys_addr */
    if (page_fault)
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
    bool we =
        ((L1_xwr != 0 && L1_success) && (L1_write)) || ((L0_xwr != 0 && L0_success) && (L0_write));
    Word w_addr = (L1_xwr != 0 && L1_success) ? L1_pte_addr : L0_pte_addr;
    Word w_data = (L1_xwr != 0 && L1_success) ? L1_pte_write : L0_pte_write;
    if (we) {
        for (int i = 0; i < 4; i++) {
            mmem[(w_addr + i) & simrv::memory::kDramMask] =
                static_cast<Byte>(static_cast<uint8_t>((w_data >> (8 * i)) & 0xFF));
        }
    }
    return page_fault;
}

}  // namespace simrv::machine_detail

using namespace simrv::machine_detail;

/**
 * @brief Emit snapshot artifacts for debug bring-up and RTL co-simulation.
 *
 * Generated files:
 * - `init_mem.txt`: DRAM image as one hex byte per line.
 * - `init_dsk.txt`: Disk image as one hex byte per line.
 * - `init_reg.txt`: Architectural state and MMIO queue state in assignment form.
 *
 * This helper is intentionally I/O-only and does not mutate simulator state.
 */
void dump_init_artifacts(CPU* cpu, Byte* ram, Console* console, Disk* disk, Byte* sector) {
    {
        std::ofstream out("init_mem.txt");
        for (Address i = 0; i < simrv::memory::kDramSize; ++i) {
            out << std::hex << static_cast<unsigned>(std::to_integer<uint8_t>(ram[i])) << '\n';
        }
        printf("__ file init_mem.txt was generated after %ld cycle\n", cpu->mtime);
    }

    {
        std::ofstream out("init_dsk.txt");
        for (Word i = 0; i < simrv::virtio::kDiskSize; ++i) {
            out << std::hex << static_cast<unsigned>(std::to_integer<uint8_t>(sector[i])) << '\n';
        }
        printf("__ file init_dsk.txt was generated after %ld cycle\n", cpu->mtime);
    }

    std::ofstream out("init_reg.txt");
    auto write_32 = [&out](std::string_view lhs, Word value) {
        out << lhs << "=32'h" << std::hex << std::setw(8) << std::setfill('0') << value << ";\n";
    };
    auto write_64 = [&out](std::string_view lhs, Counter value) {
        out << lhs << "=64'h" << std::hex << std::setw(16) << std::setfill('0') << value << ";\n";
    };

    write_32("p.pc", cpu->pc);
    for (int i = 1; i < 32; ++i) {
        out << "p.regs.mem[" << std::dec << i << "]=32'h" << std::hex << std::setw(8)
            << std::setfill('0') << cpu->reg[i] << ";\n";
    }
    write_32("p.mstatus     ", cpu->mstatus);
    write_32("p.mtvec       ", cpu->mtvec);
    write_32("p.mscratch    ", cpu->mscratch);
    write_32("p.mepc        ", cpu->mepc);
    write_32("p.mcause      ", cpu->mcause);
    write_32("p.mtval       ", cpu->mtval);
    write_32("p.mhartid     ", cpu->mhartid);
    write_32("p.misa        ", cpu->misa);
    write_32("p.mie         ", cpu->mie);
    write_32("p.mip         ", cpu->mip);
    write_32("p.medeleg     ", cpu->medeleg);
    write_32("p.mideleg     ", cpu->mideleg);
    write_32("p.mcounteren  ", cpu->mcounteren);
    write_32("p.stvec       ", cpu->stvec);
    write_32("p.sscratch    ", cpu->sscratch);
    write_32("p.sepc        ", cpu->sepc);
    write_32("p.scause      ", cpu->scause);
    write_32("p.stval       ", cpu->stval);
    write_32("p.satp        ", cpu->satp);
    write_32("p.scounteren  ", cpu->scounteren);
    write_32("p.priv        ", cpu->priv);

    write_64("p.mtime       ", cpu->mtime);
    write_64("p.mtimecmp    ", cpu->mtimecmp);

    write_32("p.load_res    ", cpu->load_res);
    out << "p.reserved    = 1'h" << std::hex << std::setw(1) << std::setfill('0') << cpu->reserved
        << ";\n";
    write_32("p.pending_exception   ", cpu->pending_exception);
    write_32("p.pending_tval", cpu->pending_tval);

    for (Word i = 0; i < simrv::memory::kTlbSize; ++i) {
        out << "mmu.TLB_inst_r.r_valid[" << std::dec << i
            << "] =" << static_cast<int>(cpu->TLB_inst_r[i].p_addr != static_cast<Address>(-1u))
            << ";\n";
        out << "mmu.TLB_inst_r.mem[" << std::dec << i << "][39:22] =18'h" << std::hex
            << std::setw(5) << std::setfill('0') << (cpu->TLB_inst_r[i].v_addr >> 14) << ";\n";
        out << "mmu.TLB_inst_r.mem[" << std::dec << i << "][21:0] =22'h" << std::hex << std::setw(6)
            << std::setfill('0') << (cpu->TLB_inst_r[i].p_addr >> 10) << ";\n";
    }
    for (Word i = 0; i < simrv::memory::kTlbSize; ++i) {
        out << "mmu.TLB_data_r.r_valid[" << std::dec << i
            << "] =" << static_cast<int>(cpu->TLB_data_r[i].p_addr != static_cast<Address>(-1u))
            << ";\n";
        out << "mmu.TLB_data_r.mem[" << std::dec << i << "][39:22] =18'h" << std::hex
            << std::setw(5) << std::setfill('0') << (cpu->TLB_data_r[i].v_addr >> 14) << ";\n";
        out << "mmu.TLB_data_r.mem[" << std::dec << i << "][21:0] =22'h" << std::hex << std::setw(6)
            << std::setfill('0') << (cpu->TLB_data_r[i].p_addr >> 10) << ";\n";
    }
    for (Word i = 0; i < simrv::memory::kTlbSize; ++i) {
        out << "mmu.TLB_data_w.r_valid[" << std::dec << i
            << "] =" << static_cast<int>(cpu->TLB_data_w[i].p_addr != static_cast<Address>(-1u))
            << ";\n";
        out << "mmu.TLB_data_w.mem[" << std::dec << i << "][39:22] =18'h" << std::hex
            << std::setw(5) << std::setfill('0') << (cpu->TLB_data_w[i].v_addr >> 14) << ";\n";
        out << "mmu.TLB_data_w.mem[" << std::dec << i << "][21:0] =22'h" << std::hex << std::setw(6)
            << std::setfill('0') << (cpu->TLB_data_w[i].p_addr >> 10) << ";\n";
    }

    write_32("mmu.console.QueueSel       ", console->QueueSel);
    write_32("mmu.console.QueueNum       ", console->QueueNum);
    for (Word i = 0; i < simrv::virtio::kConsoleMaxQueueNum; ++i) {
        out << "mmu.console.Queue[" << std::dec << i << "*9+0] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << console->Queue[i].Ready << ";\n";
        out << "mmu.console.Queue[" << std::dec << i << "*9+1] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << console->Queue[i].Notify << ";\n";
        out << "mmu.console.Queue[" << std::dec << i << "*9+2] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << console->Queue[i].DescLow << ";\n";
        out << "mmu.console.Queue[" << std::dec << i << "*9+3] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << console->Queue[i].DescHigh << ";\n";
        out << "mmu.console.Queue[" << std::dec << i << "*9+4] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << console->Queue[i].AvailLow << ";\n";
        out << "mmu.console.Queue[" << std::dec << i << "*9+5] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << console->Queue[i].AvailHigh << ";\n";
        out << "mmu.console.Queue[" << std::dec << i << "*9+6] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << console->Queue[i].UsedLow << ";\n";
        out << "mmu.console.Queue[" << std::dec << i << "*9+7] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << console->Queue[i].UsedHigh << ";\n";
        out << "mmu.console.Queue[" << std::dec << i << "*9+8] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << console->Queue[i].last_avail_idx << ";\n";
    }
    write_32("mmu.console.InterruptStatus", console->InterruptStatus);
    write_32("mmu.console.Status         ", console->Status);

    write_32("mmu.disk.QueueSel       ", disk->QueueSel);
    write_32("mmu.disk.QueueNum       ", disk->QueueNum);
    for (Word i = 0; i < simrv::virtio::kDiskMaxQueueNum; ++i) {
        out << "mmu.disk.Queue[" << std::dec << i << "*9+0] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << disk->Queue[i].Ready << ";\n";
        out << "mmu.disk.Queue[" << std::dec << i << "*9+1] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << disk->Queue[i].Notify << ";\n";
        out << "mmu.disk.Queue[" << std::dec << i << "*9+2] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << disk->Queue[i].DescLow << ";\n";
        out << "mmu.disk.Queue[" << std::dec << i << "*9+3] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << disk->Queue[i].DescHigh << ";\n";
        out << "mmu.disk.Queue[" << std::dec << i << "*9+4] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << disk->Queue[i].AvailLow << ";\n";
        out << "mmu.disk.Queue[" << std::dec << i << "*9+5] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << disk->Queue[i].AvailHigh << ";\n";
        out << "mmu.disk.Queue[" << std::dec << i << "*9+6] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << disk->Queue[i].UsedLow << ";\n";
        out << "mmu.disk.Queue[" << std::dec << i << "*9+7] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << disk->Queue[i].UsedHigh << ";\n";
        out << "mmu.disk.Queue[" << std::dec << i << "*9+8] =32'h" << std::hex << std::setw(8)
            << std::setfill('0') << disk->Queue[i].last_avail_idx << ";\n";
    }
    write_32("mmu.disk.InterruptStatus", disk->InterruptStatus);
    write_32("mmu.disk.Status         ", disk->Status);

    printf("__ file init_reg.txt was generated after %ld cycle\n", cpu->mtime);
}

/**
 * @brief Write instruction frequency totals to `instmix.txt`.
 */
void Machine::write_instruction_mix_report() {
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
    printf("__ file instmix.txt was generated after %ld cycle\n", cpu.mtime);
}

/**
 * @brief Print end-of-run timing and instruction statistics.
 */
void Machine::print_summary() {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(now - s_start_time).count();
    const Counter etime = static_cast<Counter>(elapsed == 0 ? 1 : elapsed);
    printf("__ Elapsed clocks (mtime)   : %11ld\n", cpu.mtime);
    printf("__ Executed instructions    : %11ld\n", e_icount);
    printf("__ Executed uc_instructions : %11ld\n", e_uc_cnt);
    printf("__ Fetched compressed insns : %11ld\n", e_ccount);
    printf("__ Elapsed time (usec)      : %11ld\n", etime);
    printf("__ Simulation speed (KIPS)  : %11ld\n", e_icount * 1000ul / etime);
    if (s_use_mix) write_instruction_mix_report();
}

/**
 * @brief Run the simulation loop until a termination condition is reached.
 */
void Machine::run() {
    if (s_gen_binfile) {
        binfile_gen(&cpu, mmem, disk->sector);
    }
    while (is_running_) {
        prepare_cycle();
        cpu.run_cycle(*this);
        finalize_cycle();
    }
}

/**
 * @brief Perform per-cycle side effects before CPU stage execution.
 *
 * This includes optional artifact dump generation, synthetic console input/timer handling,
 * and reset of pending trap bookkeeping fields.
 */
void Machine::prepare_cycle() {
    // Emit initialization artifacts at the configured cycle boundary.
    if (cpu.mtime == s_memimg) {
        dump_init_artifacts(&cpu, mmem, console.get(), disk.get(), disk->sector);
    }

    static constexpr std::array<Byte, 9> kSyntheticInput = {
        static_cast<Byte>('r'), static_cast<Byte>('o'),  static_cast<Byte>('o'),
        static_cast<Byte>('t'), static_cast<Byte>('\n'), static_cast<Byte>('t'),
        static_cast<Byte>('o'), static_cast<Byte>('p'),  static_cast<Byte>('\n')};
    static int adr = 0;

    if (cpu.mtime > s_enabletimer) { /* enable timer after linux boot */
        console->fifo_en = static_cast<Byte>(1);
        console->cons_fifo = kSyntheticInput[adr % static_cast<int>(kSyntheticInput.size())];

        if ((cpu.mtime & static_cast<Counter>(0xfffff)) == 0 &&
            console->fifo_en != static_cast<Byte>(0)) {  // 2019-08-30
            int ret = console->MC_receive_input(*this);  /* Keyboard */
            if (ret > 0) {
                cpu.plic_set_irq(simrv::virtio::kConsoleIrq, 1);
            }
            if (ret == -1) is_running_ = 0; /* break by Ctrl+q */
            adr++;
        } else if (cpu.mtimecmp < cpu.mtime) { /* Timer */
            cpu.mip |= enum_mask(MipBit::Mtip);
        }
    }

    cpu.pending_exception = ~0u; /* initialize regs */
    cpu.pending_tval = 0;        /* initialize regs */
}

constexpr Counter D_TRACEPC_INTERVAL = 1000;
/**
 * @brief Emit sparse PC trace samples to `tracepc.txt` every fixed interval.
 */
void emit_periodic_pc_trace(Counter mtime, Register cpc) {
    static int flag = 0;
    static std::ofstream out;
    if ((mtime % D_TRACEPC_INTERVAL) == 0) {
        if (flag == 0) {
            flag = 1;
            out.open("tracepc.txt");
            printf("__ generate trace file: tracepc.txt\n\n");
        }
        out << std::setfill('0') << std::setw(8) << std::dec
            << static_cast<int>(mtime / D_TRACEPC_INTERVAL) << ' ' << std::hex << std::setw(8)
            << cpc << '\n';
        out.flush();
    }
}

/**
 * @brief Emit branch prediction trace rows to `bpred.txt`.
 */
void emit_branch_prediction_trace(Counter mtime, Register cpc, Register jmp_pc, int r_opcode,
                                  int r_tkn) {
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
    out << std::setfill('0') << std::setw(8) << std::dec << static_cast<int>(mtime) << ' '
        << std::hex << std::setw(8) << cpc << ' ' << std::setw(8) << targ << std::dec << ' '
        << ir_jb << ' ' << r_tkn << ' ' << ir_jump << ' ' << ir_branch << '\n';
    out.flush();
}

constexpr Word CMD_PRINT_CHAR = 1; /* command for application mode using tohost */
constexpr Word CMD_POWER_OFF = 2;  /* command for application mode using tohost */
/**
 * @brief Apply end-of-cycle termination checks and optional trace outputs.
 */
void Machine::finalize_cycle() {
    if (s_strace != 0 && cpu.mtime >= s_strace)
        emit_periodic_pc_trace(cpu.mtime, cpu.pipeline_context.cpc);
    if (cpu.mtime >= s_trace_begin && cpu.mtime <= s_trace_end) write_trace_snapshot();
    if (cpu.mtime >= s_fincnt - 1) {
        printf("\n__finished by -e option\n");
        is_running_ = 0;
    }
    if (s_bp_trace) {
        emit_branch_prediction_trace(cpu.mtime, cpu.pipeline_context.cpc,
                                     cpu.pipeline_context.jmp_pc, cpu.pipeline_context.opcode,
                                     cpu.pipeline_context.tkn);
    }

    if (s_isatest && tohost != 0) {
        if (tohost == 1) {
            printf("\n__ ISA TEST PASS\n");
        } else if (tohost & 1) {
            printf("\n__ ISA TEST FAIL code=%u (tohost=0x%08x)\n", tohost >> 1, tohost);
        } else {
            printf("\n__ ISA TEST TOHOST update=0x%08x\n", tohost);
        }
        is_running_ = 0;
    }

    if ((tohost >> 16) == CMD_POWER_OFF) {
        printf("\n__ Power off\n");
        is_running_ = 0;
    }
    if ((tohost >> 16) == CMD_PRINT_CHAR) {
        printf("%c", (char)(tohost & 0xff));
        tohost = 0;
        fflush(stdout);
    }
}

/**
 * @brief Write one full architectural snapshot row set to the trace stream.
 */
void Machine::write_trace_snapshot() {
    if (!s_fp_trace.is_open()) {
        return;
    }

    auto write_hex = [&](Word value) {
        s_fp_trace << std::hex << std::setw(8) << std::setfill('0') << value;
    };

    s_fp_trace << std::dec << std::setw(8) << std::setfill('0') << cpu.mtime << ' ';
    write_hex(cpu.pipeline_context.cpc);
    s_fp_trace << ' ';
    write_hex(cpu.pipeline_context.ir);
    if (s_rtosmode) {
        s_fp_trace << ' ' << std::dec << std::setw(8) << std::setfill('0') << cpu.mtimecmp;
    }
    s_fp_trace << '\n';

    for (int i = 0; i < 4; i++) { /* output registers */
        for (int j = 0; j < 8; j++) {
            write_hex(cpu.reg[i * 8 + j]);
            s_fp_trace << (j != 7 ? ' ' : '\n');
        }
    }

    if (!s_appmode) {
        write_hex(cpu.mstatus);
        s_fp_trace << ' ';
        write_hex(cpu.mtvec);
        s_fp_trace << ' ';
        write_hex(cpu.mscratch);
        s_fp_trace << ' ';
        write_hex(cpu.mepc);
        s_fp_trace << ' ';
        write_hex(cpu.mcause);
        s_fp_trace << ' ';
        write_hex(cpu.mtval);
        s_fp_trace << ' ';
        write_hex(cpu.mhartid);
        s_fp_trace << ' ';
        write_hex(cpu.misa);
        s_fp_trace << '\n';

        write_hex(cpu.mie);
        s_fp_trace << ' ';
        write_hex(cpu.mip);
        s_fp_trace << ' ';
        write_hex(cpu.medeleg);
        s_fp_trace << ' ';
        write_hex(cpu.mideleg);
        s_fp_trace << ' ';
        write_hex(cpu.mcounteren);
        s_fp_trace << ' ';
        if (!s_rtosmode) {
            write_hex(cpu.stvec);
            s_fp_trace << ' ';
            write_hex(cpu.sscratch);
            s_fp_trace << ' ';
            write_hex(cpu.sepc);
            s_fp_trace << '\n';

            write_hex(cpu.scause);
            s_fp_trace << ' ';
            write_hex(cpu.stval);
            s_fp_trace << ' ';
            write_hex(cpu.satp);
            s_fp_trace << ' ';
            write_hex(cpu.scounteren);
            s_fp_trace << ' ';
            write_hex(cpu.load_res);
            s_fp_trace << ' ';
        }
        write_hex(cpu.pending_exception);
        s_fp_trace << ' ';
        write_hex(cpu.pending_tval);
        s_fp_trace << ' ';
        write_hex(cpu.priv);
        s_fp_trace << '\n';

        if (!s_rtosmode) {
            for (int i = 0; i < 4; i++) {
                write_hex(cpu.TLB_inst_r[i].v_addr);
                s_fp_trace << ' ';
                write_hex(cpu.TLB_inst_r[i].p_addr);
                s_fp_trace << ' ';
            }
            s_fp_trace << '\n';
            for (int i = 0; i < 4; i++) {
                write_hex(cpu.TLB_data_r[i].v_addr);
                s_fp_trace << ' ';
                write_hex(cpu.TLB_data_r[i].p_addr);
                s_fp_trace << ' ';
            }
            s_fp_trace << '\n';
            for (int i = 0; i < 4; i++) {
                write_hex(cpu.TLB_data_w[i].v_addr);
                s_fp_trace << ' ';
                write_hex(cpu.TLB_data_w[i].p_addr);
                s_fp_trace << ' ';
            }
            s_fp_trace << '\n';
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

/**
 * @brief Initialize machine components and load memory/device images.
 */
int Machine::initialize(int argc, char* argv[]) {
    set_options(this, argc, argv); /* set options before the object instantiations */

    disk = std::make_unique<Disk>();
    console = std::make_unique<Console>();
    mmem_owner_.reset(static_cast<Byte*>(std::calloc(simrv::memory::kDramSize, sizeof(Byte))));
    if (mmem_owner_ == nullptr) {
        std::fprintf(stderr, "Error: failed to allocate main memory (%zu bytes)\n",
                     static_cast<std::size_t>(simrv::memory::kDramSize));
        return 1;
    }
    mmem = mmem_owner_.get();
    console->mmem = mmem;
    disk->mmem = mmem;

    // MAKE Console QUEUE
    console_queue_owner_ = std::make_unique<QueueState[]>(simrv::virtio::kConsoleMaxQueueNum);
    disk_queue_owner_ = std::make_unique<QueueState[]>(simrv::virtio::kDiskMaxQueueNum);
    console->Queue = console_queue_owner_.get();
    disk->Queue = disk_queue_owner_.get();

    if (s_dlog_mode) s_fp_dlog.open("init_virtio.txt");

    cpu.pc = s_start_pc;
    cpu.reg[11] =
        (s_appmode || s_rtosmode) ? 0 : simrv::boot::kInitDataAddress + simrv::boot::kStartPc;
    cpu.TLB_flush();

    load_image_into_ram(s_fn_memimg, mmem);  // load a memory image file

    if (s_fn_dvtree.empty())
        load_devicetree(mmem + simrv::boot::kInitDataAddress);
    else
        load_image_into_ram(s_fn_dvtree, mmem + simrv::boot::kInitDataAddress);

    if (s_use_disk) load_image_into_ram(s_fn_dskimg, disk->sector);  // load a disk image file

    if (s_use_mix)
        for (int i = 0; i < OperationIdCount; i++) e_instmix[i] = 0;

    if (s_misa_override)
        cpu.misa = s_misa_profile;
    else if (s_rtosmode)
        cpu.misa = misa_profile_bits(MisaProfile::I);

    // #ifdef MIDDLE
    // #include "xinitreg.txt"
    // load_image_file_into_ram("xinitmem.bin", mmem);
    // disk->load_file("xinitdisk.bin", s_appmode);
    // #endif
    return 0;
}

Machine::~Machine() = default;

void Microcn::init(const std::string& image_path) {
    cmem_owner_ = std::make_unique<Byte[]>(simrv::memory::kLocalCoreMemorySize);
    cmem = cmem_owner_.get();
    load_image_into_ram(image_path, cmem);
    for (int i = 0; i < 32; i++) reg[i] = 0;
    reg[11] = 0x8000; /* simrv::boot::kInitDataAddress + simrv::boot::kStartPc; */
}

/**
 * @brief Execute one cycle of the optional I/O controller (Microcn).
 *
 * This is a simplified in-order pipeline execution for the micro-controller,
 * condensed into a single function. It performs fetch-decode-execute-memory-writeback
 * in a single call, using a local PipelineContext to maintain per-instruction transient state.
 *
 * @return true if execution should continue; false if system should halt (power-off command).
 *
 * @details
 * Flow:
 * 1. **Fetch & Decode**: Load and decompress instruction, extract opcode/fields/immediate.
 * 2. **Operand Fetch**: Read integer registers and CSR values into operand registers.
 * 3. **Execute**: Compute ALU results, branch conditions, decide memory addresses.
 * 4. **Memory**: Read/write to local/main memory or memory-mapped I/O regions.
 * 5. **Writeback**: Commit results to register file or state.
 *
 * @note
 * - Supports RV32I base ISA only (no compressed instruction support).
 * - Memory path includes special handling for MMIO regions (console queues, disk, etc.).
 * - Local state (PipelineContext ctx) is allocated on stack; not persisted between cycles.
 */
bool Microcn::exec() {
    DecodeUnit decode_unit;
    ExecuteUnit execute_unit;
    bool ret = true;
    PipelineContext ctx;

    // ===== FETCH & DECODE STAGE =====
    ctx.cpc = pc;
    memcpy(&ctx.ir, &cmem[pc & simrv::memory::kDramMask], 4);
    if ((ctx.ir & 3) != 3) {
        printf("__ ERROR: this microcn does not support compressed insn!\n");
        exit(0);
    }
    // Decode instruction fields from 32-bit word
    ctx.opcode = (ctx.ir >> 0) & 0x7F;
    ctx.rd = (ctx.ir >> 7) & 0x1f;
    ctx.rs1 = (ctx.ir >> 15) & 0x1f;
    ctx.rs2 = (ctx.ir >> 20) & 0x1f;
    ctx.funct3 = (ctx.ir >> 12) & 0x7;
    ctx.funct5 = (ctx.ir >> 27) & 0x1F;
    ctx.funct7 = (ctx.ir >> 25);
    ctx.funct12 = (ctx.ir >> 20);
    ctx.imm = decode_unit.decodeImmediate(ctx.ir);

    // ===== OPERAND FETCH STAGE =====
    ctx.rrs1 = reg[ctx.rs1]; /* regfile read port 1 */
    ctx.rrs2 = reg[ctx.rs2]; /* regfile read port 2 */

    // ===== EXECUTE STAGE =====
    switch (ctx.opcode) {
        case static_cast<Instruction>(Opcode::Lui): {
            ctx.tkn = 0;
            ctx.wb_data = ctx.imm << 12;
            break;
        }
        case static_cast<Instruction>(Opcode::Auipc): {
            ctx.tkn = 0;
            ctx.wb_data = pc + (ctx.imm << 12);
            break;
        }
        case static_cast<Instruction>(Opcode::Jal): {
            ctx.tkn = 1;
            ctx.wb_data = pc + 4;
            ctx.jmp_pc = pc + ctx.imm;
            break;
        }
        case static_cast<Instruction>(Opcode::Jalr): {
            ctx.tkn = 1;
            ctx.wb_data = pc + 4;
            ctx.jmp_pc = ctx.rrs1 + ctx.imm;
            break;
        }
        case static_cast<Instruction>(Opcode::Op): {
            ctx.tkn = 0;
            ctx.wb_data = execute_unit.aluInt(ctx.rrs1, ctx.rrs2, ctx.funct3, ctx.funct7);
            break;
        }
        case static_cast<Instruction>(Opcode::Load): {
            ctx.tkn = 0;
            ctx.mem_addr = ctx.rrs1 + ctx.imm;
            break;
        }
        case static_cast<Instruction>(Opcode::Store): {
            ctx.tkn = 0;
            ctx.mem_addr = ctx.rrs1 + ctx.imm;
            break;
        }
        case static_cast<Instruction>(Opcode::MiscMem): {
            ctx.tkn = 0;
            break;
        }
        case static_cast<Instruction>(Opcode::Branch): {
            ctx.tkn = execute_unit.branchTaken(ctx.rrs1, ctx.rrs2, ctx.funct3);
            ctx.jmp_pc = pc + ctx.imm;
            break;
        }
        case static_cast<Instruction>(Opcode::OpImm): {
            ctx.tkn = 0;
            ctx.funct7 &= (ctx.funct3 == static_cast<Instruction>(Funct3::Add)) ? 0 : 0x20;
            ctx.wb_data = execute_unit.aluInt(ctx.rrs1, ctx.imm, ctx.funct3, ctx.funct7);
            break;
        }
    }

    // ===== MEMORY STAGE =====
    int tmp = (1 << (ctx.funct3 & 0x3));
    if (ctx.opcode == static_cast<Instruction>(Opcode::Load)) {
        // Handle load from main memory, disk, or memory-mapped I/O regions
        if ((ctx.mem_addr >> 28) == 0x8) {
            ctx.mem_rdata = ram_read(ctx.mem_addr & simrv::memory::kDramMask, ctx.funct3, mmem);
        } else if ((ctx.mem_addr >> 28) == 0x9) {
            ctx.mem_rdata = disk_read(ctx.mem_addr & DISK_MASK, tmp, disk);
        } else if (ctx.mem_addr == 0x40009000) {
            ctx.mem_rdata = Mode;
        } else if (ctx.mem_addr == 0x40009004) {
            ctx.mem_rdata = Qnum;
        } else if (ctx.mem_addr == 0x40009008) {
            ctx.mem_rdata = Qsel;
        } else if ((ctx.mem_addr >> 12) == 0x4000a) {
            ctx.mem_rdata = queue_read(ctx.mem_addr & 0xff, cons_queue);
        } else if ((ctx.mem_addr >> 12) == 0x4000b) {
            ctx.mem_rdata = queue_read(ctx.mem_addr & 0xff, disk_queue);
        } else if ((ctx.mem_addr >> 12) == 0x4000c) {
            ctx.mem_rdata = static_cast<Word>(std::to_integer<uint8_t>(cons_fifo));
        } else
            ctx.mem_rdata = ram_read(ctx.mem_addr & simrv::memory::kDramMask, ctx.funct3, cmem);
    }
    if (ctx.opcode == static_cast<Instruction>(Opcode::Store)) {
        if (ctx.mem_addr == 0x40008000) {
            if (ctx.rrs2 >> 16 == 1) {
                printf("%c", (char)(ctx.rrs2 & 0xff));
                fflush(stdout);
            }
            if (ctx.rrs2 >> 16 == 2) {
                ret = false;
            }
            //            if(ctx.rrs2>>16==2){ printf("\n__ Power off\n"); ret=0;}
            //            //exit(0);}//ret=0;}
        } else if ((ctx.mem_addr >> 28) == 0x8) {
            for (int i = 0; i < (1 << ctx.funct3); i++) {
                mmem[(ctx.mem_addr + i) & simrv::memory::kDramMask] =
                    static_cast<Byte>(static_cast<uint8_t>((ctx.rrs2 >> (8 * i)) & 0xFF));
            }
        } else if ((ctx.mem_addr >> 28) == 0x9) {
            Word* dsk_tmp = reinterpret_cast<Word*>(disk);
            dsk_tmp[(ctx.mem_addr & DISK_MASK) / 4] = ctx.rrs2;

            /*for (int i=0; i<(1 << ctx.funct3); i++) {
                disk[(ctx.mem_addr+i) & DISK_MASK] =
                    static_cast<Byte>(static_cast<uint8_t>((ctx.rrs2 >> (8*i)) & 0xFF));
            }*/
        } else if ((ctx.mem_addr >> 12) == 0x4000a) {
            queue_write(ctx.mem_addr & 0xff, ctx.rrs2, cons_queue);
        } else if ((ctx.mem_addr >> 12) == 0x4000b) {
            queue_write(ctx.mem_addr & 0xff, ctx.rrs2, disk_queue);
        } else {
            for (int i = 0; i < (1 << ctx.funct3); i++) {
                cmem[(ctx.mem_addr + i) & simrv::memory::kDramMask] =
                    static_cast<Byte>(static_cast<uint8_t>((ctx.rrs2 >> (8 * i)) & 0xFF));
            }
        }
    }

    // ===== WRITEBACK STAGE =====
    // Select data and enable signal based on operation type
    Word wire_wb_r_data = 0;
    Word wire_wb_r_enable = 0;
    if (ctx.opcode == static_cast<Instruction>(Opcode::Load)) {
        wire_wb_r_data = ctx.mem_rdata;
        wire_wb_r_enable = 1;
    } else if ((ctx.opcode == static_cast<Instruction>(Opcode::Lui)) ||
               (ctx.opcode == static_cast<Instruction>(Opcode::Auipc)) ||
               (ctx.opcode == static_cast<Instruction>(Opcode::Jal)) ||
               (ctx.opcode == static_cast<Instruction>(Opcode::Jalr)) ||
               (ctx.opcode == static_cast<Instruction>(Opcode::Op)) ||
               (ctx.opcode == static_cast<Instruction>(Opcode::OpImm))) {
        wire_wb_r_data = ctx.wb_data;
        wire_wb_r_enable = 1;
    }

    // ===== COMMIT STAGE =====
    // Write result to destination register (rd != 0 to skip x0)
    if (wire_wb_r_enable && ctx.rd != 0) reg[ctx.rd] = wire_wb_r_data;

    // Update PC: jump if branch taken, otherwise sequential increment
    pc = (ctx.tkn) ? ctx.jmp_pc : pc + 4;

    if (owner != nullptr) {
        owner->e_uc_cnt++;
    }
    return ret;
}
