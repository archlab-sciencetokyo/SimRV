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
#include "simrv/memory/MmioDevice.hpp"

namespace simrv::device {

using CustomMmioFactory = std::function<std::unique_ptr<simrv::memory::MmioDevice>(
    simrv::core::Machine&, const simrv::core::SoCDeviceConfig&)>;

/** Per-machine factories for platform devices. Registry lifetime matches its Machine. */
class PlatformDeviceRegistry final {
   public:
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

    /** Construct a registered device, or return null when no factory owns this compatible. */
    auto create_mmio(simrv::core::Machine& machine,
                     const simrv::core::SoCDeviceConfig& config) const
        -> std::unique_ptr<simrv::memory::MmioDevice> {
        CustomMmioFactory factory;
        {
            std::lock_guard lock(mutex_);
            if (const auto it = kind_entries_.find(config.kind); it != kind_entries_.end()) {
                factory = it->second;
            } else {
                const auto compatible_it = compatible_entries_.find(config.compatible);
                if (compatible_it == compatible_entries_.end()) return {};
                factory = compatible_it->second;
            }
        }
        auto device = factory(machine, config);
        if (device == nullptr || device->base_address() != config.base ||
            device->size() != config.size || std::string_view(device->name()) != config.name) {
            return {};
        }
        return device;
    }

   private:
    mutable std::mutex mutex_;
    std::unordered_map<simrv::core::SoCDeviceKind, CustomMmioFactory> kind_entries_;
    std::unordered_map<std::string, CustomMmioFactory> compatible_entries_;
};

}  // namespace simrv::device
