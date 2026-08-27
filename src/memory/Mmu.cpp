/**
 * @file Mmu.cpp
 * @brief Memory Management Unit implementation for Sv32, Sv39, and Sv48.
 */
#include "simrv/memory/Mmu.hpp"

#include <utility>

#include "simrv/Define.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Pmp.hpp"
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
                    Word satp, unsigned xlen, bool update_access_bits,
                    const core::ArchState* arch_state) -> std::expected<Address, TrapCause> {
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
    return page_walk(v_addr, access, priv, mstatus, satp, xlen, update_access_bits, arch_state);
}

auto Mmu::page_fault_for(PteAccess access) -> TrapCause {
    switch (access) {
        case PteAccess::Code:
            return enum_mask(ExceptionCode::FetchPageFault);
        case PteAccess::Write:
            return enum_mask(ExceptionCode::StorePageFault);
        case PteAccess::Read:
        default:
            return enum_mask(ExceptionCode::LoadPageFault);
    }
}

auto Mmu::access_fault_for(PteAccess access) -> TrapCause {
    switch (access) {
        case PteAccess::Code:
            return enum_mask(ExceptionCode::FaultFetch);
        case PteAccess::Write:
            return enum_mask(ExceptionCode::FaultStore);
        case PteAccess::Read:
        default:
            return enum_mask(ExceptionCode::FaultLoad);
    }
}

void Mmu::fail_page_walk_access(PageWalkState& state) {
    state.fault = access_fault_for(state.access);
    state.status = PageWalkStatus::Fault;
}

void Mmu::select_next_pte(PageWalkState& state, Address table_address) const {
    const Word vpn_mask = (static_cast<Word>(1) << state.vpn_bits_per_level) - 1;
    const Word vpn =
        (state.virtual_address >> (12 + state.level * static_cast<int>(state.vpn_bits_per_level))) &
        vpn_mask;
    state.pte_address = table_address + vpn * state.pte_size;
    if (!pte_access_valid(state.pte_address, state.pte_size) ||
        (state.arch_state != nullptr &&
         !core::pmp::check_access(*state.arch_state, state.pte_address, state.pte_size,
                                  core::PmpAccessType::Read))) {
        fail_page_walk_access(state);
        return;
    }
    state.status = PageWalkStatus::ReadPte;
}

auto Mmu::begin_page_walk(Address v_addr, PteAccess access, PrivilegeLevel priv, CSRValue mstatus,
                          Word satp, unsigned xlen, bool update_access_bits,
                          const core::ArchState* arch_state) -> PageWalkState {
    PageWalkState state{.virtual_address = v_addr,
                        .mstatus = mstatus,
                        .access = access,
                        .privilege = priv,
                        .fault = page_fault_for(access),
                        .xlen = xlen,
                        .update_access_bits = update_access_bits,
                        .arch_state = arch_state};
    if (priv == kPrivMachine || !simrv::xlen::satp_translation_enabled(satp, xlen)) {
        state.physical_address = v_addr;
        state.status = PageWalkStatus::Complete;
        return state;
    }
    if (!is_canonical(v_addr, satp, xlen)) return state;

    const Word mode = simrv::xlen::satp_mode(satp, xlen);
    int levels = 0;
    if (xlen == 32 && mode == 1) {
        levels = 2;
        state.pte_size = 4;
        state.vpn_bits_per_level = 10;
    } else if (xlen == 64 && (mode == 8 || mode == 9)) {
        levels = mode == 8 ? 3 : 4;
        state.pte_size = 8;
        state.vpn_bits_per_level = 9;
    } else {
        return state;
    }
    state.level = levels - 1;
    const Address root = static_cast<Address>(simrv::xlen::satp_root_ppn(satp, xlen) << 12);
    select_next_pte(state, root);
    return state;
}

