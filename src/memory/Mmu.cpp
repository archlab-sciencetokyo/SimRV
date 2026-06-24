/**
 * @file Mmu.cpp
 * @brief Memory Management Unit implementation for SV32 virtual memory.
 */
#include "simrv/memory/Mmu.hpp"


#include <optional>
#include <ranges>

#include "simrv/Define.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Helpers.hpp"


namespace simrv {

using isa::Funct3;
using core::MstatusBit;

Mmu::Mmu(Byte* mmem) : mmem_(mmem) {}

auto Mmu::translate(Address v_addr, PteAccess access, PrivilegeLevel priv, CSRValue mstatus,
                    Word satp, unsigned xlen) -> std::expected<Address, TrapCause> {
    // Ensure virtual address fits within XLEN mask
    if (simrv::compiler::unlikely((v_addr & ~simrv::xlen::kAddrMask) != 0)) {
        // Optionally abort translation in debug builds
        // std::terminate();
    }
    // Machine mode or MMU disabled: use physical addressing
    if (priv == kPrivMachine || !simrv::xlen::satp_translation_enabled(satp, xlen)) {
        return v_addr;
    }


    // Translate through page tables
    return page_walk(v_addr, access, priv, mstatus, satp, xlen);
}

auto Mmu::validate_pte_permissions(Word pte, Word permission_bits, PteAccess access,
                                   PrivilegeLevel priv, CSRValue mstatus) const -> bool {
    // XWR field must not be reserved values: Write-Only (2) or Write-Execute (6)
    constexpr Word kPermWriteOnly = 2;
    constexpr Word kPermWriteExecute = 6;
    const Word original_rwx = (pte >> 1) & 0x7;
    if (original_rwx == kPermWriteOnly || original_rwx == kPermWriteExecute) {
        return false;
    }

    // Supervisor access to user-only page without SUM
    if (priv == kPrivSupervisor && ((pte & enum_mask(PteFlag::U)) != 0u) &&
        ((mstatus & enum_mask(MstatusBit::Sum)) == 0u)) {
        return false;
    }

    // User accessing supervisor page
    if (priv == kPrivUser && ((pte & enum_mask(PteFlag::U)) == 0u)) {
        return false;
    }

    // Check access permissions
    if (((permission_bits >> static_cast<Word>(access)) & 1) == 0) {
        return false;
    }

    return true;
}

void Mmu::update_pte_access_bits(Address pte_addr, Word& pte_value, PteAccess access, unsigned pte_size) {
    // Update A (accessed) and D (dirty) bits as per RISC-V spec
    Word updated_pte = pte_value | enum_mask(PteFlag::A);
    if (access == PteAccess::Write) {
        updated_pte |= enum_mask(PteFlag::D);
    }

    // Write back only if modified
    if (updated_pte != pte_value) {
        pte_value = updated_pte;
        Instruction const store_op = (pte_size == 4) ? static_cast<Instruction>(Funct3::Sw)
                                                     : static_cast<Instruction>(Funct3::Sd);
        simrv::memory::ram_write_fast(pte_addr, updated_pte, store_op, mmem_);

    }
}

auto Mmu::page_walk(Address v_addr, PteAccess access, PrivilegeLevel priv, CSRValue mstatus,
                    Word satp, unsigned xlen) -> std::expected<Address, TrapCause> {
    auto make_fault = [access]() -> TrapCause {
        switch (access) {
            case PteAccess::Code:
                return enum_mask(ExceptionCode::FetchPageFault);
            case PteAccess::Write:
                return enum_mask(ExceptionCode::StorePageFault);
            case PteAccess::Read:
            default:
                return enum_mask(ExceptionCode::LoadPageFault);
        }
    };

    int levels = 2;
    int pte_size = 4;
    int vpn_bits_per_level = 10;

    const Word mode = simrv::xlen::satp_mode(satp, xlen);
    if (xlen == 64) {
        if (mode == 8) {  // SV39
            levels = 3;
            pte_size = 8;
            vpn_bits_per_level = 9;
        } else if (mode == 9) {  // SV48
            levels = 4;
            pte_size = 8;
            vpn_bits_per_level = 9;
        } else if (mode == 1) {  // SV32 compatibility mode under RV64
            levels = 2;
            pte_size = 4;
            vpn_bits_per_level = 10;
        } else {
            return std::unexpected(make_fault());
        }
    } else {
        if (mode != 1) {  // SV32
            return std::unexpected(make_fault());
        }
        levels = 2;
        pte_size = 4;
        vpn_bits_per_level = 10;
    }

    const int vpn_shift_base = 12;  // Page size is 4KB (2^12)
    const Word vpn_mask = (static_cast<Word>(1) << vpn_bits_per_level) - 1;
    const Instruction pte_load_op = (pte_size == 4) ? static_cast<Instruction>(Funct3::Lw)
                                                    : static_cast<Instruction>(Funct3::Ld);

    const Word root_ppn = simrv::xlen::satp_root_ppn(satp, xlen);
    auto root_pt_addr = static_cast<Address>(root_ppn << 12);

    Word pte = 0;
    Word pte_addr = 0;
    std::optional<int> leaf_level;

    for (int i : std::views::iota(0, levels) | std::views::reverse) {
        const Word vpn_i = (v_addr >> (vpn_shift_base + i * vpn_bits_per_level)) & vpn_mask;

        pte_addr = root_pt_addr + (vpn_i * pte_size);
        pte = simrv::memory::ram_read_fast(pte_addr, pte_load_op, mmem_);
        if (pte_size == 4) {
            pte &= 0xFFFFFFFFu;
        }

        if (simrv::compiler::unlikely((pte & enum_mask(PteFlag::V)) == 0u)) {
            // PTE invalid: page fault
            return std::unexpected(make_fault());
        }

        const Word original_rwx = (pte >> 1) & 0x7;

        if (original_rwx == 0) {
            // Pointer to next level
            root_pt_addr = static_cast<Address>((pte >> kPteShift) << 12);
        } else {
            // Leaf PTE found
            leaf_level = i;
            break;
        }
    }

    if (!leaf_level.has_value()) {
        // Leaf PTE not found after traversing all levels
        return std::unexpected(make_fault());
    }

    const int i = *leaf_level;

    const bool mxr = (mstatus & enum_mask(MstatusBit::Mxr)) != 0u;
    const bool pte_r = (pte & enum_mask(PteFlag::R)) != 0u;
    const bool pte_w = (pte & enum_mask(PteFlag::W)) != 0u;
    const bool pte_x = (pte & enum_mask(PteFlag::X)) != 0u;

    const Word permission_bits = (static_cast<Word>(pte_x) << 2) |
                                 (static_cast<Word>(pte_w) << 1) |
                                 static_cast<Word>(pte_r | (mxr && pte_x));

    if (!validate_pte_permissions(pte, permission_bits, access, priv, mstatus)) {
        return std::unexpected(make_fault());
    }

    // Check for misaligned superpage (PPN[i-1:0] bits must be zero)
    const Word ppn_mask_for_level =
        (i == 0) ? 0 : (static_cast<Word>(1) << (i * vpn_bits_per_level)) - 1;
    const Word ppn = (pte >> kPteShift);
    if (simrv::compiler::unlikely((ppn & ppn_mask_for_level) != 0)) {
        return std::unexpected(make_fault());
    }

    // Calculate physical address dynamically utilizing the generic masks
    const Word offset_bits = vpn_shift_base + i * vpn_bits_per_level;
    const Word offset_mask = (static_cast<Word>(1) << offset_bits) - 1;
    const Word phys_addr = (v_addr & offset_mask) | ((ppn << 12) & ~offset_mask);

    update_pte_access_bits(pte_addr, pte, access, pte_size);
    return phys_addr;
}

}  // namespace simrv
