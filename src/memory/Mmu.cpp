/**
 * @file Mmu.cpp
 * @brief Memory Management Unit implementation delegating to PageTableWalker.
 */
#include "simrv/memory/Mmu.hpp"

#include <utility>

#include "simrv/Define.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Helpers.hpp"

namespace simrv {

auto Mmu::translate(VirtAddr v_addr, PteAccess access, PrivilegeLevel priv, CSRValue mstatus,
                    Word satp, unsigned xlen, bool update_access_bits,
                    const core::ArchState* arch_state) -> std::expected<PhysAddr, TrapCause> {
    // Machine mode or MMU disabled: use physical addressing
    if (priv == kPrivMachine || !simrv::xlen::satp_translation_enabled(satp, xlen)) {
        return PhysAddr{v_addr.raw()};
    }

    // Validate Sv39 / Sv48 canonical addresses for RV64
    if (simrv::compiler::unlikely(!memory::PageTableWalker::is_canonical(v_addr, satp, xlen))) {
        return std::unexpected(memory::PageTableWalker::page_fault_for(access));
    }

    // Translate through page tables
    return walker_.walk(v_addr, access, priv, mstatus, satp, xlen, update_access_bits, arch_state);
}

auto Mmu::page_walk(VirtAddr v_addr, PteAccess access, PrivilegeLevel priv, CSRValue mstatus,
                    Word satp, unsigned xlen, bool update_access_bits,
                    const core::ArchState* arch_state) -> std::expected<PhysAddr, TrapCause> {
    return walker_.walk(v_addr, access, priv, mstatus, satp, xlen, update_access_bits, arch_state);
}

auto Mmu::begin_page_walk(VirtAddr v_addr, PteAccess access, PrivilegeLevel priv, CSRValue mstatus,
                          Word satp, unsigned xlen, bool update_access_bits,
                          const core::ArchState* arch_state) const -> PageWalkState {
    return walker_.begin_walk(v_addr, access, priv, mstatus, satp, xlen, update_access_bits,
                              arch_state);
}

void Mmu::accept_page_walk_pte(PageWalkState& state, Word pte) const {
    walker_.accept_pte(state, pte);
}

void Mmu::accept_page_walk_write(PageWalkState& state) noexcept {
    memory::PageTableWalker::accept_write(state);
}

void Mmu::fail_page_walk_access(PageWalkState& state) noexcept {
    memory::PageTableWalker::fail_access(state);
}

}  // namespace simrv
