/**
 * @file MemorySubsystem.cpp
 * @brief Memory access and translation subsystem implementation.
 */
#include "simrv/memory/MemorySubsystem.hpp"

#include <cstring>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/TemplatedBus.hpp"

namespace simrv::memory {

MemorySubsystem::MemorySubsystem(simrv::core::Machine& machine)
    : machine_(machine),
      mmu_(nullptr),
      system_bus_(make_system_bus(machine, kDefaultTileLinkCSpec)) {}

void MemorySubsystem::configure_bus(const BusProtocolSpec& spec) {
    const bool smp = system_bus_ ? system_bus_->is_smp_enabled() : false;
    system_bus_ = make_system_bus(machine_, spec);
    if (system_bus_) {
        system_bus_->set_smp_enabled(smp);
    }
}

void MemorySubsystem::configure_bus(BusProtocol protocol) {
    if (const auto* entry = find_bus_catalog_entry(protocol)) {
        configure_bus(entry->spec);
    }
}

void MemorySubsystem::initialize_mmu() {
    if (!mmu_) {
        mmu_ = std::make_unique<simrv::Mmu>(machine_.ram_view());
    }
}

auto MemorySubsystem::dma_read(simrv::xlen::PhysAddr physical_address, void* dst, size_t len)
    -> bool {
    const auto paddr = physical_address.raw();
    if (machine_.ram_data() == nullptr || dst == nullptr || len == 0) {
        return false;
    }
    const auto geometry = memory_geometry();
    if (!geometry.contains(physical_address, len)) {
        return false;
    }
    if (!machine_.runtime_profile.is_instruction_mode() && system_bus_->has_coherence()) {
        const simrv::xlen::Address first_line =
            paddr & ~(simrv::xlen::Address{CoherenceHub::kLineBytes - 1u});
        const simrv::xlen::Address last_line =
            (paddr + len - 1u) & ~(simrv::xlen::Address{CoherenceHub::kLineBytes - 1u});
        for (simrv::xlen::Address line = first_line;; line += CoherenceHub::kLineBytes) {
            system_bus_->coherence_hub().invalidate_line_external(simrv::xlen::PhysAddr{line});
            if (line == last_line) break;
        }
    }
    std::memcpy(dst, machine_.ram_data() + (paddr - geometry.dram_base.raw()), len);
    return true;
}

auto MemorySubsystem::dma_write(simrv::xlen::PhysAddr physical_address, const void* src, size_t len)
    -> bool {
    const auto paddr = physical_address.raw();
    if (machine_.ram_data() == nullptr || src == nullptr || len == 0) {
        return false;
    }
    const auto geometry = memory_geometry();
    if (!geometry.contains(physical_address, len)) {
        return false;
    }
    if (!machine_.runtime_profile.is_instruction_mode() && system_bus_->has_coherence()) {
        const simrv::xlen::Address first_line =
            paddr & ~(simrv::xlen::Address{CoherenceHub::kLineBytes - 1u});
        const simrv::xlen::Address last_line =
            (paddr + len - 1u) & ~(simrv::xlen::Address{CoherenceHub::kLineBytes - 1u});
        for (simrv::xlen::Address line = first_line;; line += CoherenceHub::kLineBytes) {
            system_bus_->coherence_hub().invalidate_line_external(simrv::xlen::PhysAddr{line});
            if (line == last_line) break;
        }
    }
    // DMA is an external write in every execution mode, including instruction-fast.
    reservation_table_.invalidate_range(physical_address, len);
    std::memcpy(machine_.ram_data() + (paddr - geometry.dram_base.raw()), src, len);
    return true;
}

}  // namespace simrv::memory