void Mmu::accept_page_walk_pte(PageWalkState& state, Word pte) const {
    if (state.status != PageWalkStatus::ReadPte) return;
    if (state.pte_size == 4) pte &= 0xFFFFFFFFu;
    state.pte = pte;
    if ((pte & enum_mask(PteFlag::V)) == 0u ||
        (state.pte_size == 8 && (static_cast<uint64_t>(pte) >> 54U) != 0)) {
        state.fault = page_fault_for(state.access);
        state.status = PageWalkStatus::Fault;
        return;
    }

    const Word original_rwx = (pte >> 1) & 0x7;
    if (original_rwx == 0) {
        if ((pte & enum_mask(PteFlag::D)) != 0u || state.level == 0) {
            state.fault = page_fault_for(state.access);
            state.status = PageWalkStatus::Fault;
            return;
        }
        --state.level;
        select_next_pte(state, static_cast<Address>((pte >> kPteShift) << 12));
        return;
    }

    const bool mxr = (state.mstatus & enum_mask(MstatusBit::Mxr)) != 0u;
    const bool pte_r = (pte & enum_mask(PteFlag::R)) != 0u;
    const bool pte_w = (pte & enum_mask(PteFlag::W)) != 0u;
    const bool pte_x = (pte & enum_mask(PteFlag::X)) != 0u;
    const Word permissions = (static_cast<Word>(pte_x) << 2) | (static_cast<Word>(pte_w) << 1) |
                             static_cast<Word>(pte_r || (mxr && pte_x));
    if (!validate_pte_permissions(pte, permissions, state.access, state.privilege, state.mstatus)) {
        state.fault = page_fault_for(state.access);
        state.status = PageWalkStatus::Fault;
        return;
    }

    const Word low_ppn_mask =
        state.level == 0 ? 0
                         : (static_cast<Word>(1) << (state.level * state.vpn_bits_per_level)) - 1;
    const Word ppn = pte >> kPteShift;
    if ((ppn & low_ppn_mask) != 0) {
        state.fault = page_fault_for(state.access);
        state.status = PageWalkStatus::Fault;
        return;
    }
    const unsigned offset_bits = 12 + state.level * state.vpn_bits_per_level;
    const Word offset_mask = (static_cast<Word>(1) << offset_bits) - 1;
    state.physical_address = (state.virtual_address & offset_mask) | ((ppn << 12) & ~offset_mask);

    if (state.arch_state != nullptr) {
        const auto pmp_access = state.access == PteAccess::Code ? core::PmpAccessType::Execute
                                                                : (state.access == PteAccess::Write
                                                                       ? core::PmpAccessType::Write
                                                                       : core::PmpAccessType::Read);
        if (!core::pmp::check_access(*state.arch_state, state.physical_address, 1, pmp_access)) {
            state.fault = access_fault_for(state.access);
            state.status = PageWalkStatus::Fault;
            return;
        }
    }

    Word updated = pte | enum_mask(PteFlag::A);
    if (state.access == PteAccess::Write) updated |= enum_mask(PteFlag::D);
    if (state.update_access_bits && updated != pte) {
        if (state.arch_state != nullptr &&
            !core::pmp::check_access(*state.arch_state, state.pte_address, state.pte_size,
                                     core::PmpAccessType::Write)) {
            fail_page_walk_access(state);
            return;
        }
        state.pte = updated;
        state.pte_update_mask = updated ^ pte;
        state.status = PageWalkStatus::WritePte;
    } else {
        state.status = PageWalkStatus::Complete;
    }
}

void Mmu::accept_page_walk_write(PageWalkState& state) {
    if (state.status == PageWalkStatus::WritePte) state.status = PageWalkStatus::Complete;
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

auto Mmu::page_walk(Address v_addr, PteAccess access, PrivilegeLevel priv, CSRValue mstatus,
                    Word satp, unsigned xlen, bool update_access_bits,
                    const core::ArchState* arch_state) -> std::expected<Address, TrapCause> {
    auto state =
        begin_page_walk(v_addr, access, priv, mstatus, satp, xlen, update_access_bits, arch_state);
    while (state.status == PageWalkStatus::ReadPte || state.status == PageWalkStatus::WritePte) {
        const Instruction operation =
            state.pte_size == 4
                ? static_cast<Instruction>(state.status == PageWalkStatus::ReadPte ? Funct3::Lw
                                                                                   : Funct3::Sw)
                : static_cast<Instruction>(state.status == PageWalkStatus::ReadPte ? Funct3::Ld
                                                                                   : Funct3::Sd);
        if (state.status == PageWalkStatus::ReadPte) {
            const Word pte =
                simrv::memory::host_read_fast(mmem_ + (state.pte_address - dram_base_), operation);
            accept_page_walk_pte(state, pte);
        } else {
            const Word current = simrv::memory::host_read_fast(
                mmem_ + (state.pte_address - dram_base_),
                state.pte_size == 4 ? static_cast<Instruction>(Funct3::Lw)
                                    : static_cast<Instruction>(Funct3::Ld));
            simrv::memory::host_write_fast(mmem_ + (state.pte_address - dram_base_),
                                           current | state.pte_update_mask, operation);
            accept_page_walk_write(state);
        }
    }
    if (state.status == PageWalkStatus::Fault) return std::unexpected(state.fault);
    return state.physical_address;
}

}  // namespace simrv
