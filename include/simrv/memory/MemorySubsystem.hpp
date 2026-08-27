/**
 * @file MemorySubsystem.hpp
 * @brief Memory access and translation subsystem.
 */
#pragma once

#include <memory>

#include "simrv/memory/Mmu.hpp"
#include "simrv/memory/ReservationTable.hpp"
#include "simrv/memory/TileLinkBus.hpp"

namespace simrv::core {
class Machine;
struct MemoryGeometry;
}

namespace simrv::memory {

/**
 * @brief Top-level memory subsystem containing MMU and system bus integrations.
 */
class MemorySubsystem {
   public:
    explicit MemorySubsystem(simrv::core::Machine& machine)
        : machine_(machine), mmu_(nullptr), system_bus_(machine) {}

    ~MemorySubsystem() = default;

    /// Initialize MMU instance bound to machine CPU state
    void initialize_mmu();

    /// Get raw pointer to MMU (nullptr if uninitialized)
    [[nodiscard]] auto mmu() const -> simrv::Mmu* { return mmu_.get(); }
    /// Access system TileLink interconnect bus
    [[nodiscard]] auto system_bus() -> simrv::memory::TileLinkBus& { return system_bus_; }
    /// Access LR/SC reservation tracking table
    [[nodiscard]] auto reservation_table() -> simrv::memory::ReservationTable& {
        return reservation_table_;
    }
    [[nodiscard]] auto reservation_table() const -> const simrv::memory::ReservationTable& {
        return reservation_table_;
    }
    /// Access the immutable DRAM address range selected by the owning machine.
    [[nodiscard]] auto memory_geometry() const noexcept -> const simrv::core::MemoryGeometry&;

   private:
    simrv::core::Machine& machine_;
    std::unique_ptr<simrv::Mmu> mmu_;
    simrv::memory::TileLinkBus system_bus_;
    simrv::memory::ReservationTable reservation_table_;
};

}  // namespace simrv::memory
