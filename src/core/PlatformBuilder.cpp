/** @file PlatformBuilder.cpp */
#include "simrv/core/PlatformBuilder.hpp"

#include <memory>

#include "MachineRuntime.hpp"
#include "simrv/core/Machine.hpp"
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

    if (composition.pcie) {
        machine.runtime_->pcie = std::make_unique<simrv::device::PcieRootComplex>(
            &machine, machine.runtime_->aplic_s.get(), machine.runtime_->imsic_s.get());
        machine.runtime_->pci_disk = std::make_shared<simrv::device::VirtioPciBlock>(disk_path);
        machine.runtime_->pci_console = std::make_shared<simrv::device::VirtioPciConsole>();
        machine.runtime_->pci_rng = std::make_shared<simrv::device::VirtioPciRng>();
        machine.runtime_->pci_gpu = std::make_shared<simrv::device::VirtioPciGpu>();
        machine.runtime_->pci_input = std::make_shared<simrv::device::VirtioPciInput>();
        machine.runtime_->pci_sound = std::make_shared<simrv::device::VirtioPciSound>();
        machine.runtime_->pci_net = std::make_shared<simrv::device::VirtioPciNet>();
        const std::array<std::shared_ptr<simrv::device::PciDevice>, 7> pci_devices = {
            machine.runtime_->pci_disk, machine.runtime_->pci_console, machine.runtime_->pci_rng,
            machine.runtime_->pci_gpu,  machine.runtime_->pci_input,   machine.runtime_->pci_sound,
            machine.runtime_->pci_net,
        };
        for (uint8_t slot = 1; slot <= pci_devices.size(); ++slot) {
            machine.runtime_->pcie->attach_device(0, slot, 0, pci_devices[slot - 1]);
        }
    }
    if (composition.mmio) {
        machine.runtime_->mmio_disk =
            std::make_shared<simrv::device::VirtioMmioBlock>(0x10001000, 2, &machine, disk_path);
        machine.runtime_->mmio_console =
            std::make_shared<simrv::device::VirtioMmioConsole>(0x10002000, 1, &machine);
        machine.runtime_->mmio_rng =
            std::make_shared<simrv::device::VirtioMmioRng>(0x10003000, 4, &machine);
        machine.runtime_->mmio_gpu =
            std::make_shared<simrv::device::VirtioMmioGpu>(0x10004000, 5, &machine);
        machine.runtime_->mmio_input =
            std::make_shared<simrv::device::VirtioMmioInput>(0x10005000, 6, &machine);
        machine.runtime_->mmio_sound =
            std::make_shared<simrv::device::VirtioMmioSound>(0x10006000, 7, &machine);
        machine.runtime_->mmio_net =
            std::make_shared<simrv::device::VirtioMmioNet>(0x10007000, 8, &machine);
    }
}

}  // namespace simrv::core
