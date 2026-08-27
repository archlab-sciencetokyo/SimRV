/**
 * @file PcieRootComplex.cpp
 * @brief Implementation of PCIe Root Complex and ECAM/MMIO Interconnect.
 */
#include "simrv/device/pci/PcieRootComplex.hpp"

#include <cstring>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"

namespace simrv::device {

PcieRootComplex::PcieRootComplex(simrv::core::Machine* machine, Aplic* aplic_s, Imsic* imsic_s)
    : machine_(machine), aplic_s_(aplic_s), imsic_s_(imsic_s) {}

auto PcieRootComplex::attach_device(uint8_t bus, uint8_t dev, uint8_t func,
                                    std::shared_ptr<PciDevice> device) -> bool {
    if (!device) return false;
    device->set_root_complex(this, bus, dev, func);
    attached_devices_.push_back({bus, dev, func, device});
    return true;
}

auto PcieRootComplex::get_device(uint8_t bus, uint8_t dev, uint8_t func)
    -> std::shared_ptr<PciDevice> {
    for (auto& entry : attached_devices_) {
        if (entry.bus == bus && entry.dev == dev && entry.func == func) {
            return entry.device;
        }
    }
    return nullptr;
}

auto PcieRootComplex::ecam_read(Address offset, uint8_t size) -> uint32_t {
    const uint8_t bus = static_cast<uint8_t>((offset >> 20) & 0xFF);
    const uint8_t dev = static_cast<uint8_t>((offset >> 15) & 0x1F);
    const uint8_t func = static_cast<uint8_t>((offset >> 12) & 0x07);
    const Address reg_offset = offset & 0xFFF;

    auto d = get_device(bus, dev, func);
    if (!d) return 0xFFFFFFFFU;
    return d->config_read(reg_offset, size);
}

void PcieRootComplex::ecam_write(Address offset, uint32_t val, uint8_t size) {
    const uint8_t bus = static_cast<uint8_t>((offset >> 20) & 0xFF);
    const uint8_t dev = static_cast<uint8_t>((offset >> 15) & 0x1F);
    const uint8_t func = static_cast<uint8_t>((offset >> 12) & 0x07);
    const Address reg_offset = offset & 0xFFF;

    auto d = get_device(bus, dev, func);
    if (d) {
        d->config_write(reg_offset, val, size);
    }
}

auto PcieRootComplex::mmio_read(Address addr, uint8_t size) -> uint32_t {
    for (auto& entry : attached_devices_) {
        int bar_idx = 0;
        Address offset = 0;
        if (entry.device && entry.device->contains_mmio(addr, bar_idx, offset)) {
            return entry.device->bar_read(bar_idx, offset, size);
        }
    }
    return 0;
}

void PcieRootComplex::mmio_write(Address addr, uint32_t val, uint8_t size) {
    for (auto& entry : attached_devices_) {
        int bar_idx = 0;
        Address offset = 0;
        if (entry.device && entry.device->contains_mmio(addr, bar_idx, offset)) {
            entry.device->bar_write(bar_idx, offset, val, size);
            return;
        }
    }
}

void PcieRootComplex::assert_device_irq(uint8_t bus, uint8_t dev, uint8_t func) {
    (void)bus;
    (void)func;
    const uint32_t irq_source = 16 + (dev % 8);

    if (aplic_s_ != nullptr) {
        aplic_s_->set_irq(irq_source, true);
    }
    if (machine_ != nullptr) {
        machine_->set_platform_irq(static_cast<int>(irq_source), true);
    }
}

auto PcieRootComplex::EcamNode::handle_request(const memory::TlChannelA& req,
                                               memory::TlChannelD& resp) -> bool {
    const Address offset = req.address - kEcamBaseAddress;
    const bool is_write = (req.opcode == memory::TlOpcodeA::PutFullData ||
                           req.opcode == memory::TlOpcodeA::PutPartialData);

    resp.opcode = is_write ? memory::TlOpcodeD::AccessAck : memory::TlOpcodeD::AccessAckData;
    resp.size = req.size;
    resp.source = req.source;
    resp.error = false;

    const uint8_t size = 1 << req.size;
    if (is_write) {
        rc_->ecam_write(offset, static_cast<uint32_t>(req.data), size);
    } else {
        resp.data = rc_->ecam_read(offset, size);
    }
    return true;
}

auto PcieRootComplex::MmioNode::handle_request(const memory::TlChannelA& req,
                                               memory::TlChannelD& resp) -> bool {
    const Address addr = req.address;
    const bool is_write = (req.opcode == memory::TlOpcodeA::PutFullData ||
                           req.opcode == memory::TlOpcodeA::PutPartialData);

    resp.opcode = is_write ? memory::TlOpcodeD::AccessAck : memory::TlOpcodeD::AccessAckData;
    resp.size = req.size;
    resp.source = req.source;
    resp.error = false;

    const uint8_t size = 1 << req.size;
    if (is_write) {
        rc_->mmio_write(addr, static_cast<uint32_t>(req.data), size);
    } else {
        resp.data = rc_->mmio_read(addr, size);
    }
    return true;
}

}  // namespace simrv::device
