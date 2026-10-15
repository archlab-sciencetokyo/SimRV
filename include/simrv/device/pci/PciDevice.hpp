/**
 * @file PciDevice.hpp
 * @brief Base class for PCIe Type 0 endpoints with ECAM configuration space.
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "simrv/device/pci/PciTypes.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::device {

class PcieRootComplex;

/**
 * @class PciDevice
 * @brief Base class representing a PCI/PCIe Type 0 endpoint.
 */
class PciDevice {
   public:
    PciDevice(PciVendorId vendor_id, PciDeviceId device_id, PciRevisionId revision_id,
              PciClassCode class_code);
    virtual ~PciDevice() = default;

    virtual void reset();

    [[nodiscard]] virtual auto config_read(PciConfigOffset offset, PciAccessBytes size)
        -> PciConfigData;
    virtual void config_write(PciConfigOffset offset, PciConfigData val, PciAccessBytes size);

    [[nodiscard]] virtual auto bar_read(PciBarIndex bar_idx, PciBarOffset offset,
                                        PciAccessBytes size) -> PciMmioData;
    virtual void bar_write(PciBarIndex bar_idx, PciBarOffset offset, PciMmioData val,
                           PciAccessBytes size);

    [[nodiscard]] auto bar_base(PciBarIndex bar_idx) const -> simrv::xlen::Address;
    [[nodiscard]] auto bar_size(PciBarIndex bar_idx) const -> PciBarSize;
    [[nodiscard]] auto bar_is_64bit(PciBarIndex bar_idx) const noexcept -> bool {
        return bar_idx < 6 && bar_is_64bit_[bar_idx];
    }
    [[nodiscard]] auto contains_mmio(simrv::xlen::Address addr, PciBarIndex& out_bar,
                                     PciBarOffset& out_offset) const -> bool;

    void set_root_complex(PcieRootComplex* rc, PciBdf bdf);
    void set_root_complex(PcieRootComplex* rc, PciBusNumber bus, PciDeviceNumber dev,
                          PciFunctionNumber func) {
        set_root_complex(rc, PciBdf{.bus = bus, .dev = dev, .func = func});
    }
    void trigger_irq();
    void deassert_irq();

    void init_msi(uint8_t cap_offset = 0x48, bool is_64bit = true);
    [[nodiscard]] auto is_msi_enabled() const noexcept -> bool;
    [[nodiscard]] auto msi_address() const noexcept -> simrv::xlen::Address;
    [[nodiscard]] auto msi_data() const noexcept -> uint16_t;

    void init_msix(uint8_t cap_offset, PciBarIndex table_bar, uint32_t table_offset,
                   PciBarIndex pba_bar, uint32_t pba_offset, uint16_t table_size = 4);
    [[nodiscard]] auto is_msix_enabled() const noexcept -> bool;

    void trigger_msi(uint16_t vector = 0);

    virtual void on_bar_updated(PciBarIndex bar_idx);

    [[nodiscard]] auto bdf() const noexcept -> PciBdf { return bdf_; }
    [[nodiscard]] auto has_command_bit(PciCommandBit bit) const noexcept -> bool {
        const uint16_t cmd = static_cast<uint16_t>(config_space_[0x04]) |
                             (static_cast<uint16_t>(config_space_[0x05]) << 8);
        return (cmd & std::to_underlying(bit)) != 0;
    }

   protected:
    void init_bar(PciBarIndex bar_idx, PciBarSize size, bool is_64bit = false,
                  bool is_prefetchable = false);

    std::array<uint8_t, 256> config_space_{{0}};
    std::array<simrv::xlen::Address, 6> bar_bases_{{0}};
    std::array<PciBarSize, 6> bar_sizes_{{0}};
    std::array<PciBarMask, 6> bar_masks_{{0}};
    std::array<bool, 6> bar_is_64bit_{{false}};

    uint8_t msi_cap_offset_{0};
    uint8_t msix_cap_offset_{0};

    PcieRootComplex* root_complex_{nullptr};
    PciBdf bdf_{};
};

}  // namespace simrv::device
