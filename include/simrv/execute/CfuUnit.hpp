/**
 * @file CfuUnit.hpp
 * @brief Extensible Custom Function Unit (CFU) execution interface with dynamic plugin support.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace simrv::execute {

/// C ABI for dynamic shared-library CFU plugins (.so).
extern "C" {

struct SimRvCfuPlugin {
    uint32_t (*execute)(uint32_t funct7, uint32_t funct3, uint32_t src1, uint32_t src2,
                        uint32_t* latency_cycles);
    void (*reset)();
};

using SimRvCfuPluginInitFn = const SimRvCfuPlugin* (*)();
}

/**
 * @class CfuUnit
 * @brief Manages CFU execution, including the default sample CFU (bitwise OR)
 *        and dynamically loaded shared library plugins.
 */
class CfuUnit {
   public:
    CfuUnit() = default;
    ~CfuUnit();

    CfuUnit(const CfuUnit&) = delete;
    auto operator=(const CfuUnit&) -> CfuUnit& = delete;
    CfuUnit(CfuUnit&& other) noexcept;
    auto operator=(CfuUnit&& other) noexcept -> CfuUnit&;

    /// Load an external shared library (.so) CFU plugin.
    auto load_plugin(std::string_view path) -> bool;

    /// Reset internal state of the CFU and any loaded plugin.
    void reset();

    /// Execute a CFU instruction. Returns the 32-bit computation result.
    [[nodiscard]] auto execute(uint32_t funct7, uint32_t funct3, uint32_t src1, uint32_t src2)
        -> uint32_t;

    /// Query the execution latency in cycles for the specified funct7/funct3 operation.
    [[nodiscard]] auto query_latency(uint32_t funct7, uint32_t funct3) const -> uint32_t;

    /// Returns the latency in cycles produced by the most recent execute() invocation.
    [[nodiscard]] auto last_latency() const noexcept -> uint32_t { return last_latency_; }

    /// Returns true if an external plugin is currently loaded.
    [[nodiscard]] auto has_plugin() const noexcept -> bool { return plugin_handle_ != nullptr; }

    /// Returns the path of the loaded plugin, if any.
    [[nodiscard]] auto plugin_path() const noexcept -> std::string_view { return plugin_path_; }

   private:
    void* plugin_handle_ = nullptr;
    const SimRvCfuPlugin* plugin_ = nullptr;
    std::string plugin_path_{};
    uint32_t last_latency_ = 1;
};

}  // namespace simrv::execute
