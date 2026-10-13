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
 * factories, FDT generation, and future HDL generation can consume the same view
 * without duplicating policy decisions.
 */
class SoCDeviceRegistry final {
   public:
    [[nodiscard]] static auto kinds() -> std::array<SoCDeviceKind, 22> {
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
                SoCDeviceKind::CustomMmio};
    }

    [[nodiscard]] static auto enabled(const SoCConfig& config, SoCDeviceKind kind) -> bool {
        if (kind == SoCDeviceKind::ResetControl || kind == SoCDeviceKind::RingBufferEthernet ||
            kind == SoCDeviceKind::DummyMmio || kind == SoCDeviceKind::CustomMmio) {
            return config.find(kind) != nullptr;
        }
        return !config.disable_unlisted_devices || config.find(kind) != nullptr;
    }

    [[nodiscard]] static auto descriptor(const SoCConfig& config, SoCDeviceKind kind)
        -> std::optional<SoCDeviceConfig> {
        if (!enabled(config, kind)) return std::nullopt;
        if (const auto* configured = config.find(kind)) return *configured;
        return soc_device_default(kind);
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
