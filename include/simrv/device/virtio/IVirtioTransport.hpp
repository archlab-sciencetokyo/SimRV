/**
 * @file IVirtioTransport.hpp
 * @brief Abstract transport interface for VirtIO endpoints across MMIO and PCIe.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

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
    [[nodiscard]] virtual auto dma_read_bytes(uint64_t phys_addr, std::byte* dst, std::size_t len)
        -> bool = 0;

    /// Write guest physical memory via transport DMA.
    [[nodiscard]] virtual auto dma_write_bytes(uint64_t phys_addr, const std::byte* src,
                                               std::size_t len) -> bool = 0;

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
