/**
 * @file MmioRouter.cpp
 * @brief SimRV MMIO router implementation.
 */
#include "MmioRouter.hpp"

#include "Machine.hpp"

namespace {
constexpr Address D_TOHOST_ADDR = static_cast<Address>(0x40008000u);
}  // namespace

MmioRouter::MmioRouter(Machine& machine) : machine_(machine) {}

bool MmioRouter::read(Address p_addr, Word& rdata) {
    switch (p_addr & static_cast<Address>(0xF0000000u)) {
        case static_cast<Address>(0x40000000u):
            if (machine_.console != nullptr && machine_.console->contains(p_addr)) {
                return machine_.console->read(machine_, p_addr, rdata);
            }
            if (machine_.disk != nullptr && machine_.disk->contains(p_addr)) {
                return machine_.disk->read(machine_, p_addr, rdata);
            }
            return false;
        case static_cast<Address>(0x50000000u):
            return machine_.cpu.plic_mmio.contains(p_addr) &&
                   machine_.cpu.plic_mmio.read(machine_, p_addr, rdata);
        case static_cast<Address>(0x60000000u):
            return machine_.cpu.clint_mmio.contains(p_addr) &&
                   machine_.cpu.clint_mmio.read(machine_, p_addr, rdata);
        default:
            return false;
    }
}

bool MmioRouter::write(Address p_addr, Word wdata) {
    if (p_addr == D_TOHOST_ADDR) {
        machine_.tohost = wdata;
        return true;
    }

    switch (p_addr & static_cast<Address>(0xF0000000u)) {
        case static_cast<Address>(0x40000000u):
            if (machine_.console != nullptr && machine_.console->contains(p_addr)) {
                return machine_.console->write(machine_, p_addr, wdata);
            }
            if (machine_.disk != nullptr && machine_.disk->contains(p_addr)) {
                return machine_.disk->write(machine_, p_addr, wdata);
            }
            return false;
        case static_cast<Address>(0x50000000u):
            return machine_.cpu.plic_mmio.contains(p_addr) &&
                   machine_.cpu.plic_mmio.write(machine_, p_addr, wdata);
        case static_cast<Address>(0x60000000u):
            return machine_.cpu.clint_mmio.contains(p_addr) &&
                   machine_.cpu.clint_mmio.write(machine_, p_addr, wdata);
        default:
            return false;
    }
}
