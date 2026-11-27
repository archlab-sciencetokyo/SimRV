/**
 * @file IVirtioTransport.hpp
 * @brief Abstract transport interface for VirtIO endpoints across MMIO and PCIe.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>

#include "simrv/xlen/AddressTypes.hpp"

namespace simrv::device::virtio {

/**
 * @class IVirtioTransport
 * @brief Abstract interface bridging virtqueue operations to underlying bus transports (MMIO or
 * PCI).
 */
class IVirtioTransport {
   public:
    virtual ~IVirtioTransport() = default;

    /// Read guest physical memory via transport DMA.
    [[nodiscard]] virtual auto dma_read_bytes(simrv::xlen::PhysAddr phys_addr, std::byte* dst,
                                              std::size_t len) -> bool = 0;

    /// Write guest physical memory via transport DMA.
    [[nodiscard]] virtual auto dma_write_bytes(simrv::xlen::PhysAddr phys_addr,
                                               const std::byte* src, std::size_t len) -> bool = 0;

    /// Adapt a 64-bit guest descriptor/register address to the host-width physical domain.
    /// VirtIO wire addresses stay 64-bit; reject values that cannot be represented by this build.
    auto dma_read_wire_address(uint64_t phys_addr, void* dst, std::size_t len) -> bool {
        if (!std::in_range<simrv::xlen::Word>(phys_addr)) return false;
        return dma_read_bytes(simrv::xlen::PhysAddr{static_cast<simrv::xlen::Word>(phys_addr)},
                              reinterpret_cast<std::byte*>(dst), len);
    }

    auto dma_write_wire_address(uint64_t phys_addr, const void* src, std::size_t len) -> bool {
        if (!std::in_range<simrv::xlen::Word>(phys_addr)) return false;
        return dma_write_bytes(simrv::xlen::PhysAddr{static_cast<simrv::xlen::Word>(phys_addr)},
                               reinterpret_cast<const std::byte*>(src), len);
    }

    /// Assert transport-level interrupt (e.g. MMIO line or PCIe INTx/MSI).
    virtual void trigger_irq() = 0;

    /// Check if simulator is running in cycle-accurate mode.
    [[nodiscard]] virtual auto is_cycle_mode() const noexcept -> bool { return false; }

    /// Check if asynchronous DMA latency modeling is active.
    [[nodiscard]] virtual auto is_dma_enabled() const noexcept -> bool { return false; }

    /// Schedule a cycle-deferred completion if DMA latency modeling is active.
    virtual void schedule_deferred_dma(size_t transfer_bytes,
                                       std::function<void()> completion_callback) {
        (void)transfer_bytes;
        if (completion_callback) completion_callback();
    }
};

}  // namespace simrv::device::virtio
