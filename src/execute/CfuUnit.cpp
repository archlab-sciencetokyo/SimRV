/**
 * @file CfuUnit.cpp
 * @brief Implementation of the extensible Custom Function Unit (CFU) subsystem.
 */
#include "simrv/execute/CfuUnit.hpp"

#include <dlfcn.h>

#include <utility>

#include "simrv/core/Logger.hpp"

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
      plugin_path_(std::move(other.plugin_path_)),
      last_latency_(other.last_latency_) {}

auto CfuUnit::operator=(CfuUnit&& other) noexcept -> CfuUnit& {
    if (this != &other) {
        if (plugin_handle_ != nullptr) {
            dlclose(plugin_handle_);
        }
        plugin_handle_ = std::exchange(other.plugin_handle_, nullptr);
        plugin_ = std::exchange(other.plugin_, nullptr);
        plugin_path_ = std::move(other.plugin_path_);
        last_latency_ = other.last_latency_;
    }
    return *this;
}

auto CfuUnit::load_plugin(std::string_view path) -> bool {
    if (plugin_handle_ != nullptr) {
        dlclose(plugin_handle_);
        plugin_handle_ = nullptr;
        plugin_ = nullptr;
        plugin_path_.clear();
    }

    std::string path_str(path);
    void* handle = dlopen(path_str.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        simrv::log::error("Failed to load CFU plugin '{}': {}", path, dlerror());
        return false;
    }

    auto init_fn = reinterpret_cast<SimRvCfuPluginInitFn>(dlsym(handle, "simrv_cfu_init"));
    if (init_fn == nullptr) {
        simrv::log::error("CFU plugin '{}' missing entry point 'simrv_cfu_init': {}", path,
                          dlerror());
        dlclose(handle);
        return false;
    }

    const SimRvCfuPlugin* plugin = init_fn();
    if (plugin == nullptr || plugin->execute == nullptr) {
        simrv::log::error("CFU plugin '{}' returned invalid plugin descriptor", path);
        dlclose(handle);
        return false;
    }

    plugin_handle_ = handle;
    plugin_ = plugin;
    plugin_path_ = std::move(path_str);
    simrv::log::info("Loaded CFU plugin from '{}'", plugin_path_);
    return true;
}

void CfuUnit::reset() {
    last_latency_ = 1;
    if (plugin_ != nullptr && plugin_->reset != nullptr) {
        plugin_->reset();
    }
}

auto CfuUnit::execute(uint32_t funct7, uint32_t funct3, uint32_t src1, uint32_t src2) -> uint32_t {
    if (plugin_ != nullptr && plugin_->execute != nullptr) {
        uint32_t latency = 1;
        uint32_t res = plugin_->execute(funct7, funct3, src1, src2, &latency);
        last_latency_ = latency > 0 ? latency : 1;
        return res;
    }

    // Default built-in CFU behavior matching CFU-Proving-Ground's cfu.v:
    // assign rslt_o = (en_i) ? src1_i | src2_i : 0;
    last_latency_ = 1;
    return src1 | src2;
}

auto CfuUnit::query_latency(uint32_t funct7, uint32_t funct3) const -> uint32_t {
    if (plugin_ != nullptr && plugin_->execute != nullptr) {
        uint32_t latency = 1;
        plugin_->execute(funct7, funct3, 0, 0, &latency);
        return latency > 0 ? latency : 1;
    }
    return 1;
}

}  // namespace simrv::execute
