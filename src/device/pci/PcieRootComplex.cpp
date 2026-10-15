/**
 * @file PcieRootComplex.cpp
 * @brief Implementation of PCIe Root Complex and ECAM/MMIO Interconnect.
 */
#include "simrv/device/pci/PcieRootComplex.hpp"

#include <algorithm>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemorySubsystem.hpp"

namespace simrv::device {

PcieRootComplex::PcieRootComplex(simrv::core::Machine* machine, Aplic* aplic_s, Imsic* imsic_s)
    : machine_(machine), aplic_s_(aplic_s), imsic_s_(imsic_s) {}

auto PcieRootComplex::attach_device(PciBdf bdf, std::shared_ptr<PciDevice> device) -> bool {
    if (!device) return false;
    device->set_root_complex(this, bdf);
    if (bdf.bus == 0 && bdf.dev < kMaxSlots && bdf.func == 0) {
        devices_by_slot_[bdf.dev] = device.get();
    }
    owned_devices_.push_back(device);
    rebuild_bar_mappings();
    return true;
}

auto PcieRootComplex::get_device(PciBdf bdf) const noexcept -> PciDevice* {
    if (bdf.bus == 0 && bdf.dev < kMaxSlots && bdf.func == 0) {
        return devices_by_slot_[bdf.dev];
    }
    for (const auto& dev : owned_devices_) {
        if (dev && dev->bdf() == bdf) {
            return dev.get();
        }
    }
    return nullptr;
}

void PcieRootComplex::rebuild_bar_mappings() {
    active_bars_.clear();
    mru_bar_count_ = 0;
    for (const auto& dev : owned_devices_) {
        if (!dev) continue;
        for (uint8_t i = 0; i < 6; ++i) {
            const simrv::xlen::Address size = dev->bar_size(i);
            const simrv::xlen::Address base = dev->bar_base(i);
            if (size > 0 && base != 0 && base >= kMmioBaseAddress &&
                (base + size) <= (kMmioBaseAddress + kMmioSize)) {
                active_bars_.push_back(BarMapping{
                    .base = base,
                    .end = base + size,
                    .device = dev.get(),
                    .bar_idx = i,
                });
            }
            if (dev->bar_is_64bit(i)) {
                ++i;
            }
        }
    }
    std::ranges::sort(active_bars_, [](const auto& a, const auto& b) { return a.base < b.base; });
}

auto PcieRootComplex::ecam_read(simrv::xlen::Address offset, PciAccessBytes size) -> PciConfigData {
    const auto bus = static_cast<PciBusNumber>((offset >> 20) & 0xFF);
    const auto dev = static_cast<PciDeviceNumber>((offset >> 15) & 0x1F);
    const auto func = static_cast<PciFunctionNumber>((offset >> 12) & 0x07);
    const auto reg_offset = static_cast<PciConfigOffset>(offset & 0xFFF);

    auto* d = get_device(bus, dev, func);
    if (d == nullptr) return 0xFFFFFFFFU;
    return d->config_read(reg_offset, size);
}

void PcieRootComplex::ecam_write(simrv::xlen::Address offset, PciConfigData val,
                                 PciAccessBytes size) {
    const auto bus = static_cast<PciBusNumber>((offset >> 20) & 0xFF);
    const auto dev = static_cast<PciDeviceNumber>((offset >> 15) & 0x1F);
    const auto func = static_cast<PciFunctionNumber>((offset >> 12) & 0x07);
    const auto reg_offset = static_cast<PciConfigOffset>(offset & 0xFFF);

    auto* d = get_device(bus, dev, func);
    if (d != nullptr) {
        d->config_write(reg_offset, val, size);
    }
}

auto PcieRootComplex::mmio_read(simrv::xlen::Address addr, PciAccessBytes size) -> PciMmioData {
    if (active_bars_.empty() || addr < kMmioBaseAddress || addr >= kMmioBaseAddress + kMmioSize) {
        return 0;
    }

    // Check 4-entry MRU cache
    for (size_t i = 0; i < mru_bar_count_; ++i) {
        const auto& entry = mru_bars_[i];
        if (addr >= entry.base && (addr + size) <= entry.end) {
            if (i > 0) {
                const auto hit = entry;
                for (size_t j = i; j > 0; --j) {
                    mru_bars_[j] = mru_bars_[j - 1];
                }
                mru_bars_[0] = hit;
            }
            return mru_bars_[0].device->bar_read(
                mru_bars_[0].bar_idx, static_cast<PciBarOffset>(addr - mru_bars_[0].base), size);
        }
    }

    // Binary search over sorted base addresses
    auto it = std::ranges::upper_bound(active_bars_, addr, {}, &BarMapping::base);
    while (it != active_bars_.begin()) {
        --it;
        if (addr < it->end && (addr + size) <= it->end) {
            const size_t shift_limit = std::min(mru_bar_count_, kBarMruCapacity - 1);
            for (size_t j = shift_limit; j > 0; --j) {
                mru_bars_[j] = mru_bars_[j - 1];
            }
            mru_bars_[0] = *it;
            if (mru_bar_count_ < kBarMruCapacity) {
                ++mru_bar_count_;
            }
            return it->device->bar_read(it->bar_idx, static_cast<PciBarOffset>(addr - it->base),
                                        size);
        }
    }
    return 0;
}

void PcieRootComplex::mmio_write(simrv::xlen::Address addr, PciMmioData val, PciAccessBytes size) {
    if (active_bars_.empty() || addr < kMmioBaseAddress || addr >= kMmioBaseAddress + kMmioSize) {
        return;
    }

    // Check 4-entry MRU cache
    for (size_t i = 0; i < mru_bar_count_; ++i) {
        const auto& entry = mru_bars_[i];
        if (addr >= entry.base && (addr + size) <= entry.end) {
            if (i > 0) {
                const auto hit = entry;
                for (size_t j = i; j > 0; --j) {
                    mru_bars_[j] = mru_bars_[j - 1];
                }
                mru_bars_[0] = hit;
            }
            mru_bars_[0].device->bar_write(mru_bars_[0].bar_idx,
                                           static_cast<PciBarOffset>(addr - mru_bars_[0].base), val,
                                           size);
            return;
        }
    }

    // Binary search over sorted base addresses
    auto it = std::ranges::upper_bound(active_bars_, addr, {}, &BarMapping::base);
    while (it != active_bars_.begin()) {
        --it;
        if (addr < it->end && (addr + size) <= it->end) {
            const size_t shift_limit = std::min(mru_bar_count_, kBarMruCapacity - 1);
            for (size_t j = shift_limit; j > 0; --j) {
                mru_bars_[j] = mru_bars_[j - 1];
            }
            mru_bars_[0] = *it;
            if (mru_bar_count_ < kBarMruCapacity) {
                ++mru_bar_count_;
            }
            it->device->bar_write(it->bar_idx, static_cast<PciBarOffset>(addr - it->base), val,
                                  size);
            return;
        }
    }
}

void PcieRootComplex::assert_device_irq(PciBdf bdf) {
    const simrv::xlen::InterruptSourceId irq_source = 16 + (bdf.dev % 8);

    if (aplic_s_ != nullptr) {
        aplic_s_->set_irq(irq_source, true);
    }
    if (machine_ != nullptr) {
        machine_->set_platform_irq(static_cast<int>(irq_source), true);
    }
}

void PcieRootComplex::deassert_device_irq(PciBdf bdf) {
    const simrv::xlen::InterruptSourceId irq_source = 16 + (bdf.dev % 8);

    if (aplic_s_ != nullptr) {
        aplic_s_->set_irq(irq_source, false);
    }
    if (machine_ != nullptr) {
        machine_->set_platform_irq(static_cast<int>(irq_source), false);
    }
}

void PcieRootComplex::send_msi(PciBdf bdf, simrv::xlen::Address addr, uint32_t message_data) {
    (void)bdf;
    if (imsic_s_ != nullptr && addr >= imsic_s_->base_address() &&
        addr < imsic_s_->base_address() + imsic_s_->size()) {
        imsic_s_->write32(addr - imsic_s_->base_address(), message_data);
        return;
    }
    if (aplic_s_ != nullptr && addr >= aplic_s_->base_address() &&
        addr < aplic_s_->base_address() + aplic_s_->size()) {
        aplic_s_->write32(addr - aplic_s_->base_address(), message_data);
        return;
    }
    if (machine_ != nullptr) {
        machine_->memory().dma_write(addr, &message_data, sizeof(message_data));
    }
}

auto PcieRootComplex::EcamNode::handle_request(const memory::TlChannelA& req,
                                               memory::TlChannelD& resp) -> bool {
    const simrv::xlen::Address offset = (req.address - kEcamBaseAddress).raw();
    const bool is_write = (req.opcode == memory::TlOpcodeA::PutFullData ||
                           req.opcode == memory::TlOpcodeA::PutPartialData);

    resp.opcode = is_write ? memory::TlOpcodeD::AccessAck : memory::TlOpcodeD::AccessAckData;
    resp.size = req.size;
    resp.source = req.source;
    resp.denied = false;
    resp.corrupt = false;

    const auto size = static_cast<PciAccessBytes>(1u << (req.size & 0x3u));
    if (is_write) {
        rc_->ecam_write(offset, static_cast<PciConfigData>(req.data), size);
    } else {
        resp.data = rc_->ecam_read(offset, size);
    }
    return true;
}

auto PcieRootComplex::MmioNode::handle_request(const memory::TlChannelA& req,
                                               memory::TlChannelD& resp) -> bool {
    const simrv::xlen::Address addr = req.address.raw();
    const bool is_write = (req.opcode == memory::TlOpcodeA::PutFullData ||
                           req.opcode == memory::TlOpcodeA::PutPartialData);

    resp.opcode = is_write ? memory::TlOpcodeD::AccessAck : memory::TlOpcodeD::AccessAckData;
    resp.size = req.size;
    resp.source = req.source;
    resp.denied = false;
    resp.corrupt = false;

    const auto size = static_cast<PciAccessBytes>(1u << (req.size & 0x3u));
    if (is_write) {
        rc_->mmio_write(addr, static_cast<PciMmioData>(req.data), size);
    } else {
        resp.data = rc_->mmio_read(addr, size);
    }
    return true;
}

}  // namespace simrv::device
