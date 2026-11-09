/**
 * @file BenchmarkRunner.cpp
 * @brief High-resolution full-system benchmark harness and reporting for SimRV.
 */
#include "simrv/util/BenchmarkRunner.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <numeric>
#include <print>
#include <vector>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/ExitCode.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/TelemetrySink.hpp"
#include "simrv/util/FormatUtil.hpp"

namespace simrv::util {

namespace {

class NullConsoleSink final : public simrv::core::IConsoleSink {
   public:
    void handle_char_write(char /*ch*/) override {}
};

auto compute_stats(std::vector<double> values) -> BenchmarkStats {
    BenchmarkStats stats{};
    if (values.empty()) return stats;

    const double n = static_cast<double>(values.size());
    const double sum = std::accumulate(values.begin(), values.end(), 0.0);
    stats.mean = sum / n;

    std::sort(values.begin(), values.end());
    stats.min = values.front();
    stats.max = values.back();

    const size_t sz = values.size();
    if (sz % 2 == 1) {
        stats.median = values[sz / 2];
    } else {
        stats.median = (values[sz / 2 - 1] + values[sz / 2]) / 2.0;
    }

    if (sz > 1) {
        double variance_sum = 0.0;
        for (const double val : values) {
            const double diff = val - stats.mean;
            variance_sum += diff * diff;
        }
        stats.stddev = std::sqrt(variance_sum / static_cast<double>(sz - 1));
        if (stats.mean > 0.0) {
            stats.cv_percent = (stats.stddev / stats.mean) * 100.0;
        }
    }
    return stats;
}

auto execution_mode_str(RequestedExecutionMode mode) -> std::string {
    switch (mode) {
        case RequestedExecutionMode::Fast:
            return "fast";
        case RequestedExecutionMode::Detailed:
            return "detailed";
        case RequestedExecutionMode::CycleAccurate:
            return "cycle-accurate";
    }
    return "fast";
}

void print_table(const BenchmarkReport& report) {
    const bool tty = terminal_color_enabled(STDOUT_FILENO);
    const auto cyan = [tty](std::string_view s) {
        return tty ? std::format("\033[1;36m{}\033[0m", s) : std::string(s);
    };
    const auto bold = [tty](std::string_view s) {
        return tty ? std::format("\033[1m{}\033[0m", s) : std::string(s);
    };
    const auto green = [tty](std::string_view s) {
        return tty ? std::format("\033[1;32m{}\033[0m", s) : std::string(s);
    };

    std::println(
        "{}",
        cyan("================================================================================"));
    std::println("  {}", bold("SimRV Full-System Benchmark Report"));
    std::println(
        "{}",
        cyan("================================================================================"));
    std::println("  {:<20}: {}", "Target ELF", report.elf_path);
    std::println("  {:<20}: {}", "Execution Mode", report.execution_mode);
    std::println("  {:<20}: {}", "Warmup Runs", report.warmups);
    std::println("  {:<20}: {}", "Measured Runs", report.runs);
    std::println(
        "{}",
        cyan("--------------------------------------------------------------------------------"));
    std::println("  {:>5}  {:>16}  {:>16}  {:>12}  {:>10}  {:>6}", "Run", "Wall Time (ms)",
                 "Instructions", "Cycles", "MIPS", "IPC");
    std::println(
        "{}",
        cyan("--------------------------------------------------------------------------------"));

    for (const auto& it : report.iterations) {
        const double ms = static_cast<double>(it.wall_time_ns.count()) / 1e6;
        std::println("  {:>5}  {:>16.3f}  {:>16}  {:>12}  {:>10.3f}  {:>6.2f}", it.iteration, ms,
                     format_with_commas(it.retired_instructions), format_with_commas(it.cycles),
                     it.mips, it.ipc);
    }

    std::println(
        "{}",
        cyan("--------------------------------------------------------------------------------"));
    std::println("  {:<16}  {:>16}  {:>12}  {:>8}", "Statistic", "Wall Time (ms)", "MIPS", "IPC");
    std::println(
        "{}",
        cyan("--------------------------------------------------------------------------------"));
    std::println("  {:<16}  {:>16.3f}  {:>12.3f}  {:>8.2f}", "Mean", report.wall_time_ms_stats.mean,
                 report.mips_stats.mean, report.ipc_stats.mean);
    std::println("  {:<16}  {:>16.3f}  {:>12.3f}  {:>8.2f}", "Median",
                 report.wall_time_ms_stats.median, report.mips_stats.median,
                 report.ipc_stats.median);
    std::println("  {:<16}  {:>16.3f}  {:>12.3f}  {:>8.2f}", "Min", report.wall_time_ms_stats.min,
                 report.mips_stats.min, report.ipc_stats.min);
    std::println("  {:<16}  {:>16.3f}  {:>12.3f}  {:>8.2f}", "Max", report.wall_time_ms_stats.max,
                 report.mips_stats.max, report.ipc_stats.max);
    std::println("  {:<16}  {:>10.3f} ({:4.2f}%)  {:>6.3f} ({:4.2f}%)  {:>8.2f}", "StdDev (CV%)",
                 report.wall_time_ms_stats.stddev, report.wall_time_ms_stats.cv_percent,
                 report.mips_stats.stddev, report.mips_stats.cv_percent, report.ipc_stats.stddev);
    std::println(
        "{}",
        cyan("================================================================================"));
    std::println("  {} Median Throughput: {} MIPS", green("-->"),
                 green(std::format("{:.3f}", report.mips_stats.median)));
    std::println(
        "{}",
        cyan("================================================================================"));
}

auto write_json(const BenchmarkReport& report, const std::string& path) -> bool {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;

    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"elf\": \"" << report.elf_path << "\",\n";
    out << "  \"execution_mode\": \"" << report.execution_mode << "\",\n";
    out << "  \"warmup_runs\": " << report.warmups << ",\n";
    out << "  \"measured_runs\": " << report.runs << ",\n";
    out << "  \"runs\": [\n";
    for (size_t i = 0; i < report.iterations.size(); ++i) {
        const auto& it = report.iterations[i];
        const double ms = static_cast<double>(it.wall_time_ns.count()) / 1e6;
        out << "    {\n";
        out << "      \"iteration\": " << it.iteration << ",\n";
        out << "      \"wall_time_ms\": " << ms << ",\n";
        out << "      \"wall_time_ns\": " << it.wall_time_ns.count() << ",\n";
        out << "      \"retired_instructions\": " << it.retired_instructions << ",\n";
        out << "      \"cycles\": " << it.cycles << ",\n";
        out << "      \"mips\": " << it.mips << ",\n";
        out << "      \"ipc\": " << it.ipc << "\n";
        out << "    }" << (i + 1 < report.iterations.size() ? "," : "") << "\n";
    }
    out << "  ],\n";
    out << "  \"summary\": {\n";
    out << "    \"wall_time_ms\": {\n";
    out << "      \"mean\": " << report.wall_time_ms_stats.mean << ",\n";
    out << "      \"median\": " << report.wall_time_ms_stats.median << ",\n";
    out << "      \"min\": " << report.wall_time_ms_stats.min << ",\n";
    out << "      \"max\": " << report.wall_time_ms_stats.max << ",\n";
    out << "      \"stddev\": " << report.wall_time_ms_stats.stddev << ",\n";
    out << "      \"cv_percent\": " << report.wall_time_ms_stats.cv_percent << "\n";
    out << "    },\n";
    out << "    \"mips\": {\n";
    out << "      \"mean\": " << report.mips_stats.mean << ",\n";
    out << "      \"median\": " << report.mips_stats.median << ",\n";
    out << "      \"min\": " << report.mips_stats.min << ",\n";
    out << "      \"max\": " << report.mips_stats.max << ",\n";
    out << "      \"stddev\": " << report.mips_stats.stddev << ",\n";
    out << "      \"cv_percent\": " << report.mips_stats.cv_percent << "\n";
    out << "    },\n";
    out << "    \"ipc\": {\n";
    out << "      \"mean\": " << report.ipc_stats.mean << ",\n";
    out << "      \"median\": " << report.ipc_stats.median << ",\n";
    out << "      \"min\": " << report.ipc_stats.min << ",\n";
    out << "      \"max\": " << report.ipc_stats.max << ",\n";
    out << "      \"stddev\": " << report.ipc_stats.stddev << ",\n";
    out << "      \"cv_percent\": " << report.ipc_stats.cv_percent << "\n";
    out << "    }\n";
    out << "  }\n";
    out << "}\n";
    return true;
}

}  // namespace

auto run_benchmark(const RuntimeOptions& options) -> int {
    const std::string target_elf =
        !options.fn_benchmark_elf.empty() ? options.fn_benchmark_elf : options.fn_memimg;
    if (target_elf.empty()) {
        simrv::log::error("Benchmark mode requires a target binary (-B or --benchmark)");
        return simrv::core::to_exit_code(simrv::core::HostExitCode::UsageError);
    }

    const uint32_t warmups = options.benchmark_warmups;
    const uint32_t runs = (options.benchmark_runs == 0) ? 5 : options.benchmark_runs;

    RuntimeOptions benchmark_opts = options;
    benchmark_opts.fn_memimg = target_elf;
    benchmark_opts.tuimode = false;
    benchmark_opts.explicit_cli_mode = true;
    benchmark_opts.fn_log.clear();
    benchmark_opts.fn_events.clear();
    benchmark_opts.fn_archtrace.clear();
    benchmark_opts.fn_traplog.clear();
    benchmark_opts.dlog_mode = false;
    benchmark_opts.traplog_mode = false;
    benchmark_opts.bp_trace = false;
    benchmark_opts.trace_enabled = false;
    benchmark_opts.fn_uart_transcript.clear();

    simrv::log::info("Running benchmark on '{}' (warmups: {}, runs: {})...", target_elf, warmups,
                     runs);

    // Warmup phase
    for (uint32_t w = 0; w < warmups; ++w) {
        auto machine_config = benchmark_opts.to_machine_config();
        if (const auto valid = machine_config.validate(); !valid) {
            simrv::log::error("Invalid machine configuration: {}", valid.error());
            return simrv::core::to_exit_code(simrv::core::HostExitCode::ConfigurationError);
        }
        auto machine = std::make_unique<simrv::core::Machine>(std::move(machine_config));
        auto applied = apply_runtime_options(machine.get(), benchmark_opts);
        if (!applied) {
            simrv::log::error("Failed to apply runtime options: {}", applied.error());
            return simrv::core::to_exit_code(simrv::core::HostExitCode::ConfigurationError);
        }
        const auto init_res = machine->initialize();
        if (!init_res) {
            simrv::log::error("Machine initialization failed: {}", init_res.error());
            return simrv::core::to_exit_code(simrv::core::HostExitCode::InitializationError);
        }
        machine->set_console_sink(std::make_shared<NullConsoleSink>());
        machine->run();
    }

    // Measurement phase
    BenchmarkReport report{
        .elf_path = target_elf,
        .execution_mode = execution_mode_str(options.execution_mode),
        .warmups = warmups,
        .runs = runs,
    };
    report.iterations.reserve(runs);

    std::vector<double> wall_ms_values;
    std::vector<double> mips_values;
    std::vector<double> ipc_values;
    wall_ms_values.reserve(runs);
    mips_values.reserve(runs);
    ipc_values.reserve(runs);

    for (uint32_t r = 0; r < runs; ++r) {
        auto machine_config = benchmark_opts.to_machine_config();
        if (const auto valid = machine_config.validate(); !valid) {
            simrv::log::error("Invalid machine configuration: {}", valid.error());
            return simrv::core::to_exit_code(simrv::core::HostExitCode::ConfigurationError);
        }
        auto machine = std::make_unique<simrv::core::Machine>(std::move(machine_config));
        auto applied = apply_runtime_options(machine.get(), benchmark_opts);
        if (!applied) {
            simrv::log::error("Failed to apply runtime options: {}", applied.error());
            return simrv::core::to_exit_code(simrv::core::HostExitCode::ConfigurationError);
        }
        const auto init_res = machine->initialize();
        if (!init_res) {
            simrv::log::error("Machine initialization failed: {}", init_res.error());
            return simrv::core::to_exit_code(simrv::core::HostExitCode::InitializationError);
        }
        machine->set_console_sink(std::make_shared<NullConsoleSink>());

        const auto start_wall = std::chrono::steady_clock::now();
        machine->run();
        const auto end_wall = std::chrono::steady_clock::now();

        const auto wall_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end_wall - start_wall);
        const double wall_sec = static_cast<double>(wall_ns.count()) / 1e9;
        const double wall_ms = static_cast<double>(wall_ns.count()) / 1e6;

        uint64_t icount = machine->retired_instruction_count();
        if (icount == 0) {
            for (size_t h = 0; h < machine->num_harts(); ++h) {
                icount += machine->hart(h).e_icount;
            }
        }
        const uint64_t ccount = machine->primary_hart().clint_mmio.mcycle;

        const double mips = (wall_sec > 0.0) ? (static_cast<double>(icount) / wall_sec / 1e6) : 0.0;
        const double ipc =
            (ccount > 0) ? (static_cast<double>(icount) / static_cast<double>(ccount)) : 0.0;

        report.iterations.push_back(BenchmarkIterationResult{
            .iteration = r + 1,
            .wall_time_ns = wall_ns,
            .retired_instructions = icount,
            .cycles = ccount,
            .mips = mips,
            .ipc = ipc,
        });

        wall_ms_values.push_back(wall_ms);
        mips_values.push_back(mips);
        ipc_values.push_back(ipc);
    }

    report.wall_time_ms_stats = compute_stats(std::move(wall_ms_values));
    report.mips_stats = compute_stats(std::move(mips_values));
    report.ipc_stats = compute_stats(std::move(ipc_values));

    print_table(report);

    if (!options.benchmark_json_out.empty()) {
        if (!write_json(report, options.benchmark_json_out)) {
            simrv::log::error("Failed to write benchmark JSON to {}", options.benchmark_json_out);
            return simrv::core::to_exit_code(simrv::core::HostExitCode::GeneralError);
        }
        simrv::log::info("Wrote benchmark results to {}", options.benchmark_json_out);
    }

    return simrv::core::to_exit_code(simrv::core::HostExitCode::Success);
}

}  // namespace simrv::util
