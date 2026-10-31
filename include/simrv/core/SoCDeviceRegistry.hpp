/**
 * @file SoCDeviceRegistry.hpp
 * @brief Effective device descriptors for a normalized SoC preset.
 */
#pragma once

#include <array>
#include <optional>
#include <vector>

#include "simrv/core/SoCConfig.hpp"

namespace simrv::core {

/**
 * Resolves preset defaults and explicit device policy into a single device view.
 *
 * The registry deliberately contains only declarative information. Runtime device
 * factories, SoC manifest generation, and auto-generated Linux device trees consume
 * the same view without duplicating device enablement policy decisions.
 */
class SoCDeviceRegistry final {
   public:
    [[nodiscard]] static auto kinds() -> std::array<SoCDeviceKind, 23> {
        return {SoCDeviceKind::Uart,
                SoCDeviceKind::Rtc,
                SoCDeviceKind::Power,
                SoCDeviceKind::AclintMtimer,
                SoCDeviceKind::AclintMswi,
                SoCDeviceKind::Imsic,
                SoCDeviceKind::Aplic,
                SoCDeviceKind::Plic,
                SoCDeviceKind::Clint,
                SoCDeviceKind::VirtioMmioBlock,
                SoCDeviceKind::VirtioMmioConsole,
                SoCDeviceKind::VirtioMmioRng,
                SoCDeviceKind::VirtioMmioGpu,
                SoCDeviceKind::VirtioMmioInput,
                SoCDeviceKind::VirtioMmioSound,
                SoCDeviceKind::VirtioMmioNet,
                SoCDeviceKind::VirtioMmioFs,
                SoCDeviceKind::DmaController,
                SoCDeviceKind::ResetControl,
                SoCDeviceKind::RingBufferEthernet,
                SoCDeviceKind::DummyMmio,
                SoCDeviceKind::CustomMmio,
                SoCDeviceKind::PFlash};
    }

    [[nodiscard]] static auto enabled(const SoCConfig& config, SoCDeviceKind kind) -> bool {
        if (kind == SoCDeviceKind::ResetControl || kind == SoCDeviceKind::RingBufferEthernet ||
            kind == SoCDeviceKind::DummyMmio || kind == SoCDeviceKind::CustomMmio ||
            kind == SoCDeviceKind::PFlash) {
            return config.find(kind) != nullptr;
        }
        return !config.disable_unlisted_devices || config.find(kind) != nullptr;
    }

    [[nodiscard]] static auto descriptor(const SoCConfig& config, SoCDeviceKind kind)
        -> std::optional<SoCDeviceConfig> {
        if (!enabled(config, kind)) return std::nullopt;
        auto device = config.find(kind) != nullptr ? *config.find(kind) : soc_device_default(kind);
        const auto defaults = soc_device_default(kind);
        device.pci_bus = device.pci_bus.value_or(defaults.pci_bus.value_or(0));
        device.pci_slot = device.pci_slot.value_or(defaults.pci_slot.value_or(0));
        device.pci_function = device.pci_function.value_or(defaults.pci_function.value_or(0));
        device.pci_intx_source =
            device.pci_intx_source.value_or(defaults.pci_intx_source.value_or(0));
        return device;
    }

    [[nodiscard]] static auto resolve(const SoCConfig& config) -> std::vector<SoCDeviceConfig> {
        std::vector<SoCDeviceConfig> devices;
        for (const auto kind : kinds()) {
            if (kind == SoCDeviceKind::DummyMmio || kind == SoCDeviceKind::CustomMmio) continue;
            if (const auto device = descriptor(config, kind)) devices.push_back(*device);
        }
        for (const auto& device : config.devices) {
            if (device.enabled && (device.kind == SoCDeviceKind::DummyMmio ||
                                   device.kind == SoCDeviceKind::CustomMmio)) {
                devices.push_back(device);
            }
        }
        return devices;
    }
};

}  // namespace simrv::core
