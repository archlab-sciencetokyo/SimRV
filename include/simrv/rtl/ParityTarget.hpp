/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Archlab, Science Tokyo
 */

#ifndef SIMRV_RTL_PARITY_TARGET_HPP
#define SIMRV_RTL_PARITY_TARGET_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/rtl/ParityPlugin.h"
#include "simrv/xlen/AddressTypes.hpp"

namespace simrv::rtl {

struct RetirementEvent {
    uint64_t cycle{0};
    uint64_t inst{0};
    simrv::xlen::VirtAddr pc{};
    uint32_t ir{0};

    auto operator==(const RetirementEvent&) const noexcept -> bool = default;
};

struct TimingDivergence {
    std::size_t index{0};
    simrv::xlen::VirtAddr pc{};
    uint64_t rtl_step_cycles{0};
    uint64_t simrv_step_cycles{0};
};

struct TraceComparison {
    std::size_t rtl_events{0};
    std::size_t simrv_events{0};
    std::optional<std::size_t> first_architectural_divergence;
    std::optional<TimingDivergence> first_timing_divergence;

    [[nodiscard]] auto is_match() const noexcept -> bool {
        return rtl_events > 0 && rtl_events == simrv_events &&
               !first_architectural_divergence.has_value();
    }
};

struct Benchmark {
    std::string name;
    std::string source;
};

struct Toolchain {
    std::filesystem::path gcc;
    std::filesystem::path objcopy;
    std::filesystem::path objdump;
};

/**
 * @brief Abstract base class for RTL parity target adapters.
 */
class ParityTarget {
   public:
    virtual ~ParityTarget() = default;

    [[nodiscard]] virtual auto name() const noexcept -> std::string_view = 0;
    [[nodiscard]] virtual auto description() const noexcept -> std::string_view = 0;

    virtual auto prepare(const SimRvRtlContext& ctx) -> bool = 0;
    virtual auto run_benchmark(const SimRvRtlContext& ctx, std::string_view bench_name,
                               const std::filesystem::path& elf_path, SimRvParityResult& out_result)
        -> bool = 0;
    virtual void cleanup(const SimRvRtlContext& ctx) = 0;
};

/**
 * @brief Discovers a usable RISC-V cross-compiler toolchain.
 */
auto find_riscv_toolchain() -> std::optional<Toolchain>;

/**
 * @brief Parses CYCLETRACE lines from simulator output.
 */
auto parse_retirement_trace(std::string_view text) -> std::vector<RetirementEvent>;

/**
 * @brief Compares two retirement traces for architectural and timing equivalence.
 */
auto compare_retirement_traces(std::span<const RetirementEvent> rtl,
                               std::span<const RetirementEvent> simrv) -> TraceComparison;

/**
 * @brief Returns the embedded microbenchmark suite.
 */
auto get_embedded_benchmarks() -> const std::vector<Benchmark>&;

/**
 * @brief Returns the standard crt0.S assembly source.
 */
auto get_standard_crt0() -> std::string_view;

/**
 * @brief Returns the standard link.ld linker script.
 */
auto get_standard_linker_script() -> std::string_view;

/**
 * @brief Declarative registry and dynamic loader for RTL target adapters.
 */
class ParityRegistry {
   public:
    static auto instance() -> ParityRegistry&;

    void register_target(std::unique_ptr<ParityTarget> target);
    auto load_plugin(const std::filesystem::path& plugin_path) -> bool;

    [[nodiscard]] auto get_target(std::string_view name) -> ParityTarget*;
    [[nodiscard]] auto all_targets() const -> const std::vector<std::unique_ptr<ParityTarget>>&;

   private:
    ParityRegistry();
    std::vector<std::unique_ptr<ParityTarget>> targets_;
    std::vector<void*> dl_handles_;
};

}  // namespace simrv::rtl

#endif  // SIMRV_RTL_PARITY_TARGET_HPP
