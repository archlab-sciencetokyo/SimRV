/**
 * @file MemorySubsystem.hpp
 * @brief Memory access and translation subsystem.
 */
#pragma once

#include <memory>

#include "simrv/memory/Mmu.hpp"
#include "simrv/memory/TileLinkBus.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::memory {

class MemorySubsystem {
   public:
    explicit MemorySubsystem(simrv::core::Machine& machine)
        : machine_(machine), mmu_(nullptr), system_bus_(machine) {}

    ~MemorySubsystem() = default;

    /// Initialize MMU after CPU is constructed
    void initialize_mmu();

    [[nodiscard]] auto mmu() const -> simrv::Mmu* { return mmu_.get(); }
    [[nodiscard]] auto system_bus() -> simrv::memory::TileLinkBus& { return system_bus_; }

   private:
    simrv::core::Machine& machine_;
    std::unique_ptr<simrv::Mmu> mmu_;
    simrv::memory::TileLinkBus system_bus_;
};

}  // namespace simrv::memory
