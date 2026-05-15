/**
 * @file BootHacks.cpp
 * @brief Implementation of legacy bootloader compat hacks.
 */
#include "simrv/core/BootHacks.hpp"

#include <ranges>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Constants.hpp"

namespace simrv::boot {

void handle_legacy_bbl_satp_hacks(simrv::core::Machine& machine, Word& satp) {
    if constexpr (simrv::xlen::kIsXLen64) {
        return;  // Legacy BBL hacks are unnecessary for RV64/OpenSBI
    }

    if (!simrv::xlen::satp_translation_enabled(satp)) {
        return;
    }

    Word root_ppn = simrv::xlen::satp_root_ppn(satp);

    // 1. Correct virtual addresses written into the physical PPN field
    constexpr Address kLinuxHighKernelBase = 0xC0000000U;
    constexpr Address kLinuxHighKernelOffset = 0x40000000U;
    const Address candidate_root_addr = static_cast<Address>(root_ppn << 12);

    if (candidate_root_addr >= kLinuxHighKernelBase &&
        candidate_root_addr < (kLinuxHighKernelBase + simrv::memory::kDramSize)) {
        root_ppn -= (kLinuxHighKernelOffset >> 12);
        // Patch the CSR architecturally so the MMU sees the correct physical PPN
        satp = (satp & ~simrv::xlen::kSatpRootPpnMask) | root_ppn;
    }

    const Address root_pt_addr = static_cast<Address>(root_ppn << 12);
    constexpr Word kPteSize = 4;
    auto* mmem = machine.mmem;

    if (mmem == nullptr) {
        return;
    }

    // 2. Ensure 1:1 identity mapping exists for the 64MB DRAM range (VPN 0x200 - 0x20F).
    for (Word vpn : std::views::iota(0x200u, 0x210u)) {
        const Word pte_addr = root_pt_addr + (vpn * kPteSize);
        Word pte =
            simrv::memory::ram_read_fast(pte_addr, static_cast<Instruction>(Funct3::Lw), mmem);
        if (pte == 0) {
            Word identity_pte = (vpn << 20) | enum_mask(PteFlag::V) | enum_mask(PteFlag::R) |
                                enum_mask(PteFlag::W) | enum_mask(PteFlag::X) |
                                enum_mask(PteFlag::A) | enum_mask(PteFlag::D);
            simrv::memory::ram_write_fast(pte_addr, identity_pte,
                                          static_cast<Instruction>(Funct3::Sw), mmem);
        }
    }

    // 3. Mirror PTEs from 0x80000000 (VPN 0x200..0x300) to 0xC0000000 (VPN 0x300..0x400)
    for (Word vpn : std::views::iota(0x200u, 0x300u)) {
        const Word src_pte_addr = root_pt_addr + (vpn * kPteSize);
        const Word dst_pte_addr = root_pt_addr + ((vpn + 0x100) * kPteSize);

        Word pte =
            simrv::memory::ram_read_fast(src_pte_addr, static_cast<Instruction>(Funct3::Lw), mmem);
        if (pte != 0) {
            Word existing_dst = simrv::memory::ram_read_fast(
                dst_pte_addr, static_cast<Instruction>(Funct3::Lw), mmem);
            if (existing_dst == 0) {
                simrv::memory::ram_write_fast(dst_pte_addr, pte,
                                              static_cast<Instruction>(Funct3::Sw), mmem);
            }
        }
    }
}

auto normalize_legacy_trap_pc(const core::ArchState& state) -> Address {
    if constexpr (simrv::xlen::kIsXLen64) {
        return state.pc;
    }
    if (state.priv != kPrivSupervisor || !simrv::xlen::satp_translation_enabled(state.satp)) {
        return state.pc;
    }
    constexpr Address kKernelPhysBase = 0x80400000U;
    constexpr Address kKernelPhysEnd = 0x84000000U;
    constexpr Address kKernelVirtBase = 0xC0000000U;
    if (state.pc >= kKernelPhysBase && state.pc < kKernelPhysEnd) {
        return state.pc - kKernelPhysBase + kKernelVirtBase;
    }
    return state.pc;
}

}  // namespace simrv::boot