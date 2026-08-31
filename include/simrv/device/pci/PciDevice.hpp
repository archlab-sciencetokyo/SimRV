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

/**
 * @class PciDevice
 * @brief Base class representing a PCI/PCIe Type 0 endpoint.
 */
class PciDevice {
   public:
    PciDevice(uint16_t vendor_id, uint16_t device_id, uint8_t revision_id, uint32_t class_code);
    virtual ~PciDevice() = default;

    virtual void reset();

    [[nodiscard]] virtual auto config_read(Address offset, uint8_t size) -> uint32_t;
    virtual void config_write(Address offset, uint32_t val, uint8_t size);

    [[nodiscard]] virtual auto bar_read(int bar_idx, Address offset, uint8_t size) -> uint32_t;
    virtual void bar_write(int bar_idx, Address offset, uint32_t val, uint8_t size);

    [[nodiscard]] auto bar_base(int bar_idx) const -> Address;
    [[nodiscard]] auto bar_size(int bar_idx) const -> Address;
    [[nodiscard]] auto contains_mmio(Address addr, int& out_bar, Address& out_offset) const -> bool;

    void set_root_complex(PcieRootComplex* rc, uint8_t bus, uint8_t dev, uint8_t func);
    void trigger_irq();

   protected:
    void init_bar(int bar_idx, Address size, bool is_64bit = false, bool is_prefetchable = false);

    std::array<uint8_t, 256> config_space_{{0}};
    std::array<Address, 6> bar_sizes_{{0}};
    std::array<Address, 6> bar_masks_{{0}};
    std::array<bool, 6> bar_is_64bit_{{false}};

    PcieRootComplex* root_complex_{nullptr};
    uint8_t bus_{0};
    uint8_t dev_{0};
    uint8_t func_{0};
};

}  // namespace simrv::device
