/**
 * @file PciDevice.cpp
 * @brief Implementation of PCI Type 0 endpoint base class.
 */
#include "simrv/device/pci/PciDevice.hpp"

#include <cstring>

#include "simrv/device/pci/PcieRootComplex.hpp"

namespace simrv::device {

PciDevice::PciDevice(PciVendorId vendor_id, PciDeviceId device_id, PciRevisionId revision_id,
                     PciClassCode class_code) {
    config_space_[0x00] = static_cast<uint8_t>(vendor_id & 0xff);
    config_space_[0x01] = static_cast<uint8_t>((vendor_id >> 8) & 0xff);
    config_space_[0x02] = static_cast<uint8_t>(device_id & 0xff);
    config_space_[0x03] = static_cast<uint8_t>((device_id >> 8) & 0xff);

    config_space_[0x08] = revision_id;
    config_space_[0x09] = static_cast<uint8_t>(class_code & 0xff);
    config_space_[0x0A] = static_cast<uint8_t>((class_code >> 8) & 0xff);
    config_space_[0x0B] = static_cast<uint8_t>((class_code >> 16) & 0xff);

    config_space_[0x0E] = 0x00;  // Type 0 header
    config_space_[0x3D] = 0x01;  // INTA#
}

void PciDevice::reset() {
    for (int i = 0; i < 6; ++i) {
        config_space_[0x10 + i * 4] = 0;
        config_space_[0x11 + i * 4] = 0;
        config_space_[0x12 + i * 4] = 0;
        config_space_[0x13 + i * 4] = 0;
        bar_bases_[i] = 0;
    }
    config_space_[0x04] = 0;
    config_space_[0x05] = 0;
    if (root_complex_ != nullptr) {
        root_complex_->rebuild_bar_mappings();
    }
}

void PciDevice::set_root_complex(PcieRootComplex* rc, PciBdf bdf) {
    root_complex_ = rc;
    bdf_ = bdf;
}

void PciDevice::trigger_irq() {
    if (root_complex_ != nullptr) {
        root_complex_->assert_device_irq(bdf_.bus, bdf_.dev, bdf_.func);
    }
}

void PciDevice::deassert_irq() {
    if (root_complex_ != nullptr) {
        root_complex_->deassert_device_irq(bdf_.bus, bdf_.dev, bdf_.func);
    }
}

void PciDevice::on_bar_updated(PciBarIndex bar_idx) {
    (void)bar_idx;
    if (root_complex_ != nullptr) {
        root_complex_->rebuild_bar_mappings();
    }
}

void PciDevice::init_bar(PciBarIndex bar_idx, PciBarSize size, bool is_64bit,
                         bool is_prefetchable) {
    if (bar_idx >= 6) return;
    bar_sizes_[bar_idx] = size;
    bar_masks_[bar_idx] = ~(size - 1);
    bar_is_64bit_[bar_idx] = is_64bit;
    bar_bases_[bar_idx] = 0;

    uint32_t val = 0;
    if (is_64bit) val |= 0x04;
    if (is_prefetchable) val |= 0x08;
    std::memcpy(&config_space_[0x10 + bar_idx * 4], &val, 4);
    if (is_64bit && bar_idx < 5) {
        uint32_t zero = 0;
        std::memcpy(&config_space_[0x14 + bar_idx * 4], &zero, 4);
    }
}

auto PciDevice::config_read(PciConfigOffset offset, PciAccessBytes size) -> PciConfigData {
    if (offset + size > config_space_.size()) return 0xFFFFFFFFU;
    uint32_t val = 0;
    std::memcpy(&val, &config_space_[offset], size);
    return val;
}

void PciDevice::config_write(PciConfigOffset offset, PciConfigData val, PciAccessBytes size) {
    if (offset + size > config_space_.size()) return;

    if (offset >= 0x10 && offset < 0x28) {
        int bar_idx = static_cast<int>((offset - 0x10) / 4);
        if (bar_idx < 6) {
            const bool is_hi_of_64bit = (bar_idx > 0 && bar_is_64bit_[bar_idx - 1]);
            const int target_bar = is_hi_of_64bit ? bar_idx - 1 : bar_idx;

            if (bar_sizes_[target_bar] > 0) {
                uint32_t cur = 0;
                std::memcpy(&cur, &config_space_[offset], 4);
                if (is_hi_of_64bit) {
                    const auto hi_mask = static_cast<uint32_t>(bar_masks_[target_bar] >> 32);
                    if (val == 0xFFFFFFFFU) {
                        cur = hi_mask;
                    } else {
                        cur = val & hi_mask;
                    }
                    std::memcpy(&config_space_[offset], &cur, 4);
                } else {
                    const auto lo_mask =
                        static_cast<uint32_t>(bar_masks_[target_bar] & 0xFFFFFFFFULL);
                    if (val == 0xFFFFFFFFU) {
                        cur = lo_mask;
                    } else {
                        cur = val & lo_mask;
                    }
                    if (bar_is_64bit_[target_bar]) cur |= 0x04;
                    std::memcpy(&config_space_[offset], &cur, 4);
                }
                bar_bases_[target_bar] = bar_base(static_cast<PciBarIndex>(target_bar));
                on_bar_updated(static_cast<PciBarIndex>(target_bar));
                return;
            }
        }
    }

    std::memcpy(&config_space_[offset], &val, size);
}

auto PciDevice::bar_read(PciBarIndex bar_idx, PciBarOffset offset, PciAccessBytes size)
    -> PciMmioData {
    (void)bar_idx;
    (void)offset;
    (void)size;
    return 0;
}

void PciDevice::bar_write(PciBarIndex bar_idx, PciBarOffset offset, PciMmioData val,
                          PciAccessBytes size) {
    (void)bar_idx;
    (void)offset;
    (void)val;
    (void)size;
}

auto PciDevice::bar_base(PciBarIndex bar_idx) const -> simrv::xlen::Address {
    if (bar_idx >= 6) return 0;
    uint32_t lo = 0;
    std::memcpy(&lo, &config_space_[0x10 + bar_idx * 4], 4);
    lo &= 0xFFFFFFF0U;
    if (bar_is_64bit_[bar_idx] && bar_idx < 5) {
        uint32_t hi = 0;
        std::memcpy(&hi, &config_space_[0x14 + bar_idx * 4], 4);
        return static_cast<simrv::xlen::Address>((static_cast<uint64_t>(hi) << 32) | lo);
    }
    return lo;
}

auto PciDevice::bar_size(PciBarIndex bar_idx) const -> PciBarSize {
    if (bar_idx >= 6) return 0;
    return bar_sizes_[bar_idx];
}

auto PciDevice::contains_mmio(simrv::xlen::Address addr, PciBarIndex& out_bar,
                              PciBarOffset& out_offset) const -> bool {
    for (uint8_t i = 0; i < 6; ++i) {
        if (bar_sizes_[i] == 0) continue;
        simrv::xlen::Address base = bar_bases_[i] != 0 ? bar_bases_[i] : bar_base(i);
        if (base != 0 && addr >= base && addr < base + bar_sizes_[i]) {
            out_bar = i;
            out_offset = addr - base;
            return true;
        }
        if (bar_is_64bit_[i]) ++i;
    }
    return false;
}

}  // namespace simrv::device
