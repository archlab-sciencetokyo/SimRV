/**
 * @file MemorySubsystem.hpp
 * @brief Memory access and translation subsystem.
 */
#pragma once

#include <memory>

#include "simrv/memory/Bus.hpp"
#include "simrv/memory/BusProtocolSpec.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/memory/ReservationTable.hpp"

namespace simrv::core {
class Machine;
struct MemoryGeometry;
}  // namespace simrv::core

namespace simrv::memory {

/**
 * @brief Top-level memory subsystem containing MMU and system bus integrations.
 */
class MemorySubsystem {
   public:
    explicit MemorySubsystem(simrv::core::Machine& machine);

    ~MemorySubsystem() = default;

    /// Reconfigure the system interconnect bus protocol
    void configure_bus(const BusProtocolSpec& spec);
    void configure_bus(BusProtocol protocol);

    /// Initialize MMU instance bound to machine CPU state
    void initialize_mmu();

    /// Get raw pointer to MMU (nullptr if uninitialized)
    [[nodiscard]] auto mmu() const -> simrv::Mmu* { return mmu_.get(); }
    /// Access system interconnect bus
    [[nodiscard]] auto system_bus() -> simrv::memory::Bus& { return *system_bus_; }
    [[nodiscard]] auto system_bus() const -> const simrv::memory::Bus& { return *system_bus_; }
    /// Access LR/SC reservation tracking table
    [[nodiscard]] auto reservation_table() -> simrv::memory::ReservationTable& {
        return reservation_table_;
    }
    [[nodiscard]] auto reservation_table() const -> const simrv::memory::ReservationTable& {
        return reservation_table_;
    }
    /// Access the immutable DRAM address range selected by the owning machine.
    [[nodiscard]] auto memory_geometry() const noexcept -> const simrv::core::MemoryGeometry&;

    /// Direct DMA read from guest physical memory with cache line invalidation
    auto dma_read(simrv::xlen::PhysAddr paddr, void* dst, size_t len) -> bool;
    template <std::integral I>
    auto dma_read(I, void*, size_t) -> bool = delete;
    /// Direct DMA write to guest physical memory with cache line invalidation
    auto dma_write(simrv::xlen::PhysAddr paddr, const void* src, size_t len) -> bool;
    template <std::integral I>
    auto dma_write(I, const void*, size_t) -> bool = delete;

   private:
    simrv::core::Machine& machine_;
    std::unique_ptr<simrv::Mmu> mmu_;
    std::unique_ptr<simrv::memory::Bus> system_bus_;
    simrv::memory::ReservationTable reservation_table_;
};

}  // namespace simrv::memory
