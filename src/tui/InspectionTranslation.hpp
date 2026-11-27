#pragma once

#include <optional>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/Mmu.hpp"

namespace simrv::tui {

// Inspection must not update page-table access bits.
inline auto translate_for_inspection(const simrv::core::CPU& cpu,
                                     const simrv::core::Machine& machine,
                                     simrv::xlen::VirtAddr vaddr)
    -> std::optional<simrv::xlen::PhysAddr> {
    const auto privilege = cpu.effective_data_privilege();
    if (privilege == simrv::xlen::PrivilegeLevel::Machine ||
        !simrv::xlen::satp_translation_enabled(cpu.state().satp, cpu.state().regs.xlen)) {
        if constexpr (sizeof(simrv::xlen::Word) > sizeof(uint32_t)) {
            if (cpu.state().regs.xlen == 32) vaddr &= 0xFFFFFFFFULL;
        }
        return simrv::xlen::PhysAddr{vaddr.raw()};
    }
    auto* mmu = const_cast<Mmu*>(machine.memory().mmu());
    auto result = mmu->translate(vaddr, PteAccess::Read, privilege, cpu.state().mstatus,
                                 cpu.state().satp, cpu.state().regs.xlen,
                                 /*update_access_bits=*/false);
    if (result.has_value()) return result.value();
    return std::nullopt;
}

}  // namespace simrv::tui
