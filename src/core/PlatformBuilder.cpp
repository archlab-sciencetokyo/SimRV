/** @file PlatformBuilder.cpp */
#include "simrv/core/PlatformBuilder.hpp"

#include <memory>

#include "MachineRuntime.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/SoCDeviceRegistry.hpp"
#include "simrv/device/mmio/VirtioMmioBlock.hpp"
#include "simrv/device/mmio/VirtioMmioConsole.hpp"
#include "simrv/device/mmio/VirtioMmioGpu.hpp"
#include "simrv/device/mmio/VirtioMmioInput.hpp"
#include "simrv/device/mmio/VirtioMmioNet.hpp"
#include "simrv/device/mmio/VirtioMmioRng.hpp"
#include "simrv/device/mmio/VirtioMmioSound.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"
#include "simrv/device/pci/VirtioPciBlock.hpp"
#include "simrv/device/pci/VirtioPciConsole.hpp"
#include "simrv/device/pci/VirtioPciGpu.hpp"
#include "simrv/device/pci/VirtioPciInput.hpp"
#include "simrv/device/pci/VirtioPciNet.hpp"
#include "simrv/device/pci/VirtioPciRng.hpp"
#include "simrv/device/pci/VirtioPciSound.hpp"

namespace simrv::core {

void PlatformBuilder::compose(Machine& machine) {
    const auto composition = platform_composition(machine.config.platform_profile);
    const auto& disk_path = machine.config.files.disk_path;
    const auto network_mode = [&]() {
        using Mode = simrv::device::virtio::NetBackend::Mode;
        if (machine.config.network.mode == "tap") return Mode::Tap;
        if (machine.config.network.mode == "socket") return Mode::Socket;
        if (machine.config.network.mode == "none") return Mode::None;
        return Mode::User;
    }();

    if (composition.pcie) {
        machine.runtime_->pcie = std::make_unique<simrv::device::PcieRootComplex>(
            &machine, machine.runtime_->aplic_s.get(), machine.runtime_->imsic_s.get());
        const auto enabled = [&](SoCDeviceKind kind) {
            return SoCDeviceRegistry::enabled(machine.config.soc, kind);
        };
        if (enabled(SoCDeviceKind::VirtioMmioBlock)) {
            machine.runtime_->pci_disk = std::make_shared<simrv::device::VirtioPciBlock>(disk_path);
        }
        if (enabled(SoCDeviceKind::VirtioMmioConsole)) {
            machine.runtime_->pci_console = std::make_shared<simrv::device::VirtioPciConsole>();
        }
        if (enabled(SoCDeviceKind::VirtioMmioRng)) {
            machine.runtime_->pci_rng = std::make_shared<simrv::device::VirtioPciRng>();
        }
        if (enabled(SoCDeviceKind::VirtioMmioGpu)) {
            machine.runtime_->pci_gpu = std::make_shared<simrv::device::VirtioPciGpu>();
        }
        if (enabled(SoCDeviceKind::VirtioMmioInput)) {
            machine.runtime_->pci_input = std::make_shared<simrv::device::VirtioPciInput>();
        }
        if (enabled(SoCDeviceKind::VirtioMmioSound)) {
            machine.runtime_->pci_sound = std::make_shared<simrv::device::VirtioPciSound>();
        }
        if (enabled(SoCDeviceKind::VirtioMmioNet)) {
            machine.runtime_->pci_net = std::make_shared<simrv::device::VirtioPciNet>(network_mode);
        }
        const std::array<std::shared_ptr<simrv::device::PciDevice>, 7> pci_devices = {
            machine.runtime_->pci_disk, machine.runtime_->pci_console, machine.runtime_->pci_rng,
            machine.runtime_->pci_gpu,  machine.runtime_->pci_input,   machine.runtime_->pci_sound,
            machine.runtime_->pci_net,
        };
        for (uint8_t slot = 1; slot <= pci_devices.size(); ++slot) {
            if (pci_devices[slot - 1]) {
                machine.runtime_->pcie->attach_device(0, slot, 0, pci_devices[slot - 1]);
            }
        }
    }
    if (composition.mmio) {
        const auto descriptor = [&](SoCDeviceKind kind) {
            return SoCDeviceRegistry::descriptor(machine.config.soc, kind);
        };
        if (const auto device = descriptor(SoCDeviceKind::VirtioMmioBlock)) {
            machine.runtime_->mmio_disk = std::make_shared<simrv::device::VirtioMmioBlock>(
                device->base, device->irq, &machine, disk_path);
        }
        if (const auto device = descriptor(SoCDeviceKind::VirtioMmioConsole)) {
            machine.runtime_->mmio_console = std::make_shared<simrv::device::VirtioMmioConsole>(
                device->base, device->irq, &machine);
        }
        if (const auto device = descriptor(SoCDeviceKind::VirtioMmioRng)) {
            machine.runtime_->mmio_rng =
                std::make_shared<simrv::device::VirtioMmioRng>(device->base, device->irq, &machine);
        }
        if (const auto device = descriptor(SoCDeviceKind::VirtioMmioGpu)) {
            machine.runtime_->mmio_gpu =
                std::make_shared<simrv::device::VirtioMmioGpu>(device->base, device->irq, &machine);
        }
        if (const auto device = descriptor(SoCDeviceKind::VirtioMmioInput)) {
            machine.runtime_->mmio_input = std::make_shared<simrv::device::VirtioMmioInput>(
                device->base, device->irq, &machine);
        }
        if (const auto device = descriptor(SoCDeviceKind::VirtioMmioSound)) {
            machine.runtime_->mmio_sound = std::make_shared<simrv::device::VirtioMmioSound>(
                device->base, device->irq, &machine);
        }
        if (const auto device = descriptor(SoCDeviceKind::VirtioMmioNet)) {
            machine.runtime_->mmio_net = std::make_shared<simrv::device::VirtioMmioNet>(
                device->base, device->irq, &machine, network_mode);
        }
    }
}

}  // namespace simrv::core
