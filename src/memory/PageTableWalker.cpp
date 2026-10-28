/**
 * @file PageTableWalker.cpp
 * @brief Dedicated hardware page-table walk engine implementation for Sv32, Sv39, and Sv48.
 */
#include "simrv/memory/PageTableWalker.hpp"

#include <utility>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Pmp.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Constants.hpp"

namespace simrv::memory {

using core::MstatusBit;
using isa::Funct3;

auto PageTableWalker::pte_access_valid(simrv::xlen::PhysAddr address, unsigned size) const noexcept
    -> bool {
    return ram_.contains(address.raw(), size);
}

auto PageTableWalker::page_fault_for(PteAccess access) noexcept -> simrv::xlen::TrapCause {
    switch (access) {
        case PteAccess::Code:
            return to_trap_cause(ExceptionCode::FetchPageFault);
        case PteAccess::Write:
            return to_trap_cause(ExceptionCode::StorePageFault);
        case PteAccess::Read:
        default:
            return to_trap_cause(ExceptionCode::LoadPageFault);
    }
}

auto PageTableWalker::access_fault_for(PteAccess access) noexcept -> simrv::xlen::TrapCause {
    switch (access) {
        case PteAccess::Code:
            return to_trap_cause(ExceptionCode::FaultFetch);
        case PteAccess::Write:
            return to_trap_cause(ExceptionCode::FaultStore);
        case PteAccess::Read:
        default:
            return to_trap_cause(ExceptionCode::FaultLoad);
    }
}

void PageTableWalker::fail_access(PageWalkState& state) noexcept {
    state.fault = access_fault_for(state.access);
    state.status = PageWalkStatus::Fault;
}

void PageTableWalker::accept_write(PageWalkState& state) noexcept {
    if (state.status == PageWalkStatus::WritePte) {
        state.status = PageWalkStatus::Complete;
    }
}

void PageTableWalker::select_next_pte(PageWalkState& state,
                                      simrv::xlen::PhysAddr table_address) const {
    const simrv::xlen::Word vpn_mask =
        (static_cast<simrv::xlen::Word>(1) << state.vpn_bits_per_level) - 1;
    const simrv::xlen::Word vpn =
        (state.virtual_address.raw() >>
         (12 + state.level * static_cast<int>(state.vpn_bits_per_level))) &
        vpn_mask;
    state.pte_address = table_address + vpn * state.pte_size;
    // Implicit page table accesses evaluate under Supervisor privilege per RISC-V Privileged spec
    if (!pte_access_valid(state.pte_address, state.pte_size) ||
        (state.arch_state != nullptr &&
         !core::pmp::check_access(*state.arch_state, state.pte_address, state.pte_size,
                                  core::PmpAccessType::Read,
                                  simrv::xlen::PrivilegeLevel::Supervisor))) {
        fail_access(state);
        return;
    }
    state.status = PageWalkStatus::ReadPte;
}

auto PageTableWalker::begin_walk(simrv::xlen::VirtAddr v_addr, PteAccess access,
                                 simrv::xlen::PrivilegeLevel priv, simrv::xlen::CsrValue mstatus,
                                 simrv::xlen::Word satp, unsigned xlen, bool update_access_bits,
                                 const core::ArchState* arch_state) const -> PageWalkState {
    PageWalkState state{.virtual_address = v_addr,
                        .mstatus = mstatus,
                        .access = access,
                        .privilege = priv,
                        .fault = page_fault_for(access),
                        .xlen = xlen,
                        .update_access_bits = update_access_bits,
                        .arch_state = arch_state};
    if (priv == kPrivMachine || !simrv::xlen::satp_translation_enabled(satp, xlen)) {
        state.physical_address = simrv::xlen::PhysAddr{v_addr.raw()};
        state.status = PageWalkStatus::Complete;
        return state;
    }
    if (!is_canonical(v_addr, satp, xlen)) {
        return state;
    }

    const simrv::xlen::Word mode = simrv::xlen::satp_mode(satp, xlen);
    int levels = 0;
    if (xlen == 32 && mode == 1) {
        levels = 2;
        state.pte_size = 4;
        state.vpn_bits_per_level = 10;
    } else if (xlen == 64 && (mode == 8 || mode == 9 || mode == 10)) {
        levels = mode == 8 ? 3 : (mode == 9 ? 4 : 5);
        state.pte_size = 8;
        state.vpn_bits_per_level = 9;
    } else {
        return state;
    }
    state.level = levels - 1;
    const simrv::xlen::PhysAddr root = simrv::xlen::PhysAddr{
        static_cast<simrv::xlen::Word>(simrv::xlen::satp_root_ppn(satp, xlen) << 12)};
    state.satp_root = root.raw();

    if (xlen == 64 && mode == 8) {
        // Sv39 Page Directory Cache lookup: 2 MiB region (VPN[2..1], bits 38:21)
        const simrv::xlen::Word vpn_prefix = (v_addr.raw() >> 21) & 0x3FFFFU;
        const size_t pdc_idx = (vpn_prefix ^ (vpn_prefix >> 8)) & (kPdcSize - 1);
        const auto& entry = pdc_[pdc_idx];
        if (entry.epoch == pdc_epoch_ && entry.satp_root == state.satp_root &&
            entry.vpn_prefix == vpn_prefix) {
            state.level = 0;
            select_next_pte(state, entry.table_address);
            return state;
        }
    }

    select_next_pte(state, root);
    return state;
}

void PageTableWalker::accept_pte(PageWalkState& state, simrv::xlen::Word pte) const {
    if (state.status != PageWalkStatus::ReadPte) {
        return;
    }
    if (state.pte_size == 4) {
        pte &= 0xFFFFFFFFu;
    }
    state.pte = pte;
    const PteView view{.raw = pte};
    if (!view.valid() || view.has_reserved_bits(state.pte_size) || view.is_reserved_encoding()) {
        state.fault = page_fault_for(state.access);
        state.status = PageWalkStatus::Fault;
        return;
    }

    if (view.is_table_pointer()) {
        if (view.is_napot() || view.table_pointer_has_reserved_bits() || state.level == 0) {
            state.fault = page_fault_for(state.access);
            state.status = PageWalkStatus::Fault;
            return;
        }
        --state.level;
        const simrv::xlen::Word ppn_mask =
            state.pte_size == 8 ? static_cast<simrv::xlen::Word>((uint64_t{1} << 44) - 1)
                                : static_cast<simrv::xlen::Word>((uint32_t{1} << 22) - 1);
        const simrv::xlen::PhysAddr next_table = simrv::xlen::PhysAddr{
            static_cast<simrv::xlen::Word>(((pte >> kPteShift) & ppn_mask) << 12)};

        // Cache the Level-0 table address in the Page Directory Cache for Sv39 walks
        if (state.xlen == 64 && state.level == 0 && state.vpn_bits_per_level == 9) {
            const simrv::xlen::Word vpn_prefix = (state.virtual_address.raw() >> 21) & 0x3FFFFU;
            const size_t pdc_idx = (vpn_prefix ^ (vpn_prefix >> 8)) & (kPdcSize - 1);
            pdc_[pdc_idx] = PdcEntry{.satp_root = state.satp_root,
                                     .vpn_prefix = vpn_prefix,
                                     .table_address = next_table,
                                     .epoch = pdc_epoch_};
        }

        select_next_pte(state, next_table);
        return;
    }

    // Svnapot standardizes 64 KiB leaf mappings in the level-0 PTE for Sv39/Sv48/Sv57.
    // PPN[3:0]=1000 is the encoding marker; the implicit PTE value substitutes VPN[0][3:0]
    // into those bits to select one of the sixteen contiguous 4 KiB physical pages.
    if (view.is_napot() &&
        (state.pte_size != 8 || state.level != 0 || !view.has_valid_napot_ppn_encoding())) {
        state.fault = page_fault_for(state.access);
        state.status = PageWalkStatus::Fault;
        return;
    }

    const bool mxr = (state.mstatus & MstatusBit::Mxr) != 0u;
    const bool pte_r = view.readable();
    const bool pte_w = view.writable();
    const bool pte_x = view.executable();
    const simrv::xlen::Word permissions = (static_cast<simrv::xlen::Word>(pte_x) << 2) |
                                          (static_cast<simrv::xlen::Word>(pte_w) << 1) |
                                          static_cast<simrv::xlen::Word>(pte_r || (mxr && pte_x));
    if (!validate_pte_permissions(pte, permissions, state.access, state.privilege, state.mstatus)) {
        state.fault = page_fault_for(state.access);
        state.status = PageWalkStatus::Fault;
        return;
    }

    const simrv::xlen::Word low_ppn_mask =
        state.level == 0
            ? 0
            : (static_cast<simrv::xlen::Word>(1) << (state.level * state.vpn_bits_per_level)) - 1;
    const simrv::xlen::Word ppn_mask =
        state.pte_size == 8 ? static_cast<simrv::xlen::Word>((uint64_t{1} << 44) - 1)
                            : static_cast<simrv::xlen::Word>((uint32_t{1} << 22) - 1);
    simrv::xlen::Word ppn = (pte >> kPteShift) & ppn_mask;
    if (view.is_napot()) {
        const simrv::xlen::Word vpn0 = (state.virtual_address.raw() >> 12U) & 0xFU;
        ppn = (ppn & ~simrv::xlen::Word{0xF}) | vpn0;
    }
    if ((ppn & low_ppn_mask) != 0) {
        state.fault = page_fault_for(state.access);
        state.status = PageWalkStatus::Fault;
        return;
    }
    const unsigned offset_bits = 12 + state.level * state.vpn_bits_per_level;
    const simrv::xlen::Word offset_mask = (static_cast<simrv::xlen::Word>(1) << offset_bits) - 1;
    state.physical_address = simrv::xlen::PhysAddr{(state.virtual_address.raw() & offset_mask) |
                                                   ((ppn << 12) & ~offset_mask)};

    if (state.arch_state != nullptr) {
        const auto pmp_access = state.access == PteAccess::Code ? core::PmpAccessType::Execute
                                                                : (state.access == PteAccess::Write
                                                                       ? core::PmpAccessType::Write
                                                                       : core::PmpAccessType::Read);
        if (!core::pmp::check_access(*state.arch_state, state.physical_address, 1, pmp_access,
                                     state.privilege)) {
            state.fault = access_fault_for(state.access);
            state.status = PageWalkStatus::Fault;
            return;
        }
    }

    simrv::xlen::Word updated = pte | PteFlag::A;
    if (state.access == PteAccess::Write) {
        updated |= PteFlag::D;
    }
    if (state.update_access_bits && updated != pte) {
        if (state.arch_state != nullptr &&
            !core::pmp::check_access(*state.arch_state, state.pte_address, state.pte_size,
                                     core::PmpAccessType::Write,
                                     simrv::xlen::PrivilegeLevel::Supervisor)) {
            fail_access(state);
            return;
        }
        state.pte = updated;
        state.pte_update_mask = updated ^ pte;
        state.status = PageWalkStatus::WritePte;
    } else {
        state.status = PageWalkStatus::Complete;
    }
}

auto PageTableWalker::validate_pte_permissions(simrv::xlen::Word pte,
                                               simrv::xlen::Word permission_bits, PteAccess access,
                                               simrv::xlen::PrivilegeLevel priv,
                                               simrv::xlen::CsrValue mstatus) const noexcept
    -> bool {
    // XWR field must not be reserved values: Write-Only (2) or Write-Execute (6)
    constexpr simrv::xlen::Word kPermWriteOnly = 2;
    constexpr simrv::xlen::Word kPermWriteExecute = 6;
    const PteView view{.raw = pte};
    const simrv::xlen::Word original_rwx = (pte >> 1) & 0x7;
    if (original_rwx == kPermWriteOnly || original_rwx == kPermWriteExecute) {
        return false;
    }

    const bool pte_u = view.user();
    // Supervisor access to user-only page (U=1)
    if (priv == kPrivSupervisor && pte_u) {
        if (access == PteAccess::Code) {
            // Executing code from a U=1 page in Supervisor mode raises a Page Fault regardless of
            // SUM
            return false;
        }
        if ((mstatus & MstatusBit::Sum) == 0u) {
            return false;
        }
    }

    // User accessing supervisor page
    if (priv == kPrivUser && !pte_u) {
        return false;
    }

    // Check access permissions
    if (((permission_bits >> std::to_underlying(access)) & 1) == 0) {
        return false;
    }

    return true;
}

auto PageTableWalker::walk(simrv::xlen::VirtAddr v_addr, PteAccess access,
                           simrv::xlen::PrivilegeLevel priv, simrv::xlen::CsrValue mstatus,
                           simrv::xlen::Word satp, unsigned xlen, bool update_access_bits,
                           const core::ArchState* arch_state) const
    -> std::expected<PageWalkResult, simrv::xlen::TrapCause> {
    auto state =
        begin_walk(v_addr, access, priv, mstatus, satp, xlen, update_access_bits, arch_state);
    while (state.status == PageWalkStatus::ReadPte || state.status == PageWalkStatus::WritePte) {
        const simrv::xlen::Instruction operation =
            state.pte_size == 4
                ? static_cast<simrv::xlen::Instruction>(
                      state.status == PageWalkStatus::ReadPte ? Funct3::Lw : Funct3::Sw)
                : static_cast<simrv::xlen::Instruction>(
                      state.status == PageWalkStatus::ReadPte ? Funct3::Ld : Funct3::Sd);
        simrv::xlen::Byte* pte_ptr = ram_.unchecked_ptr(state.pte_address.raw());
        if (state.status == PageWalkStatus::ReadPte) {
            const simrv::xlen::Word pte = simrv::memory::host_read_fast(pte_ptr, operation);
            accept_pte(state, pte);
        } else {
            const simrv::xlen::Word current = simrv::memory::host_read_fast(
                pte_ptr, state.pte_size == 4 ? static_cast<simrv::xlen::Instruction>(Funct3::Lw)
                                             : static_cast<simrv::xlen::Instruction>(Funct3::Ld));
            simrv::memory::host_write_fast(pte_ptr, current | state.pte_update_mask, operation);
            accept_write(state);
        }
    }
    if (state.status == PageWalkStatus::Fault) {
        return std::unexpected(state.fault);
    }
    return PageWalkResult{.physical_address = state.physical_address, .pte = state.pte};
}

}  // namespace simrv::memory
