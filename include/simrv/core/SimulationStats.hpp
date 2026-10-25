/**
 * @file SimulationStats.hpp
 * @brief Unified simulation statistics and telemetry snapshot framework.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace simrv::core {

class Machine;

/**
 * @struct Stat
 * @brief Strongly-typed named simulation metric per modern simulator conventions (gem5/Spike).
 */
template <typename T>
struct Stat {
    std::string_view name{};
    std::string_view description{};
    T value{};

    constexpr operator const T&() const noexcept { return value; }
    constexpr auto operator*() const noexcept -> const T& { return value; }
};

/**
 * @struct HartStatsSnapshot
 * @brief Simulation telemetry captured for an individual hardware thread (hart).
 */
struct HartStatsSnapshot {
    uint32_t hart_id = 0;
    uint64_t retired_instructions = 0;
    uint64_t cycles = 0;
    double ipc = 0.0;

    // L1 Instruction Cache
    uint64_t icache_hits = 0;
    uint64_t icache_misses = 0;
    double icache_hit_rate = 0.0;

    // L1 Data Cache
    uint64_t dcache_hits = 0;
    uint64_t dcache_misses = 0;
    double dcache_hit_rate = 0.0;

    // Branch Predictor
    uint64_t branches = 0;
    uint64_t branch_hits = 0;
    uint64_t branch_misses = 0;
    double branch_accuracy = 0.0;
};

/**
 * @struct MachineStatsSnapshot
 * @brief Unified telemetry snapshot spanning all harts, cache hierarchy, bus, and coherence.
 */
struct MachineStatsSnapshot {
    uint64_t total_retired_instructions = 0;
    uint64_t total_cycles = 0;
    double aggregate_ipc = 0.0;
    std::vector<HartStatsSnapshot> harts{};

    // Memory Hierarchy & Coherence
    uint64_t coherence_acq = 0;
    uint64_t coherence_probe = 0;
    uint64_t coherence_release = 0;
    uint64_t coherence_invalidation = 0;
    uint64_t bus_delayed_requests = 0;
    uint64_t bus_contention_penalty = 0;

    [[nodiscard]] auto to_json() const -> std::string;
    [[nodiscard]] auto to_string() const -> std::string;
};

/**
 * @brief Captures a consistent point-in-time statistics snapshot of the machine.
 */
[[nodiscard]] auto take_stats_snapshot(const Machine& machine) -> MachineStatsSnapshot;

}  // namespace simrv::core

namespace simrv {
using core::HartStatsSnapshot;
using core::MachineStatsSnapshot;
using core::Stat;
using core::take_stats_snapshot;
}  // namespace simrv

