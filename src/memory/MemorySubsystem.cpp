/**
 * @file MemorySubsystem.cpp
 * @brief Memory access and translation subsystem implementation.
 */
#include "MemorySubsystem.hpp"

#include "Machine.hpp"

namespace simrv::memory_detail {
Word ram_read(Address addr, Instruction funct3, Byte* ram) {
    Word rdata = 0;
    int n = (1 << (funct3 & 0x3));
    for (int i = 0; i < n; i++) {
        rdata |=
            static_cast<Word>(std::to_integer<uint8_t>(ram[(addr + i) & simrv::memory::kDramMask]))
            << (8 * i);
    }

    if ((funct3 & 0x4) == 0) {
        Word sign_mask = (~((Word)0)) << (8 * n - 1);
        rdata |= ((sign_mask & rdata) ? sign_mask : 0);
    }
    return rdata;
}

int page_walk(Address v_addr, Address* p_addr, PteAccess access, CPU* cpu, Byte* mmem) {
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
          ((L1_xwr >> static_cast<Word>(access)) & 1) == 0);

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
          ((L0_xwr >> static_cast<Word>(access)) & 1) == 0);

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

    if (ret)
        *p_addr = 0;
    else if (L1_success)
        *p_addr = L1_p_addr;
    else if (L0_success)
        *p_addr = L0_p_addr;

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
}  // namespace simrv::memory_detail

Word MemorySubsystem::target_read(CPU& cpu, Address v_addr, Instruction funct3) {
    Word rdata = 0;
    Address p_addr;
    TLBEntry* entry =
        &cpu.TLB_data_r[(v_addr >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1)];

    if (cpu.priv == kPrivMachine || (cpu.satp >> 31) == 0) {
        p_addr = v_addr;
    } else if (entry->v_addr == (v_addr & ~simrv::memory::kPageMask)) {
        p_addr = entry->p_addr + (v_addr & simrv::memory::kPageMask);
    } else {
        if (simrv::memory_detail::page_walk(v_addr, &p_addr, PteAccess::Read, &cpu,
                                            machine_.mmem)) {
            cpu.pending_exception = enum_mask(ExceptionCode::LoadPageFault);
            cpu.pending_tval = v_addr;
        } else {
            entry->v_addr = v_addr & ~simrv::memory::kPageMask;
            entry->p_addr = p_addr & ~simrv::memory::kPageMask;
        }
    }

    if (cpu.pending_exception == ~0u) {
        if (machine_.mmio_router_.read(p_addr, rdata)) return rdata;

        switch (p_addr & 0xF0000000) {
            case 0x10000000:
            case 0x20000000:
            case 0x30000000:
            case 0x70000000:
                break;
            default:
                rdata = simrv::memory_detail::ram_read(p_addr, funct3, machine_.mmem);
                break;
        }
    }
    return rdata;
}

void MemorySubsystem::target_write(CPU& cpu, Address v_addr, Word wdata, Instruction funct3) {
    Address p_addr;
    TLBEntry* entry =
        &cpu.TLB_data_w[(v_addr >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1)];

    if (cpu.priv == kPrivMachine || (cpu.satp >> 31) == 0) {
        p_addr = v_addr;
    } else if (entry->v_addr == (v_addr & ~simrv::memory::kPageMask)) {
        p_addr = entry->p_addr + (v_addr & simrv::memory::kPageMask);
    } else {
        if (simrv::memory_detail::page_walk(v_addr, &p_addr, PteAccess::Write, &cpu,
                                            machine_.mmem)) {
            cpu.pending_exception = enum_mask(ExceptionCode::StorePageFault);
            cpu.pending_tval = v_addr;
        } else {
            entry->v_addr = v_addr & ~simrv::memory::kPageMask;
            entry->p_addr = p_addr & ~simrv::memory::kPageMask;
        }
    }

    if (cpu.pending_exception == ~0u) {
        if (machine_.mmio_router_.write(p_addr, wdata)) return;

        switch (p_addr & 0xF0000000) {
            case 0x10000000:
            case 0x20000000:
            case 0x30000000:
            case 0x70000000:
                break;
            default:
                for (int i = 0; i < (1 << funct3); i++) {
                    machine_.mmem[(p_addr + i) & simrv::memory::kDramMask] =
                        static_cast<Byte>(static_cast<uint8_t>((wdata >> (8 * i)) & 0xFF));
                }
                if (machine_.s_isatest && funct3 == static_cast<Instruction>(Funct3::Sw) &&
                    p_addr == machine_.s_isatest_tohost) {
                    machine_.tohost = wdata;
                }
                break;
        }
    }
}
