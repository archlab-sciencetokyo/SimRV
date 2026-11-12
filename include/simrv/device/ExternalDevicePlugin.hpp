/**
 * @file ExternalDevicePlugin.hpp
 * @brief Dynamic loader and adapter for external MMIO device plugins.
 */
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "simrv/device/ExternalDevice.h"
#include "simrv/device/PlatformDeviceRegistry.hpp"
#include "simrv/memory/MmioDevice.hpp"

namespace simrv::device {

/**
 * @class ExternalMmioDevice
 * @brief Adapts a C-ABI SimRvExternalDevice descriptor into a SimRV TileLink MmioDevice.
 */
class ExternalMmioDevice : public memory::MmioDevice {
   public:
    ExternalMmioDevice(const SimRvExternalDevice* desc, void* ctx, std::string name,
                       simrv::xlen::PhysAddr base, simrv::xlen::Address size,
                       simrv::core::Machine& machine, bool defer_reset = false);
    ~ExternalMmioDevice() override;
    void reset() override;

    [[nodiscard]] auto name() const -> const char* override { return name_.c_str(); }
    [[nodiscard]] auto base_address() const -> simrv::xlen::PhysAddr override { return base_; }
    [[nodiscard]] auto size() const -> simrv::xlen::Address override { return size_; }
    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool override;

    [[nodiscard]] auto read8(simrv::memory::MmioOffset offset) -> simrv::memory::MmioData8 override;
    [[nodiscard]] auto read16(simrv::memory::MmioOffset offset)
        -> simrv::memory::MmioData16 override;
    [[nodiscard]] auto read32(simrv::memory::MmioOffset offset)
        -> simrv::memory::MmioData32 override;
    [[nodiscard]] auto read64(simrv::memory::MmioOffset offset)
        -> simrv::memory::MmioData64 override;

    void write8(simrv::memory::MmioOffset offset, simrv::memory::MmioData8 val) override;
    void write16(simrv::memory::MmioOffset offset, simrv::memory::MmioData16 val) override;
    void write32(simrv::memory::MmioOffset offset, simrv::memory::MmioData32 val) override;
    void write64(simrv::memory::MmioOffset offset, simrv::memory::MmioData64 val) override;

    void advance_cycles(uint64_t cycles) override;
    [[nodiscard]] auto has_cycle_ticking() const noexcept -> bool override;

   private:
    SimRvHostServices host_services_{};
    const SimRvExternalDevice* desc_{nullptr};
    void* ctx_{nullptr};
    std::string name_;
    simrv::xlen::PhysAddr base_{};
    simrv::xlen::Address size_{0};
};

/**
 * @brief Load an external device shared library (.so) and register it with the device registry.
 * @param path_and_args Path or logical name of plugin, optionally followed by '=' and arguments.
 * @param registry PlatformDeviceRegistry to register the compatible string and factory into.
 * @return True on successful load and registration.
 */
auto load_external_device_plugin(std::string_view path_and_args, PlatformDeviceRegistry& registry)
    -> bool;

/**
 * @brief Resolves a logical plugin name or path into an absolute file path.
 * Searches current directory, plugins/, executable directory, and SIMRV_PLUGIN_PATH.
 * @param name_or_path Logical name (e.g. "mmc", "rvcomp-mmc") or filename/path.
 * @return Resolved absolute filesystem path if found, or std::nullopt.
 */
auto resolve_device_plugin_path(std::string_view name_or_path) -> std::optional<std::string>;

/**
 * @brief Auto-discovers and loads an external device plugin matching a DeviceTree compatible
 * string.
 * @param compatible DeviceTree compatible property (e.g. "isct,rvcomp-mmc").
 * @param default_args Default argument string to pass to the plugin if initialized.
 * @param registry PlatformDeviceRegistry to register the loaded device with.
 * @return True if a matching plugin was found and loaded.
 */
auto autodiscover_external_device_plugin(std::string_view compatible, std::string_view default_args,
                                         PlatformDeviceRegistry& registry) -> bool;

}  // namespace simrv::device
