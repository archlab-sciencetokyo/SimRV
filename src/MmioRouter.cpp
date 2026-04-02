/**
 * @file MmioRouter.cpp
 * @brief SimRV MMIO router implementation.
 */
#include "MmioRouter.hpp"

#include "Machine.hpp"

namespace {
constexpr Address D_TOHOST_ADDR = static_cast<Address>(0x40008000u);

using DeviceList = std::array<MmioDevice*, 4>;

DeviceList make_devices(Machine& machine) {
    return {machine.console.get(), machine.disk.get(),
            static_cast<MmioDevice*>(&machine.cpu.plic_mmio),
            static_cast<MmioDevice*>(&machine.cpu.clint_mmio)};
}
}  // namespace

MmioRouter::MmioRouter(Machine& machine) : machine_(machine) {}

bool MmioRouter::read(Address p_addr, Word& rdata) {
    for (MmioDevice* device : make_devices(machine_)) {
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

    for (MmioDevice* device : make_devices(machine_)) {
        if (device == nullptr) continue;
        if (device->contains(p_addr)) return device->write(machine_, p_addr, wdata);
    }
    return false;
}
