/**
 * @file PlatformDeviceRegistry.hpp
 * @brief Registration API for embedding applications that provide custom MMIO devices.
 */
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "simrv/core/SoCConfig.hpp"
#include "simrv/device/SerialDevice.hpp"
#include "simrv/memory/MmioDevice.hpp"

namespace simrv::device {

using CustomMmioFactory = std::function<std::unique_ptr<simrv::memory::MmioDevice>(
    simrv::core::Machine&, const simrv::core::SoCDeviceConfig&)>;
using SerialDeviceFactory = std::function<std::unique_ptr<SerialDevice>(
    simrv::core::Machine&, const simrv::core::SoCDeviceConfig&)>;

/** Per-machine factories for platform devices. Registry lifetime matches its Machine. */
class PlatformDeviceRegistry final {
   public:
    /** Register a serial implementation selected by a device's compatible string. */
    auto register_serial_compatible(std::string compatible, SerialDeviceFactory factory) -> bool {
        if (compatible.empty() || !factory) return false;
        std::lock_guard lock(mutex_);
        return serial_entries_.emplace(std::move(compatible), std::move(factory)).second;
    }

    [[nodiscard]] auto has_serial_compatible(const std::string& compatible) const -> bool {
        std::lock_guard lock(mutex_);
        return serial_entries_.contains(compatible);
    }

    auto create_serial(simrv::core::Machine& machine,
                       const simrv::core::SoCDeviceConfig& config) const
        -> std::unique_ptr<SerialDevice> {
        SerialDeviceFactory factory;
        {
            std::lock_guard lock(mutex_);
            const auto it = serial_entries_.find(config.compatible);
            if (it == serial_entries_.end()) return {};
            factory = it->second;
        }
        auto device = factory(machine, config);
        if (!device || (config.base != 0 && device->base_address() != config.base) ||
            (config.size != 0 && device->size() != config.size)) return {};
        return device;
    }

    /** Register an MMIO implementation for a built-in declarative device kind. */
    auto register_mmio_device(simrv::core::SoCDeviceKind kind, CustomMmioFactory factory) -> bool {
        if (!factory) return false;
        std::lock_guard lock(mutex_);
        return kind_entries_.emplace(kind, std::move(factory)).second;
    }

    /** Register an MMIO implementation for a custom DT compatible string. */
    auto register_mmio_compatible(std::string compatible, CustomMmioFactory factory) -> bool {
        if (compatible.empty() || !factory) return false;
        std::lock_guard lock(mutex_);
        return compatible_entries_.emplace(std::move(compatible), std::move(factory)).second;
    }

    /** Register a plugin's MMIO and optional serial capabilities together. */
    auto register_external_compatible(std::string compatible, CustomMmioFactory mmio,
                                      SerialDeviceFactory serial = {}) -> bool {
        if (compatible.empty() || !mmio) return false;
        std::lock_guard lock(mutex_);
        if (compatible_entries_.contains(compatible) ||
            (serial && serial_entries_.contains(compatible))) return false;
        compatible_entries_.emplace(compatible, std::move(mmio));
        if (serial) serial_entries_.emplace(std::move(compatible), std::move(serial));
        return true;
    }

    [[nodiscard]] auto has_mmio_compatible(const std::string& compatible) const -> bool {
        std::lock_guard lock(mutex_);
        return compatible_entries_.contains(compatible);
    }

    /** Construct a registered device, or return null when no factory owns this compatible. */
    auto create_mmio(simrv::core::Machine& machine,
                     const simrv::core::SoCDeviceConfig& config) const
        -> std::unique_ptr<simrv::memory::MmioDevice> {
        CustomMmioFactory factory;
        {
            std::lock_guard lock(mutex_);
            if (!config.compatible.empty()) {
                if (const auto compatible_it = compatible_entries_.find(config.compatible);
                    compatible_it != compatible_entries_.end()) {
                    factory = compatible_it->second;
                }
            }
            if (!factory) {
                if (const auto it = kind_entries_.find(config.kind); it != kind_entries_.end()) {
                    factory = it->second;
                }
            }
            if (!factory) return {};
        }
        auto device = factory(machine, config);
        if (device == nullptr || (config.base != 0 && device->base_address() != config.base) ||
            (config.size != 0 && device->size() != config.size) ||
            (!config.name.empty() && std::string_view(device->name()) != config.name)) {
            return {};
        }
        return device;
    }

   private:
    mutable std::mutex mutex_;
    std::unordered_map<simrv::core::SoCDeviceKind, CustomMmioFactory> kind_entries_;
    std::unordered_map<std::string, CustomMmioFactory> compatible_entries_;
    std::unordered_map<std::string, SerialDeviceFactory> serial_entries_;
};

}  // namespace simrv::device
