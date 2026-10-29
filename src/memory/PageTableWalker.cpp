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
    state.pte_guest_address = (table_address + vpn * state.pte_size).raw();
    state.pte_address = simrv::xlen::PhysAddr{state.pte_guest_address};
    if (state.guest_stage_enabled) {
        auto translated = walk_guest_physical(state.pte_guest_address, PteAccess::Read,
                                              state.hgatp, state.xlen);
        if (!translated) {
            state.fault = translated.error();
            state.status = PageWalkStatus::Fault;
            return;
        }
        state.pte_address = translated->physical_address;
    }
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
    if (arch_state != nullptr && arch_state->virt && xlen == 64) {
        state.guest_stage_enabled = true;
        state.hgatp = arch_state->hgatp;
    }
    if (priv == kPrivMachine || !simrv::xlen::satp_translation_enabled(satp, xlen)) {
        state.physical_address = simrv::xlen::PhysAddr{v_addr.raw()};
        if (state.guest_stage_enabled && priv != kPrivMachine) {
            auto translated = walk_guest_physical(v_addr.raw(), access, state.hgatp, xlen);
            if (!translated) {
                state.fault = translated.error();
                return state;
            }
            state.physical_address = translated->physical_address;
        }
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
        if (view.is_napot() || view.pbmt() != PtePbmt::Pma ||
            view.table_pointer_has_reserved_bits() || state.level == 0) {
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
    const simrv::xlen::PhysAddr stage_one_address{
        (state.virtual_address.raw() & offset_mask) | ((ppn << 12) & ~offset_mask)};
    state.physical_address = stage_one_address;
    if (state.guest_stage_enabled) {
        auto translated = walk_guest_physical(stage_one_address.raw(), state.access, state.hgatp,
                                              state.xlen);
        if (!translated) {
            state.fault = translated.error();
            state.status = PageWalkStatus::Fault;
            return;
        }
        state.physical_address = translated->physical_address;
    }

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
    if (updated != pte && !state.update_access_bits) {
        // Svade requires a page fault when software has not set a required
        // accessed or dirty bit. Hardware A/D updates are reserved for an
        // implementation that exposes Svadu and enables menvcfg.ADUE.
        state.fault = page_fault_for(state.access);
        state.status = PageWalkStatus::Fault;
        return;
    }
    if (updated != pte) {
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

auto PageTableWalker::walk_guest_physical(simrv::xlen::Address guest_address, PteAccess access,
                                          simrv::xlen::Word hgatp, unsigned xlen) const
    -> std::expected<PageWalkResult, simrv::xlen::TrapCause> {
    const auto guest_page_fault = [access]() -> simrv::xlen::TrapCause {
        using simrv::isa::ExceptionCode;
        switch (access) {
            case PteAccess::Code:
                return simrv::isa::to_trap_cause(ExceptionCode::GuestInstructionPageFault);
            case PteAccess::Write:
                return simrv::isa::to_trap_cause(ExceptionCode::GuestStorePageFault);
            case PteAccess::Read:
            default:
                return simrv::isa::to_trap_cause(ExceptionCode::GuestLoadPageFault);
        }
    };
    const simrv::xlen::Word mode = simrv::xlen::satp_mode(hgatp, xlen);
    if (mode == 0) {
        return PageWalkResult{.physical_address = simrv::xlen::PhysAddr{guest_address}, .pte = 0};
    }
    if (xlen != 64 || (mode != 8 && mode != 9 && mode != 10)) {
        return std::unexpected(guest_page_fault());
    }

    // SvNNx4 adds two GPA bits to the root-level VPN, so the root table has 2048 entries and
    // its 16 KiB base address must be aligned. Lower levels retain the ordinary 512-entry shape.
    const unsigned levels = mode == 8 ? 3U : (mode == 9 ? 4U : 5U);
    const unsigned guest_address_bits = 12U + 9U * (levels - 1U) + 11U;
    if ((guest_address >> guest_address_bits) != 0) {
        return std::unexpected(guest_page_fault());
    }
    const simrv::xlen::Word root_ppn = (hgatp >> 0U) & ((uint64_t{1} << 44U) - 1U);
    if ((root_ppn & 0x3U) != 0) {
        return std::unexpected(guest_page_fault());
    }
    simrv::xlen::Word table_address = root_ppn << 12U;
    simrv::xlen::Word leaf_pte = 0;
    int level = static_cast<int>(levels) - 1;
    for (;;) {
        const unsigned index_bits = level == static_cast<int>(levels) - 1 ? 11U : 9U;
        const unsigned shift = 12U + 9U * static_cast<unsigned>(level);
        const simrv::xlen::Word index_mask = (simrv::xlen::Word{1} << index_bits) - 1U;
        const simrv::xlen::Word index = (guest_address >> shift) & index_mask;
        const simrv::xlen::Word pte_address = table_address + index * sizeof(uint64_t);
        if (!ram_.contains(pte_address, sizeof(uint64_t))) {
            return std::unexpected(access_fault_for(access));
        }
        const auto* pte_ptr = ram_.unchecked_ptr(pte_address);
        const auto pte = simrv::memory::host_read_fast(
            pte_ptr, static_cast<simrv::xlen::Instruction>(Funct3::Ld));
        const PteView view{.raw = pte};
        if (!view.valid() || view.has_reserved_bits(8) || view.is_reserved_encoding()) {
            return std::unexpected(guest_page_fault());
        }
        if (view.is_table_pointer()) {
            if (view.is_napot() || view.pbmt() != PtePbmt::Pma ||
                view.table_pointer_has_reserved_bits() || level == 0) {
                return std::unexpected(guest_page_fault());
            }
            table_address = view.ppn() << 12U;
            --level;
            continue;
        }

        const simrv::xlen::Word rwx = (pte >> 1U) & 0x7U;
        if (rwx == 0x2U || rwx == 0x6U || (rwx & (1U << std::to_underlying(access))) == 0) {
            return std::unexpected(guest_page_fault());
        }
        if (view.is_napot() && (level != 0 || !view.has_valid_napot_ppn_encoding())) {
            return std::unexpected(guest_page_fault());
        }
        const unsigned page_ppn_bits = 9U * static_cast<unsigned>(level);
        const simrv::xlen::Word low_ppn_mask =
            page_ppn_bits == 0 ? 0 : (simrv::xlen::Word{1} << page_ppn_bits) - 1U;
        simrv::xlen::Word ppn = view.ppn();
        if ((ppn & low_ppn_mask) != 0) {
            return std::unexpected(guest_page_fault());
        }
        if (!view.accessed() || (access == PteAccess::Write && !view.dirty())) {
            // RVA23 requires Svade and does not advertise Svadu, so guest-stage A/D faults
            // remain software-managed and are reported as guest-page faults.
            return std::unexpected(guest_page_fault());
        }
        if (view.is_napot()) {
            const simrv::xlen::Word vpn0 = (guest_address >> 12U) & 0xFU;
            ppn = (ppn & ~simrv::xlen::Word{0xFU}) | vpn0;
        }
        const unsigned offset_bits = 12U + page_ppn_bits;
        const simrv::xlen::Word offset_mask = (simrv::xlen::Word{1} << offset_bits) - 1U;
        const simrv::xlen::Word physical =
            (guest_address & offset_mask) | ((ppn << 12U) & ~offset_mask);
        leaf_pte = pte;
        return PageWalkResult{.physical_address = simrv::xlen::PhysAddr{physical}, .pte = leaf_pte};
    }
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
