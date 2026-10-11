/**
 * @file Mmu.cpp
 * @brief Memory Management Unit implementation delegating to PageTableWalker.
 */
#include "simrv/memory/Mmu.hpp"

#include <utility>

#include "simrv/CompilerHints.hpp"
#include "simrv/isa/TrapTypes.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Helpers.hpp"

namespace simrv {

auto Mmu::translate(simrv::xlen::VirtAddr v_addr, PteAccess access,
                    simrv::xlen::PrivilegeLevel priv, simrv::xlen::CSRValue mstatus,
                    simrv::xlen::Word satp, unsigned xlen, bool update_access_bits,
                    const core::ArchState* arch_state)
    -> std::expected<PageWalkResult, simrv::xlen::TrapCause> {
    // Machine mode or MMU disabled: use physical addressing directly
    if (priv == kPrivMachine || !simrv::xlen::satp_translation_enabled(satp, xlen)) {
        return PageWalkResult{.physical_address = simrv::xlen::PhysAddr{v_addr.raw()}, .pte = 0};
    }

    auto check_canonical =
        [v_addr, satp, xlen,
         access]() -> std::expected<simrv::xlen::VirtAddr, simrv::xlen::TrapCause> {
        if (simrv::compiler::unlikely(!memory::PageTableWalker::is_canonical(v_addr, satp, xlen))) {
            return std::unexpected(memory::PageTableWalker::page_fault_for(access));
        }
        return v_addr;
    };

    return check_canonical().and_then([&](simrv::xlen::VirtAddr va) {
        return walker_.walk(va, access, priv, mstatus, satp, xlen, update_access_bits, arch_state);
    });
}

auto Mmu::page_walk(simrv::xlen::VirtAddr v_addr, PteAccess access,
                    simrv::xlen::PrivilegeLevel priv, simrv::xlen::CSRValue mstatus,
                    simrv::xlen::Word satp, unsigned xlen, bool update_access_bits,
                    const core::ArchState* arch_state)
    -> std::expected<PageWalkResult, simrv::xlen::TrapCause> {
    return walker_.walk(v_addr, access, priv, mstatus, satp, xlen, update_access_bits, arch_state);
}

auto Mmu::begin_page_walk(simrv::xlen::VirtAddr v_addr, PteAccess access,
                          simrv::xlen::PrivilegeLevel priv, simrv::xlen::CSRValue mstatus,
                          simrv::xlen::Word satp, unsigned xlen, bool update_access_bits,
                          const core::ArchState* arch_state) const -> PageWalkState {
    return walker_.begin_walk(v_addr, access, priv, mstatus, satp, xlen, update_access_bits,
                              arch_state);
}

void Mmu::accept_page_walk_pte(PageWalkState& state, simrv::xlen::Word pte) const {
    walker_.accept_pte(state, pte);
}

void Mmu::accept_page_walk_write(PageWalkState& state) noexcept {
    memory::PageTableWalker::accept_write(state);
}

void Mmu::fail_page_walk_access(PageWalkState& state) noexcept {
    memory::PageTableWalker::fail_access(state);
}

}  // namespace simrv
