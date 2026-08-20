/**
 * @file Mmu.cpp
 * @brief Memory Management Unit implementation for Sv32, Sv39, and Sv48.
 */
#include "simrv/memory/Mmu.hpp"

#include <optional>
#include <ranges>
#include <utility>

#include "simrv/Define.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Helpers.hpp"

namespace simrv {

using core::MstatusBit;
using isa::Funct3;

Mmu::Mmu(Byte* mmem, Address dram_base, Address dram_size)
    : mmem_(mmem), dram_base_(dram_base), dram_size_(dram_size) {}

auto Mmu::pte_access_valid(Address address, unsigned size) const -> bool {
    if (mmem_ == nullptr || size == 0 || dram_size_ < size || address < dram_base_) {
        return false;
    }
    return address - dram_base_ <= dram_size_ - size;
}

auto Mmu::translate(Address v_addr, PteAccess access, PrivilegeLevel priv, CSRValue mstatus,
                    Word satp, unsigned xlen, bool update_access_bits)
    -> std::expected<Address, TrapCause> {
    // Machine mode or MMU disabled: use physical addressing
    if (priv == kPrivMachine || !simrv::xlen::satp_translation_enabled(satp, xlen)) {
        return v_addr;
    }

    // Validate Sv39 / Sv48 canonical addresses for RV64
    if (xlen == 64) {
        Word const satp_mode = simrv::xlen::satp_mode(satp, 64);
        if (satp_mode == 8) {  // Sv39 (39-bit VAs: bits 63..38 must match bit 38)
            int64_t const signed_vaddr = static_cast<int64_t>(v_addr << 25) >> 25;
            if (simrv::compiler::unlikely(static_cast<uint64_t>(signed_vaddr) != v_addr)) {
                switch (access) {
                    case PteAccess::Code:
                        return std::unexpected(enum_mask(ExceptionCode::FetchPageFault));
                    case PteAccess::Write:
                        return std::unexpected(enum_mask(ExceptionCode::StorePageFault));
                    case PteAccess::Read:
                    default:
                        return std::unexpected(enum_mask(ExceptionCode::LoadPageFault));
                }
            }
        } else if (satp_mode == 9) {  // Sv48 (48-bit VAs: bits 63..47 must match bit 47)
            int64_t const signed_vaddr = static_cast<int64_t>(v_addr << 16) >> 16;
            if (simrv::compiler::unlikely(static_cast<uint64_t>(signed_vaddr) != v_addr)) {
                switch (access) {
                    case PteAccess::Code:
                        return std::unexpected(enum_mask(ExceptionCode::FetchPageFault));
                    case PteAccess::Write:
                        return std::unexpected(enum_mask(ExceptionCode::StorePageFault));
                    case PteAccess::Read:
                    default:
                        return std::unexpected(enum_mask(ExceptionCode::LoadPageFault));
                }
            }
        }
    }

    // Translate through page tables
    return page_walk(v_addr, access, priv, mstatus, satp, xlen, update_access_bits);
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

    // Supervisor access to user-only page (U=1)
    if (priv == kPrivSupervisor && ((pte & enum_mask(PteFlag::U)) != 0u)) {
        if (access == PteAccess::Code) {
            // Executing code from a U=1 page in Supervisor mode raises a Page Fault regardless of
            // SUM
            return false;
        }
        if ((mstatus & enum_mask(MstatusBit::Sum)) == 0u) {
            return false;
        }
    }

    // User accessing supervisor page
    if (priv == kPrivUser && ((pte & enum_mask(PteFlag::U)) == 0u)) {
        return false;
    }

    // Check access permissions
    if (((permission_bits >> std::to_underlying(access)) & 1) == 0) {
        return false;
    }

    return true;
}

void Mmu::update_pte_access_bits(Address pte_addr, Word& pte_value, PteAccess access,
                                 unsigned pte_size) {
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
        simrv::memory::host_write_fast(mmem_ + (pte_addr - dram_base_), updated_pte, store_op);
    }
}

