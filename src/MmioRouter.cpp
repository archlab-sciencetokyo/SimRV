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
    MmioDevice* devices[] = {
        machine_.console,
        machine_.disk,
        machine_.cpu ? static_cast<MmioDevice*>(&machine_.cpu->plic_mmio) : nullptr,
        machine_.cpu ? static_cast<MmioDevice*>(&machine_.cpu->clint_mmio) : nullptr,
    };

    for (const auto& device : devices) {
        if (device == nullptr) continue;
        if (device->contains(p_addr)) return device->read(machine_, p_addr, rdata);
    }
    return false;
}

bool MmioRouter::write(Address p_addr, Word wdata) {
    if (p_addr == D_TOHOST_ADDR) {
        machine_.tohost = wdata;
        return true;
    }

    MmioDevice* devices[] = {
        machine_.console,
        machine_.disk,
        machine_.cpu ? static_cast<MmioDevice*>(&machine_.cpu->plic_mmio) : nullptr,
        machine_.cpu ? static_cast<MmioDevice*>(&machine_.cpu->clint_mmio) : nullptr,
    };

    for (const auto& device : devices) {
        if (device == nullptr) continue;
        if (device->contains(p_addr)) return device->write(machine_, p_addr, wdata);
    }
    return false;
}
