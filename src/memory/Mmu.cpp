/**
 * @file Mmu.cpp
 * @brief Memory Management Unit implementation for SV32 virtual memory.
 */
#include "simrv/memory/Mmu.hpp"

#include <cstdint>
#include <optional>
#include <ranges>

#include "simrv/Define.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Constants.hpp"

namespace simrv {

// Static member initialization
Word Mmu::s_last_valid_root_ppn = 0;

Mmu::Mmu(Byte* mmem) : mmem_(mmem) {}

auto Mmu::translate(Address v_addr, PteAccess access, PrivilegeLevel priv, CSRValue mstatus,
                    Word satp) -> std::expected<Address, TrapCause> {
    // Machine mode or MMU disabled: use physical addressing
    if (priv == kPrivMachine || !simrv::xlen::satp_translation_enabled(satp)) {
        return v_addr;
    }

    // Translate through page tables
    return page_walk(v_addr, access, priv, mstatus, satp);
}

auto Mmu::validate_pte_permissions(Word pte, Word permission_bits, PteAccess access,
                                   PrivilegeLevel priv, CSRValue mstatus) const -> bool {
    // XWR field must not be reserved values: Write-Only (2) or Write-Execute (6)
    constexpr Word kPermWriteOnly = 2;
    constexpr Word kPermWriteExecute = 6;
    if (permission_bits == kPermWriteOnly || permission_bits == kPermWriteExecute) {
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

void Mmu::update_pte_access_bits(Address pte_addr, Word& pte_value, PteAccess access) {
    // Update A (accessed) and D (dirty) bits as per RISC-V spec
    Word updated_pte = pte_value | enum_mask(PteFlag::A);
    if (access == PteAccess::Write) {
        updated_pte |= enum_mask(PteFlag::D);
    }

    // Write back only if modified
    if (updated_pte != pte_value) {
        pte_value = updated_pte;
        Instruction const store_op = simrv::xlen::kIsXLen64 ? static_cast<Instruction>(Funct3::Sd)
                                                            : static_cast<Instruction>(Funct3::Sw);
        simrv::memory::ram_write_fast(pte_addr, updated_pte, store_op, mmem_);
    }
}

auto Mmu::page_walk(Address v_addr, PteAccess access, PrivilegeLevel priv, CSRValue mstatus,
                    Word satp) -> std::expected<Address, TrapCause> {
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

    if constexpr (simrv::xlen::kIsXLen64) {
        const Word mode = simrv::xlen::satp_mode(satp);
        if (mode == 8) {  // SV39
            levels = 3;
            pte_size = 8;
            vpn_bits_per_level = 9;
        } else if (mode == 9) {  // SV48
            levels = 4;
            pte_size = 8;
            vpn_bits_per_level = 9;
        } else {
            return std::unexpected(make_fault());
        }
    } else {
        const Word mode = simrv::xlen::satp_mode(satp);
        if (mode != 1) {  // SV32
            return std::unexpected(make_fault());
        }
    }

    const int vpn_shift_base = 12;  // Page size is 4KB (2^12)
    const Word vpn_mask = (static_cast<Word>(1) << vpn_bits_per_level) - 1;
    const Instruction pte_load_op = simrv::xlen::kIsXLen64 ? static_cast<Instruction>(Funct3::Ld)
                                                           : static_cast<Instruction>(Funct3::Lw);

    const Word root_ppn = (satp & kPpnMask);
    auto root_pt_addr = static_cast<Address>(root_ppn << 12);

    Word pte = 0;
    Word pte_addr = 0;
    std::optional<int> leaf_level;

    for (int i : std::views::iota(0, levels) | std::views::reverse) {
        const Word vpn_i = (v_addr >> (vpn_shift_base + i * vpn_bits_per_level)) & vpn_mask;

        pte_addr = root_pt_addr + (vpn_i * pte_size);
        pte = simrv::memory::ram_read_fast(pte_addr, pte_load_op, mmem_);

        if ((pte & enum_mask(PteFlag::V)) == 0u) {
            // PTE invalid: page fault
            return std::unexpected(make_fault());
        }

        const Word permission_bits =
            (((mstatus & enum_mask(MstatusBit::Mxr)) != 0u) ? pte >> 1 | pte >> 3 : pte >> 1) &
            kPermissionBitsMask;

        if (permission_bits == 0) {
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

    const Word permission_bits =
        (((mstatus & enum_mask(MstatusBit::Mxr)) != 0u) ? pte >> 1 | pte >> 3 : pte >> 1) &
        kPermissionBitsMask;

    if (!validate_pte_permissions(pte, permission_bits, access, priv, mstatus)) {
        return std::unexpected(make_fault());
    }

    // Check for misaligned superpage (PPN[i-1:0] bits must be zero)
    const Word ppn_mask_for_level =
        (i == 0) ? 0 : (static_cast<Word>(1) << (i * vpn_bits_per_level)) - 1;
    const Word ppn = (pte >> kPteShift);
    if ((ppn & ppn_mask_for_level) != 0) {
        return std::unexpected(make_fault());
    }

    // Calculate physical address dynamically utilizing the generic masks
    const Word offset_bits = vpn_shift_base + i * vpn_bits_per_level;
    const Word offset_mask = (static_cast<Word>(1) << offset_bits) - 1;
    const Word phys_addr = (v_addr & offset_mask) | ((ppn << 12) & ~offset_mask);

    update_pte_access_bits(pte_addr, pte, access);
    return phys_addr;
}

}  // namespace simrv
