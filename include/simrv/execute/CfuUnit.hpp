/**
 * @file CfuUnit.hpp
 * @brief Extensible Custom Function Unit (CFU) execution interface with dynamic plugin support.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "simrv/execute/ExternalCfu.h"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::execute {

using ::SimRvCfuInitFn;
using ::SimRvCfuPlugin;
using SimRvCfuPluginInitFn = ::SimRvCfuInitFn;

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

    /// Execute a CFU instruction. Returns the register computation result.
    [[nodiscard]] auto execute(uint32_t funct7, uint32_t funct3, simrv::xlen::Register src1,
                               simrv::xlen::Register src2) -> simrv::xlen::Register;

    /// Query the execution latency in cycles for the specified funct7/funct3 operation.
    [[nodiscard]] auto query_latency(uint32_t funct7, uint32_t funct3) const
        -> simrv::xlen::LatencyCycles;

    /// Query additional stall cycles after the CFU issue cycle.
    [[nodiscard]] auto query_stall_cycles(uint32_t funct7, uint32_t funct3) const
        -> simrv::xlen::LatencyCycles {
        const auto total_cycles = query_latency(funct7, funct3);
        return total_cycles > 0 ? total_cycles - 1 : 0;
    }

    /// Set the latency used by the built-in CFU implementation.
    void set_default_latency(simrv::xlen::LatencyCycles latency) noexcept {
        default_latency_ = latency == 0 ? 1 : latency;
    }

    /// Returns the latency in cycles produced by the most recent execute() invocation.
    [[nodiscard]] auto last_latency() const noexcept -> simrv::xlen::LatencyCycles {
        return last_latency_;
    }

    /// Returns true if an external plugin is currently loaded.
    [[nodiscard]] auto has_plugin() const noexcept -> bool {
        return plugin_handle_ != nullptr && (plugin_ != nullptr || raw_cfu_op_ != nullptr);
    }

    /// Returns the path of the loaded plugin, if any.
    [[nodiscard]] auto plugin_path() const noexcept -> std::string_view { return plugin_path_; }

    /// Returns the name of the loaded plugin, if any.
    [[nodiscard]] auto plugin_name() const noexcept -> std::string_view;

   private:
    using RawCfuOpFn = uint32_t (*)(int funct3, int funct7, uint32_t rs1, uint32_t rs2);
    using RawCfuResetFn = void (*)();

    void* plugin_handle_ = nullptr;
    const SimRvCfuPlugin* plugin_ = nullptr;
    RawCfuOpFn raw_cfu_op_ = nullptr;
    RawCfuResetFn raw_cfu_reset_ = nullptr;
    std::string plugin_path_{};
    simrv::xlen::LatencyCycles last_latency_ = 1;
    simrv::xlen::LatencyCycles default_latency_ = 1;
};

}  // namespace simrv::execute
