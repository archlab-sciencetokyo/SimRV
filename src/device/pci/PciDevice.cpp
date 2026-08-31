/**
 * @file PciDevice.cpp
 * @brief Implementation of PCI Type 0 endpoint base class.
 */
#include "simrv/device/pci/PciDevice.hpp"

#include <cstring>

#include "simrv/device/pci/PcieRootComplex.hpp"

namespace simrv::device {

PciDevice::PciDevice(PciVendorId vendor_id, PciDeviceId device_id, uint8_t revision_id,
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
    }
    config_space_[0x04] = 0;
    config_space_[0x05] = 0;
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

void PciDevice::init_bar(BarIndex bar_idx, Address size, bool is_64bit, bool is_prefetchable) {
    if (bar_idx >= 6) return;
    bar_sizes_[bar_idx] = size;
    bar_masks_[bar_idx] = ~(size - 1);
    bar_is_64bit_[bar_idx] = is_64bit;

    uint32_t val = 0;
    if (is_64bit) val |= 0x04;
    if (is_prefetchable) val |= 0x08;
    std::memcpy(&config_space_[0x10 + bar_idx * 4], &val, 4);
}

auto PciDevice::config_read(Address offset, uint8_t size) -> uint32_t {
    if (offset + size > config_space_.size()) return 0xFFFFFFFFU;
    uint32_t val = 0;
    std::memcpy(&val, &config_space_[offset], size);
    return val;
}

void PciDevice::config_write(Address offset, uint32_t val, uint8_t size) {
    if (offset + size > config_space_.size()) return;

    if (offset >= 0x10 && offset < 0x28) {
        int bar_idx = static_cast<int>((offset - 0x10) / 4);
        if (bar_idx < 6 && bar_sizes_[bar_idx] > 0) {
            uint32_t cur = 0;
            std::memcpy(&cur, &config_space_[offset], 4);
            if (val == 0xFFFFFFFFU) {
                cur = static_cast<uint32_t>(bar_masks_[bar_idx]);
            } else {
                cur = val & static_cast<uint32_t>(bar_masks_[bar_idx]);
            }
            if (bar_is_64bit_[bar_idx]) cur |= 0x04;
            std::memcpy(&config_space_[offset], &cur, 4);
            return;
        }
    }

    std::memcpy(&config_space_[offset], &val, size);
}

auto PciDevice::bar_read(BarIndex bar_idx, Address offset, uint8_t size) -> uint32_t {
    (void)bar_idx;
    (void)offset;
    (void)size;
    return 0;
}

void PciDevice::bar_write(BarIndex bar_idx, Address offset, uint32_t val, uint8_t size) {
    (void)bar_idx;
    (void)offset;
    (void)val;
    (void)size;
}

auto PciDevice::bar_base(BarIndex bar_idx) const -> Address {
    if (bar_idx >= 6) return 0;
    uint32_t lo = 0;
    std::memcpy(&lo, &config_space_[0x10 + bar_idx * 4], 4);
    lo &= 0xFFFFFFF0U;
    if (bar_is_64bit_[bar_idx] && bar_idx < 5) {
        uint32_t hi = 0;
        std::memcpy(&hi, &config_space_[0x14 + bar_idx * 4], 4);
        return static_cast<Address>((static_cast<uint64_t>(hi) << 32) | lo);
    }
    return lo;
}

auto PciDevice::bar_size(BarIndex bar_idx) const -> Address {
    if (bar_idx >= 6) return 0;
    return bar_sizes_[bar_idx];
}

auto PciDevice::contains_mmio(Address addr, BarIndex& out_bar, Address& out_offset) const -> bool {
    for (uint8_t i = 0; i < 6; ++i) {
        if (bar_sizes_[i] == 0) continue;
        Address base = bar_base(i);
        if (base != 0 && addr >= base && addr < base + bar_sizes_[i]) {
            out_bar = i;
            out_offset = addr - base;
            return true;
        }
    }
    return false;
}

}  // namespace simrv::device