auto Mmu::page_walk(Address v_addr, PteAccess access, PrivilegeLevel priv, CSRValue mstatus,
                    Word satp, unsigned xlen, bool update_access_bits)
    -> std::expected<Address, TrapCause> {
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
    auto make_access_fault = [access]() -> TrapCause {
        switch (access) {
            case PteAccess::Code:
                return enum_mask(ExceptionCode::FaultFetch);
            case PteAccess::Write:
                return enum_mask(ExceptionCode::FaultStore);
            case PteAccess::Read:
            default:
                return enum_mask(ExceptionCode::FaultLoad);
        }
    };

    if (simrv::compiler::unlikely(!cached_valid_ || cached_satp_ != satp || cached_xlen_ != xlen)) {
        cached_satp_ = satp;
        cached_xlen_ = xlen;
        cached_satp_mode_ = simrv::xlen::satp_mode(satp, xlen);
        if (xlen == 64) {
            if (cached_satp_mode_ == 8) {  // SV39
                cached_levels_ = 3;
                cached_pte_size_ = 8;
                cached_vpn_bits_per_level_ = 9;
            } else if (cached_satp_mode_ == 9) {  // SV48
                cached_levels_ = 4;
                cached_pte_size_ = 8;
                cached_vpn_bits_per_level_ = 9;
            } else {
                cached_valid_ = false;
                return std::unexpected(make_fault());
            }
        } else {
            if (cached_satp_mode_ != 1) {  // SV32
                cached_valid_ = false;
                return std::unexpected(make_fault());
            }
            cached_levels_ = 2;
            cached_pte_size_ = 4;
            cached_vpn_bits_per_level_ = 10;
        }
        cached_root_ppn_ = simrv::xlen::satp_root_ppn(satp, xlen);
        cached_root_pt_addr_ = static_cast<Address>(cached_root_ppn_ << 12);
        cached_valid_ = true;
    }

    const int levels = cached_levels_;
    const int pte_size = cached_pte_size_;
    const int vpn_bits_per_level = cached_vpn_bits_per_level_;
    const int vpn_shift_base = 12;  // Page size is 4KB (2^12)
    const Word vpn_mask = (static_cast<Word>(1) << vpn_bits_per_level) - 1;
    const Instruction pte_load_op = (pte_size == 4) ? static_cast<Instruction>(Funct3::Lw)
                                                    : static_cast<Instruction>(Funct3::Ld);

    auto root_pt_addr = cached_root_pt_addr_;

    Word pte = 0;
    Word pte_addr = 0;
    std::optional<int> leaf_level;

    for (int i : std::views::iota(0, levels) | std::views::reverse) {
        const Word vpn_i = (v_addr >> (vpn_shift_base + i * vpn_bits_per_level)) & vpn_mask;

        pte_addr = root_pt_addr + (vpn_i * pte_size);
        if (simrv::compiler::unlikely(!pte_access_valid(pte_addr, pte_size))) {
            return std::unexpected(make_access_fault());
        }
        pte = simrv::memory::host_read_fast(mmem_ + (pte_addr - dram_base_), pte_load_op);
        if (pte_size == 4) {
            pte &= 0xFFFFFFFFu;
        }

        if (simrv::compiler::unlikely((pte & enum_mask(PteFlag::V)) == 0u)) {
            return std::unexpected(make_fault());
        }

        const Word original_rwx = (pte >> 1) & 0x7;

        // Svnapot and Svpbmt are not implemented, so Sv39/Sv48 PTE bits
        // 63:54 are reserved and must be zero.
        if (pte_size == 8 && simrv::compiler::unlikely((static_cast<uint64_t>(pte) >> 54U) != 0)) {
            return std::unexpected(make_fault());
        }

        if (original_rwx == 0) {
            // In a non-leaf PTE (R=0, W=0, X=0), D=1 is reserved and raises page fault.
            if (simrv::compiler::unlikely((pte & enum_mask(PteFlag::D)) != 0u)) {
                return std::unexpected(make_fault());
            }
            // Pointer to next level
            root_pt_addr = static_cast<Address>((pte >> kPteShift) << 12);
        } else {
            // Leaf PTE found
            leaf_level = i;
            break;
        }
    }

    if (!leaf_level.has_value()) {
        return std::unexpected(make_fault());
    }

    const int i = *leaf_level;

    const bool mxr = (mstatus & enum_mask(MstatusBit::Mxr)) != 0u;
    const bool pte_r = (pte & enum_mask(PteFlag::R)) != 0u;
    const bool pte_w = (pte & enum_mask(PteFlag::W)) != 0u;
    const bool pte_x = (pte & enum_mask(PteFlag::X)) != 0u;

    const Word permission_bits = (static_cast<Word>(pte_x) << 2) | (static_cast<Word>(pte_w) << 1) |
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

    if (update_access_bits) {
        update_pte_access_bits(pte_addr, pte, access, pte_size);
    }
    return phys_addr;
}

}  // namespace simrv
