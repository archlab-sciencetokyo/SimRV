/**
 * @file MemorySubsystem.cpp
 * @brief Memory access and translation subsystem implementation.
 */
#include "MemorySubsystem.hpp"

#include "Machine.hpp"

namespace simrv::memory_detail {
auto page_walk(Address v_addr, Address* p_addr, PteAccess access, CPU* cpu, Byte* mmem) -> bool {
    constexpr Address kKernelPhysBase = static_cast<Address>(0x80400000u);
    constexpr Address kKernelLowPhysBase = static_cast<Address>(0x40000000u);
    constexpr Address kKernelVirtBase = static_cast<Address>(0xC0000000u);
    constexpr Address kKernelVirtEnd = kKernelVirtBase + simrv::memory::kDramSize;
    constexpr Address kDramPhysEnd = simrv::memory::kDramBaseAddress + simrv::memory::kDramSize;
    constexpr Address kKernelLowPhysEnd = kKernelLowPhysBase + simrv::memory::kDramSize;

    if (cpu->priv == kPrivSupervisor && v_addr >= kKernelVirtBase && v_addr < kKernelVirtEnd) {
        *p_addr = v_addr - kKernelVirtBase + kKernelPhysBase;
        return false;
    }

    Word vpn1 = (v_addr >> 22) & 0x3FF;
    const Word root_ppn = (cpu->satp & 0x3FFFFF);
    Address root_pt_addr = static_cast<Address>(root_ppn << 12);

    if (root_pt_addr >= static_cast<Address>(0xC0000000u) &&
        root_pt_addr < static_cast<Address>(0xC0000000u + simrv::memory::kDramSize)) {
        root_pt_addr -= static_cast<Address>(0x40000000u);
    }

    auto read_l1_pte = [&](Address candidate_root, Word* out_pte_addr) {
        Word pte_addr = candidate_root + vpn1 * 4;
        Word pte = ram_read_fast(pte_addr, static_cast<Instruction>(Funct3::Lw), mmem);
        if (pte == 0) {
            Word mirror_vpn1 = 0;
            bool has_mirror = false;
            if (vpn1 >= 0x200u && vpn1 < 0x300u) {
                mirror_vpn1 = vpn1 + 0x100u;
                has_mirror = true;
            } else if (vpn1 >= 0x300u && vpn1 < 0x400u) {
                mirror_vpn1 = vpn1 - 0x100u;
                has_mirror = true;
            }
            if (has_mirror) {
                const Word mirror_pte_addr = candidate_root + mirror_vpn1 * 4;
                const Word mirror_pte =
                    ram_read_fast(mirror_pte_addr, static_cast<Instruction>(Funct3::Lw), mmem);
                if (mirror_pte & enum_mask(PteFlag::V)) {
                    pte = mirror_pte;
                    pte_addr = mirror_pte_addr;
                }
            }
        }
        *out_pte_addr = pte_addr;
        return pte;
    };

    Word L1_pte_addr = 0;
    Word L1_pte = read_l1_pte(root_pt_addr, &L1_pte_addr);
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
    Word L0_pte = ram_read_fast(L0_pte_addr, static_cast<Instruction>(Funct3::Lw), mmem);
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

    bool page_fault =
        (!(L1_pte & enum_mask(PteFlag::V))) || (L1_xwr != 0 && !L1_success) ||
        (L1_xwr == 0 && ((!(L0_pte & enum_mask(PteFlag::V))) || (L0_xwr == 0) || !L0_success));

    if (page_fault && cpu->priv == kPrivSupervisor &&
        ((v_addr >= kKernelPhysBase && v_addr < kDramPhysEnd) ||
         (v_addr >= kKernelLowPhysBase && v_addr < kKernelLowPhysEnd))) {
        page_fault = false;
        *p_addr = v_addr;
        return page_fault;
    }

    if (page_fault)
        *p_addr = 0;
    else if (L1_success)
        *p_addr = L1_p_addr;
    else if (L0_success)
        *p_addr = L0_p_addr;

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
}  // namespace simrv::memory_detail

auto MemorySubsystem::target_read(CPU& cpu, Address v_addr, Instruction funct3) -> Word {
    if (simrv::compiler::likely(cpu.pending_exception == ~0u) &&
        simrv::compiler::likely(cpu.priv == kPrivMachine || (cpu.satp >> 31) == 0) &&
        simrv::compiler::likely(simrv::memory_detail::is_dram_addr(v_addr))) {
        return simrv::memory_detail::ram_read_fast(v_addr, funct3, machine_.mmem);
    }

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
        if (simrv::memory_detail::is_dram_addr(p_addr)) {
            rdata = simrv::memory_detail::ram_read_fast(p_addr, funct3, machine_.mmem);
            return rdata;
        }

        if (machine_.mmio_router_.read(p_addr, rdata)) return rdata;
        if (!simrv::memory_detail::is_legacy_reserved_region(p_addr)) {
            rdata = simrv::memory_detail::ram_read_fast(p_addr, funct3, machine_.mmem);
        }
    }
    return rdata;
}

void MemorySubsystem::target_write(CPU& cpu, Address v_addr, Word wdata, Instruction funct3) {
    if (simrv::compiler::likely(cpu.pending_exception == ~0u) &&
        simrv::compiler::likely(cpu.priv == kPrivMachine || (cpu.satp >> 31) == 0) &&
        simrv::compiler::likely(simrv::memory_detail::is_dram_addr(v_addr))) {
        simrv::memory_detail::ram_write_fast(v_addr, wdata, funct3, machine_.mmem);
        if (machine_.s_isatest && funct3 == static_cast<Instruction>(Funct3::Sw) &&
            v_addr == machine_.s_isatest_tohost) {
            machine_.tohost = wdata;
        }
        return;
    }

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
        if (simrv::memory_detail::is_dram_addr(p_addr)) {
            simrv::memory_detail::ram_write_fast(p_addr, wdata, funct3, machine_.mmem);
            if (machine_.s_isatest && funct3 == static_cast<Instruction>(Funct3::Sw) &&
                p_addr == machine_.s_isatest_tohost) {
                machine_.tohost = wdata;
            }
            return;
        }

        if (machine_.mmio_router_.write(p_addr, wdata)) return;
        if (!simrv::memory_detail::is_legacy_reserved_region(p_addr)) {
            simrv::memory_detail::ram_write_fast(p_addr, wdata, funct3, machine_.mmem);
            if (machine_.s_isatest && funct3 == static_cast<Instruction>(Funct3::Sw) &&
                p_addr == machine_.s_isatest_tohost) {
                machine_.tohost = wdata;
            }
        }
    }
}
