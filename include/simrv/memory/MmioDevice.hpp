/**
 * @file MmioDevice.hpp
 * @brief Modern typed MMIO device abstraction with direct DMA support.
 */
#pragma once

#include <cstdint>
#include <span>

#include "simrv/memory/Bus.hpp"
#include "simrv/memory/MmioTypes.hpp"
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::memory {

/**
 * @class MmioDevice
 * @brief Enhanced TileLink-attached memory-mapped I/O device base class.
 *
 * Provides typed register accessor methods (read8..read64 / write8..write64)
 * and guest physical memory DMA mastering capabilities.
 */
class MmioDevice : public TileLinkNode {
   public:
    explicit MmioDevice(simrv::core::Machine* machine = nullptr);
    ~MmioDevice() override = default;

    // Fast-path typed read/write interfaces (offset is relative to base_address)
    [[nodiscard]] virtual auto read8(MmioOffset offset) -> MmioData8;
    [[nodiscard]] virtual auto read16(MmioOffset offset) -> MmioData16;
    [[nodiscard]] virtual auto read32(MmioOffset offset) -> MmioData32;
    [[nodiscard]] virtual auto read64(MmioOffset offset) -> MmioData64;

    virtual void write8(MmioOffset offset, MmioData8 val);
    virtual void write16(MmioOffset offset, MmioData16 val);
    virtual void write32(MmioOffset offset, MmioData32 val);
    virtual void write64(MmioOffset offset, MmioData64 val);

    // TileLink channel adaptation
    auto handle_request(const TlChannelA& req, TlChannelD& resp) -> bool override;

    // Guest physical memory DMA mastering
    auto dma_read(simrv::xlen::Address paddr, std::span<uint8_t> dst) -> bool;
    auto dma_write(simrv::xlen::Address paddr, std::span<const uint8_t> src) -> bool;

    void set_machine(simrv::core::Machine* machine) { machine_ = machine; }
    [[nodiscard]] auto machine() const -> simrv::core::Machine* { return machine_; }

   protected:
    simrv::core::Machine* machine_{nullptr};
};

}  // namespace simrv::memory
