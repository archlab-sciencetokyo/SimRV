/**
 * @file CfuUnit.cpp
 * @brief Implementation of the extensible Custom Function Unit (CFU) subsystem.
 */
#include "simrv/execute/CfuUnit.hpp"

#include <dlfcn.h>

#include <utility>

#include "simrv/core/Logger.hpp"
#include "simrv/device/ExternalDevicePlugin.hpp"

namespace simrv::execute {

CfuUnit::~CfuUnit() {
    if (plugin_handle_ != nullptr) {
        dlclose(plugin_handle_);
        plugin_handle_ = nullptr;
    }
}

CfuUnit::CfuUnit(CfuUnit&& other) noexcept
    : plugin_handle_(std::exchange(other.plugin_handle_, nullptr)),
      plugin_(std::exchange(other.plugin_, nullptr)),
      raw_cfu_op_(std::exchange(other.raw_cfu_op_, nullptr)),
      raw_cfu_reset_(std::exchange(other.raw_cfu_reset_, nullptr)),
      plugin_path_(std::move(other.plugin_path_)),
      last_latency_(other.last_latency_),
      default_latency_(other.default_latency_) {}

auto CfuUnit::operator=(CfuUnit&& other) noexcept -> CfuUnit& {
    if (this != &other) {
        if (plugin_handle_ != nullptr) {
            dlclose(plugin_handle_);
        }
        plugin_handle_ = std::exchange(other.plugin_handle_, nullptr);
        plugin_ = std::exchange(other.plugin_, nullptr);
        raw_cfu_op_ = std::exchange(other.raw_cfu_op_, nullptr);
        raw_cfu_reset_ = std::exchange(other.raw_cfu_reset_, nullptr);
        plugin_path_ = std::move(other.plugin_path_);
        last_latency_ = other.last_latency_;
        default_latency_ = other.default_latency_;
    }
    return *this;
}

auto CfuUnit::load_plugin(std::string_view path) -> bool {
    if (plugin_handle_ != nullptr) {
        dlclose(plugin_handle_);
        plugin_handle_ = nullptr;
        plugin_ = nullptr;
        raw_cfu_op_ = nullptr;
        raw_cfu_reset_ = nullptr;
        plugin_path_.clear();
    }

    std::string path_str(path);
    if (auto resolved = simrv::device::resolve_device_plugin_path(path)) {
        path_str = *resolved;
    }
    void* handle = dlopen(path_str.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        simrv::log::error("Failed to load CFU plugin '{}': {}", path, dlerror());
        return false;
    }

    auto init_fn = reinterpret_cast<SimRvCfuPluginInitFn>(dlsym(handle, "simrv_cfu_init"));
    if (init_fn == nullptr) {
        // Fallback: check for native CFU-Proving-Ground / CFU-Playground cfu_op symbol
        auto cfu_op_fn = reinterpret_cast<RawCfuOpFn>(dlsym(handle, "cfu_op"));
        if (cfu_op_fn == nullptr) {
            cfu_op_fn = reinterpret_cast<RawCfuOpFn>(dlsym(handle, "cfu_execute"));
        }
        if (cfu_op_fn != nullptr) {
            plugin_handle_ = handle;
            raw_cfu_op_ = cfu_op_fn;
            raw_cfu_reset_ = reinterpret_cast<RawCfuResetFn>(dlsym(handle, "cfu_reset"));
            plugin_path_ = std::move(path_str);
            simrv::log::info("Loaded CFU-Proving-Ground compatible plugin from '{}'", plugin_path_);
            return true;
        }

        simrv::log::error("CFU plugin '{}' missing entry point ('simrv_cfu_init' or 'cfu_op'): {}",
                          path, dlerror());
        dlclose(handle);
        return false;
    }

    const SimRvCfuPlugin* plugin = init_fn();
    if (plugin == nullptr) {
        simrv::log::error("CFU plugin '{}' returned invalid plugin descriptor", path);
        dlclose(handle);
        return false;
    }

    if (plugin->api_version != SIMRV_CFU_PLUGIN_API_VERSION &&
        plugin->api_version != SIMRV_CFU_PLUGIN_API_VERSION_V1) {
        simrv::log::error("CFU plugin '{}' has incompatible API version {} (expected {} or {})",
                          path, plugin->api_version, SIMRV_CFU_PLUGIN_API_VERSION,
                          SIMRV_CFU_PLUGIN_API_VERSION_V1);
        dlclose(handle);
        return false;
    }

    if (plugin->execute == nullptr &&
        (plugin->api_version == SIMRV_CFU_PLUGIN_API_VERSION_V1 || plugin->execute64 == nullptr)) {
        simrv::log::error("CFU plugin '{}' has no execution callback", path);
        dlclose(handle);
        return false;
    }

    plugin_handle_ = handle;
    plugin_ = plugin;
    plugin_path_ = std::move(path_str);
    simrv::log::info("Loaded CFU plugin from '{}'{}", plugin_path_,
                     plugin_name().empty() ? "" : std::format(" ({})", plugin_name()));
    return true;
}

void CfuUnit::reset() {
    last_latency_ = default_latency_;
    if (raw_cfu_reset_ != nullptr) {
        raw_cfu_reset_();
    }
    if (plugin_ != nullptr && plugin_->reset != nullptr) {
        plugin_->reset();
    }
}

auto CfuUnit::execute(uint32_t funct7, uint32_t funct3, simrv::xlen::Register src1,
                      simrv::xlen::Register src2) -> simrv::xlen::Register {
    if (raw_cfu_op_ != nullptr) {
        last_latency_ = default_latency_;
        uint32_t res = raw_cfu_op_(static_cast<int>(funct3), static_cast<int>(funct7),
                                   static_cast<uint32_t>(src1), static_cast<uint32_t>(src2));
        return static_cast<simrv::xlen::Register>(res);
    }
    if (plugin_ != nullptr) {
        if (plugin_->api_version >= SIMRV_CFU_PLUGIN_API_VERSION && plugin_->execute64 != nullptr) {
            uint32_t latency = 1;
            uint64_t res = plugin_->execute64(funct7, funct3, static_cast<uint64_t>(src1),
                                              static_cast<uint64_t>(src2), &latency);
            last_latency_ = latency > 0 ? latency : 1;
            return static_cast<simrv::xlen::Register>(res);
        }
        if (plugin_->execute != nullptr) {
            uint32_t latency = 1;
            uint32_t res = plugin_->execute(funct7, funct3, static_cast<uint32_t>(src1),
                                            static_cast<uint32_t>(src2), &latency);
            last_latency_ = latency > 0 ? latency : 1;
            return static_cast<simrv::xlen::Register>(res);
        }
    }

    // Default built-in CFU behavior matching CFU-Proving-Ground's cfu.v:
    // assign rslt_o = (en_i) ? src1_i | src2_i : 0;
    last_latency_ = default_latency_;
    return src1 | src2;
}

auto CfuUnit::query_latency(uint32_t funct7, uint32_t funct3) const -> simrv::xlen::LatencyCycles {
    if (raw_cfu_op_ != nullptr) {
        return default_latency_;
    }
    if (plugin_ != nullptr) {
        if (plugin_->api_version >= SIMRV_CFU_PLUGIN_API_VERSION &&
            plugin_->query_latency != nullptr) {
            uint32_t lat = plugin_->query_latency(funct7, funct3);
            return lat > 0 ? lat : 1;
        }
        if (plugin_->api_version >= SIMRV_CFU_PLUGIN_API_VERSION &&
            plugin_->execute64 != nullptr) {
            uint32_t latency = 1;
            plugin_->execute64(funct7, funct3, 0, 0, &latency);
            return latency > 0 ? latency : 1;
        }
        if (plugin_->execute != nullptr) {
            uint32_t latency = 1;
            plugin_->execute(funct7, funct3, 0, 0, &latency);
            return latency > 0 ? latency : 1;
        }
    }
    return default_latency_;
}

auto CfuUnit::plugin_name() const noexcept -> std::string_view {
    if (raw_cfu_op_ != nullptr) {
        return "cfu-provingground-compat";
    }
    if (plugin_ != nullptr && plugin_->api_version >= SIMRV_CFU_PLUGIN_API_VERSION &&
        plugin_->name != nullptr) {
        return plugin_->name;
    }
    return {};
}

}  // namespace simrv::execute
