/**
 * @file PciDevice.hpp
 * @brief Base class for PCIe Type 0 endpoints with ECAM configuration space.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "simrv/Define.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::device {

class PcieRootComplex;

using PciVendorId = uint16_t;
using PciDeviceId = uint16_t;
using PciClassCode = uint32_t;
using BarIndex = uint8_t;

struct PciBdf {
    uint8_t bus{0};
    uint8_t dev{0};
    uint8_t func{0};

    [[nodiscard]] constexpr auto raw_address() const noexcept -> uint32_t {
        return (static_cast<uint32_t>(bus) << 20) | (static_cast<uint32_t>(dev) << 15) |
               (static_cast<uint32_t>(func) << 12);
    }
    constexpr auto operator<=>(const PciBdf&) const noexcept = default;
    constexpr bool operator==(const PciBdf&) const noexcept = default;
};

/**
 * @class PciDevice
 * @brief Base class representing a PCI/PCIe Type 0 endpoint.
 */
class PciDevice {
   public:
    PciDevice(PciVendorId vendor_id, PciDeviceId device_id, uint8_t revision_id,
              PciClassCode class_code);
    virtual ~PciDevice() = default;

    virtual void reset();

    [[nodiscard]] virtual auto config_read(Address offset, uint8_t size) -> uint32_t;
    virtual void config_write(Address offset, uint32_t val, uint8_t size);

    [[nodiscard]] virtual auto bar_read(BarIndex bar_idx, Address offset, uint8_t size) -> uint32_t;
    virtual void bar_write(BarIndex bar_idx, Address offset, uint32_t val, uint8_t size);

    [[nodiscard]] auto bar_base(BarIndex bar_idx) const -> Address;
    [[nodiscard]] auto bar_size(BarIndex bar_idx) const -> Address;
    [[nodiscard]] auto contains_mmio(Address addr, BarIndex& out_bar, Address& out_offset) const
        -> bool;

    void set_root_complex(PcieRootComplex* rc, PciBdf bdf);
    void set_root_complex(PcieRootComplex* rc, uint8_t bus, uint8_t dev, uint8_t func) {
        set_root_complex(rc, PciBdf{.bus = bus, .dev = dev, .func = func});
    }
    void trigger_irq();

    [[nodiscard]] auto bdf() const noexcept -> PciBdf { return bdf_; }
    [[nodiscard]] auto has_command_bit(PciCommandBit bit) const noexcept -> bool {
        const uint16_t cmd = static_cast<uint16_t>(config_space_[0x04]) |
                             (static_cast<uint16_t>(config_space_[0x05]) << 8);
        return (cmd & std::to_underlying(bit)) != 0;
    }

   protected:
    void init_bar(BarIndex bar_idx, Address size, bool is_64bit = false,
                  bool is_prefetchable = false);

    std::array<uint8_t, 256> config_space_{{0}};
    std::array<Address, 6> bar_sizes_{{0}};
    std::array<Address, 6> bar_masks_{{0}};
    std::array<bool, 6> bar_is_64bit_{{false}};

    PcieRootComplex* root_complex_{nullptr};
    PciBdf bdf_{};
};

}  // namespace simrv::device
