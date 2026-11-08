/**
 * @file BenchmarkRunner.hpp
 * @brief High-resolution full-system benchmark harness and reporting for SimRV.
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "simrv/util/CliParser.hpp"

namespace simrv::util {

/**
 * @struct BenchmarkIterationResult
 * @brief Metrics recorded for a single benchmark run.
 */
struct BenchmarkIterationResult {
    uint32_t iteration = 0;
    std::chrono::nanoseconds wall_time_ns{0};
    uint64_t retired_instructions = 0;
    uint64_t cycles = 0;
    double mips = 0.0;
    double ipc = 0.0;
};

/**
 * @struct BenchmarkStats
 * @brief Statistical summary across multiple benchmark runs.
 */
struct BenchmarkStats {
    double mean = 0.0;
    double median = 0.0;
    double min = 0.0;
    double max = 0.0;
    double stddev = 0.0;
    double cv_percent = 0.0;
};

/**
 * @struct BenchmarkReport
 * @brief Complete benchmark execution record including metadata and statistics.
 */
struct BenchmarkReport {
    std::string elf_path;
    std::string execution_mode;
    uint32_t warmups = 0;
    uint32_t runs = 0;
    std::vector<BenchmarkIterationResult> iterations{};
    BenchmarkStats wall_time_ms_stats{};
    BenchmarkStats mips_stats{};
    BenchmarkStats ipc_stats{};
    uint64_t total_instructions_retired = 0;
};

/**
 * @brief Run full-system benchmark iterations for an ELF binary.
 * @param options Runtime options configured from CLI arguments.
 * @return 0 on success, non-zero host exit code on failure.
 */
auto run_benchmark(const RuntimeOptions& options) -> int;

}  // namespace simrv::util
