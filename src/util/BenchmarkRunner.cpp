/**
 * @file BenchmarkRunner.cpp
 * @brief High-resolution full-system benchmark harness and reporting for SimRV.
 */
#define ANKERL_NANOBENCH_IMPLEMENT
#include "simrv/util/BenchmarkRunner.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <numeric>
#include <print>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/ExitCode.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/TelemetrySink.hpp"
#include "simrv/third_party/nanobench.h"
#include "simrv/util/FormatUtil.hpp"

namespace simrv::util {

namespace {

namespace fs = std::filesystem;

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

auto which(const std::string& name) -> std::string {
    if (name.empty()) return "";
    if (name.find('/') != std::string::npos) {
        if (fs::exists(name)) return name;
        return "";
    }
    const char* path_env = std::getenv("PATH");
    if (!path_env) return "";

    std::stringstream ss(path_env);
    std::string item;
    while (std::getline(ss, item, ':')) {
        if (item.empty()) continue;
        fs::path p = fs::path(item) / name;
        if (fs::exists(p)) {
            return p.string();
        }
    }
    return "";
}

struct ProcessOutput {
    int returncode = -1;
    std::string output;
    std::chrono::nanoseconds duration{0};
};

auto execute_command(const std::vector<std::string>& cmd, double timeout_sec = 60.0)
    -> ProcessOutput {
    ProcessOutput result;
    if (cmd.empty()) return result;

    int pipe_fd[2];
    if (::pipe(pipe_fd) != 0) return result;

    auto start_time = std::chrono::steady_clock::now();
    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
        return result;
    }

    if (pid == 0) {
        ::close(pipe_fd[0]);
        ::dup2(pipe_fd[1], STDOUT_FILENO);
        ::dup2(pipe_fd[1], STDERR_FILENO);
        ::close(pipe_fd[1]);

        std::vector<char*> c_args;
        c_args.reserve(cmd.size() + 1);
        for (const auto& s : cmd) {
            c_args.push_back(const_cast<char*>(s.c_str()));
        }
        c_args.push_back(nullptr);

        ::execvp(c_args[0], c_args.data());
        ::_exit(127);
    }

    ::close(pipe_fd[1]);
    int flags = ::fcntl(pipe_fd[0], F_GETFL, 0);
    ::fcntl(pipe_fd[0], F_SETFL, flags | O_NONBLOCK);

    std::array<char, 4096> buffer{};
    while (true) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();
        if (elapsed > timeout_sec) {
            ::kill(pid, SIGKILL);
            break;
        }

        ssize_t bytes = ::read(pipe_fd[0], buffer.data(), buffer.size());
        if (bytes > 0) {
            result.output.append(buffer.data(), static_cast<std::size_t>(bytes));
        }

        int status = 0;
        pid_t waited = ::waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            while ((bytes = ::read(pipe_fd[0], buffer.data(), buffer.size())) > 0) {
                result.output.append(buffer.data(), static_cast<std::size_t>(bytes));
            }
            result.returncode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    auto end_time = std::chrono::steady_clock::now();
    result.duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time);
    ::close(pipe_fd[0]);
    return result;
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

auto write_json(const BenchmarkReport& report, const std::string& path,
                const SpikeBenchmarkReport* spike_report = nullptr) -> bool {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;

    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"elf\": \"" << report.elf_path << "\",\n";
    out << "  \"execution_mode\": \"" << report.execution_mode << "\",\n";
    out << "  \"warmup_runs\": " << report.warmups << ",\n";
    out << "  \"measured_runs\": " << report.runs << ",\n";
    out << "  \"simrv\": {\n";
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
    out << "  }";

    if (spike_report && spike_report->success) {
        double speedup = (spike_report->mips_stats.median > 0.0)
                             ? (report.mips_stats.median / spike_report->mips_stats.median)
                             : 1.0;
        out << ",\n  \"spike\": {\n";
        out << "    \"executable\": \"" << spike_report->spike_path << "\",\n";
        out << "    \"wall_time_ms\": {\n";
        out << "      \"mean\": " << spike_report->wall_time_ms_stats.mean << ",\n";
        out << "      \"median\": " << spike_report->wall_time_ms_stats.median << ",\n";
        out << "      \"cv_percent\": " << spike_report->wall_time_ms_stats.cv_percent << "\n";
        out << "    },\n";
        out << "    \"mips\": {\n";
        out << "      \"mean\": " << spike_report->mips_stats.mean << ",\n";
        out << "      \"median\": " << spike_report->mips_stats.median << ",\n";
        out << "      \"cv_percent\": " << spike_report->mips_stats.cv_percent << "\n";
        out << "    }\n";
        out << "  },\n";
        out << "  \"comparison\": {\n";
        out << "    \"speedup_ratio\": " << speedup << ",\n";
        out << "    \"simrv_mips\": " << report.mips_stats.median << ",\n";
        out << "    \"spike_mips\": " << spike_report->mips_stats.median << "\n";
        out << "  }\n";
    } else {
        out << "\n";
    }

    out << "}\n";
    return true;
}

}  // namespace

auto find_spike_binary(const std::string& hint) -> std::string {
    if (!hint.empty() && fs::exists(hint)) {
        return hint;
    }

    std::vector<fs::path> candidates = {
        "tools/spike",
        "../tools/spike",
        fs::path(std::getenv("HOME") ? std::getenv("HOME") : "") / ".local" / "bin" / "spike",
        "/usr/local/bin/spike",
        "/opt/riscv/bin/spike",
    };

    for (const auto& c : candidates) {
        if (!c.empty() && fs::exists(c)) {
            return fs::absolute(c).string();
        }
    }

    std::string in_path = which("spike");
    if (!in_path.empty()) return in_path;
    return "";
}

auto resolve_benchmark_elf(const std::string& name) -> std::string {
    if (fs::is_regular_file(name)) {
        return name;
    }

    std::vector<std::string> suffixes = {".riscv", ".elf", ""};
    std::vector<fs::path> dirs = {
        "tests/riscv-tests/benchmarks",
        "/home/ren/workspace/lab/tests/riscv-tests/benchmarks",
        "/home/ren/workspace/lab/tests/coremark",
        "../../tests/riscv-tests/benchmarks",
        "../../tests/coremark",
    };

    for (const auto& dir : dirs) {
        for (const auto& sfx : suffixes) {
            fs::path cand = dir / (name + sfx);
            if (fs::is_regular_file(cand)) {
                return fs::absolute(cand).string();
            }
        }
    }

    return name;
}

auto run_spike_benchmark(const std::string& spike_bin, const std::string& elf_path, uint32_t runs,
                         uint32_t warmups, uint64_t instruction_limit, const std::string& isa,
                         uint64_t fallback_icount) -> SpikeBenchmarkReport {
    SpikeBenchmarkReport report{
        .spike_path = spike_bin,
        .elf_path = elf_path,
        .warmups = warmups,
        .runs = runs,
        .retired_instructions = instruction_limit > 0 ? instruction_limit : fallback_icount,
    };

    if (!fs::exists(spike_bin)) {
        simrv::log::error("Spike binary not found at '{}'", spike_bin);
        return report;
    }

    std::vector<std::string> cmd = {spike_bin};
    if (!isa.empty()) {
        cmd.push_back("--isa=" + isa);
    }
    if (instruction_limit > 0) {
        cmd.push_back("--instructions=" + std::to_string(instruction_limit));
    }
    cmd.push_back(elf_path);

    // Warmup phase
    for (uint32_t w = 0; w < warmups; ++w) {
        execute_command(cmd, 120.0);
    }

    std::vector<double> wall_ms_values;
    std::vector<double> mips_values;
    wall_ms_values.reserve(runs);
    mips_values.reserve(runs);
    report.wall_times.reserve(runs);

    static const std::regex minstret_re(R"re(minstret\s*=\s*([0-9]+))re");

    for (uint32_t r = 0; r < runs; ++r) {
        ProcessOutput out = execute_command(cmd, 120.0);
        if (out.returncode != 0) {
            simrv::log::error("Spike execution failed with exit code {}", out.returncode);
            return report;
        }

        double wall_ms = static_cast<double>(out.duration.count()) / 1e6;
        double wall_sec = static_cast<double>(out.duration.count()) / 1e9;
        report.wall_times.push_back(out.duration);

        if (report.retired_instructions == 0) {
            std::smatch m;
            if (std::regex_search(out.output, m, minstret_re)) {
                try {
                    report.retired_instructions = std::stoull(m[1].str());
                } catch (...) {
                    report.retired_instructions = instruction_limit;
                }
            } else {
                report.retired_instructions = instruction_limit;
            }
        }

        double mips = (wall_sec > 0.0 && report.retired_instructions > 0)
                          ? (static_cast<double>(report.retired_instructions) / wall_sec / 1e6)
                          : 0.0;

        wall_ms_values.push_back(wall_ms);
        mips_values.push_back(mips);
    }

    report.wall_time_ms_stats = compute_stats(std::move(wall_ms_values));
    report.mips_stats = compute_stats(std::move(mips_values));
    report.success = true;
    return report;
}

void print_comparative_table(const BenchmarkReport& simrv_report,
                             const SpikeBenchmarkReport& spike_report) {
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
    const auto yellow = [tty](std::string_view s) {
        return tty ? std::format("\033[1;33m{}\033[0m", s) : std::string(s);
    };

    double speedup = (spike_report.mips_stats.median > 0.0)
                         ? (simrv_report.mips_stats.median / spike_report.mips_stats.median)
                         : 1.0;
    double delta_pct = (speedup - 1.0) * 100.0;

    std::println(
        "{}",
        cyan("================================================================================"));
    std::println("  {}", bold("SimRV vs. Spike Comparative Benchmark Report"));
    std::println(
        "{}",
        cyan("================================================================================"));
    std::println("  {:<20}: {}", "Target ELF", simrv_report.elf_path);
    std::println("  {:<20}: {}", "Spike Binary", spike_report.spike_path);
    std::println("  {:<20}: {}", "Measured Runs", simrv_report.runs);
    std::println("  {:<20}: {}", "Warmup Runs", simrv_report.warmups);
    std::println(
        "{}",
        cyan("--------------------------------------------------------------------------------"));
    std::println("  {:<24}  {:>16}  {:>16}  {:>14}", "Performance Metric", "SimRV (Native)",
                 "Spike (ISA Sim)", "Speedup Ratio");
    std::println(
        "{}",
        cyan("--------------------------------------------------------------------------------"));

    std::println(
        "  {:<24}  {:>13.3f} ms  {:>13.3f} ms  {:>13.3f}x", "Median Execution Time",
        simrv_report.wall_time_ms_stats.median, spike_report.wall_time_ms_stats.median,
        (simrv_report.wall_time_ms_stats.median > 0.0)
            ? (spike_report.wall_time_ms_stats.median / simrv_report.wall_time_ms_stats.median)
            : 1.0);

    std::println("  {:<24}  {:>13.3f} ms  {:>13.3f} ms  {:>13.3f}x", "Mean Execution Time",
                 simrv_report.wall_time_ms_stats.mean, spike_report.wall_time_ms_stats.mean,
                 (simrv_report.wall_time_ms_stats.mean > 0.0)
                     ? (spike_report.wall_time_ms_stats.mean / simrv_report.wall_time_ms_stats.mean)
                     : 1.0);

    std::println("  {:<24}  {:>11.2f} MIPS  {:>11.2f} MIPS  {:>13.3f}x", "Throughput (Median)",
                 simrv_report.mips_stats.median, spike_report.mips_stats.median, speedup);

    std::println("  {:<24}  {:>14.2f}%  {:>14.2f}%  {:>14}", "Variance (CV%)",
                 simrv_report.wall_time_ms_stats.cv_percent,
                 spike_report.wall_time_ms_stats.cv_percent, "-");

    std::println(
        "{}",
        cyan("================================================================================"));

    std::string assess;
    if (speedup >= 1.0) {
        assess = green(std::format("SimRV is FASTER than Spike (+{:.1f}%)", delta_pct));
    } else if (speedup >= 0.70) {
        assess = yellow(std::format(
            "SimRV is within competitive range of Spike ({:.2f}x, {:+.1f}%)", speedup, delta_pct));
    } else {
        assess = std::format("Spike is faster ({:.2f}x)", speedup);
    }
    std::println("  {} Assessment: {}", green("-->"), assess);
    std::println(
        "{}",
        cyan("================================================================================"));
}

auto run_nanobenchmark(const RuntimeOptions& options) -> int {
    const std::string target_elf =
        !options.fn_benchmark_elf.empty() ? options.fn_benchmark_elf : options.fn_memimg;
    if (target_elf.empty()) {
        simrv::log::error("Nanobench mode requires a target binary");
        return simrv::core::to_exit_code(simrv::core::HostExitCode::UsageError);
    }

    const uint32_t warmups = options.benchmark_warmups;
    const uint32_t runs = (options.benchmark_runs == 0) ? 5 : options.benchmark_runs;

    RuntimeOptions benchmark_opts = options;
    benchmark_opts.fn_memimg = target_elf;
    benchmark_opts.tuimode = false;
    benchmark_opts.explicit_cli_mode = true;
    benchmark_opts.quiet = true;
    benchmark_opts.log_level = simrv::log::Level::Warn;
    benchmark_opts.fn_log.clear();
    benchmark_opts.fn_events.clear();
    benchmark_opts.fn_archtrace.clear();
    benchmark_opts.fn_traplog.clear();
    benchmark_opts.dlog_mode = false;
    benchmark_opts.traplog_mode = false;
    benchmark_opts.bp_trace = false;
    benchmark_opts.trace_enabled = false;
    benchmark_opts.fn_uart_transcript.clear();

    simrv::log::info("Running high-precision nanobench on '{}' (warmups: {}, min epochs: {})...",
                     target_elf, warmups, runs);

    auto base_config = benchmark_opts.to_machine_config();
    if (const auto valid = base_config.validate(); !valid) {
        simrv::log::error("Invalid machine configuration: {}", valid.error());
        return simrv::core::to_exit_code(simrv::core::HostExitCode::ConfigurationError);
    }

    std::string bench_title = fs::path(target_elf).filename().string();
    ankerl::nanobench::Bench bench;
    bench.title(std::format("SimRV Workload: {}", bench_title))
        .unit("run")
        .warmup(warmups)
        .minEpochIterations(std::max(1u, runs));

    uint64_t last_icount = 0;

    const auto orig_level = simrv::log::get_level();
    simrv::log::set_level(simrv::log::Level::Warn);

    bench.run(bench_title, [&]() {
        auto machine = std::make_unique<simrv::core::Machine>(base_config);
        auto applied = apply_runtime_options(machine.get(), benchmark_opts);
        (void)applied;
        auto init_res = machine->initialize();
        (void)init_res;
        machine->set_console_sink(std::make_shared<NullConsoleSink>());
        machine->run();
        last_icount = machine->retired_instruction_count();
        ankerl::nanobench::doNotOptimizeAway(last_icount);
    });

    simrv::log::set_level(orig_level);

    if (last_icount > 0 && !bench.results().empty()) {
        double median_sec =
            bench.results().front().median(ankerl::nanobench::Result::Measure::elapsed);
        if (median_sec > 0.0) {
            double mips = static_cast<double>(last_icount) / median_sec / 1e6;
            std::println(
                "\n--> High-Precision Nanobench Median Throughput: {:.2f} MIPS ({} insns in {:.3f} "
                "ms)\n",
                mips, format_with_commas(last_icount), median_sec * 1e3);
        }
    }

    return 0;
}

auto run_benchmark(const RuntimeOptions& options) -> int {
    const std::string target_elf =
        !options.fn_benchmark_elf.empty() ? options.fn_benchmark_elf : options.fn_memimg;
    if (target_elf.empty()) {
        simrv::log::error("Benchmark mode requires a target binary (-B or --benchmark)");
        return simrv::core::to_exit_code(simrv::core::HostExitCode::UsageError);
    }

    if (options.benchmark_nanobench) {
        return run_nanobenchmark(options);
    }

    const uint32_t warmups = options.benchmark_warmups;
    const uint32_t runs = (options.benchmark_runs == 0) ? 5 : options.benchmark_runs;

    RuntimeOptions benchmark_opts = options;
    benchmark_opts.fn_memimg = target_elf;
    benchmark_opts.tuimode = false;
    benchmark_opts.explicit_cli_mode = true;
    benchmark_opts.quiet = true;
    benchmark_opts.log_level = simrv::log::Level::Warn;
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

    const auto orig_level = simrv::log::get_level();
    simrv::log::set_level(simrv::log::Level::Warn);

    // Warmup phase
    for (uint32_t w = 0; w < warmups; ++w) {
        auto machine_config = benchmark_opts.to_machine_config();
        if (const auto valid = machine_config.validate(); !valid) {
            simrv::log::set_level(orig_level);
            simrv::log::error("Invalid machine configuration: {}", valid.error());
            return simrv::core::to_exit_code(simrv::core::HostExitCode::ConfigurationError);
        }
        auto machine = std::make_unique<simrv::core::Machine>(std::move(machine_config));
        auto applied = apply_runtime_options(machine.get(), benchmark_opts);
        if (!applied) {
            simrv::log::set_level(orig_level);
            simrv::log::error("Failed to apply runtime options: {}", applied.error());
            return simrv::core::to_exit_code(simrv::core::HostExitCode::ConfigurationError);
        }
        const auto init_res = machine->initialize();
        if (!init_res) {
            simrv::log::set_level(orig_level);
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
            simrv::log::set_level(orig_level);
            simrv::log::error("Invalid machine configuration: {}", valid.error());
            return simrv::core::to_exit_code(simrv::core::HostExitCode::ConfigurationError);
        }
        auto machine = std::make_unique<simrv::core::Machine>(std::move(machine_config));
        auto applied = apply_runtime_options(machine.get(), benchmark_opts);
        if (!applied) {
            simrv::log::set_level(orig_level);
            simrv::log::error("Failed to apply runtime options: {}", applied.error());
            return simrv::core::to_exit_code(simrv::core::HostExitCode::ConfigurationError);
        }
        const auto init_res = machine->initialize();
        if (!init_res) {
            simrv::log::set_level(orig_level);
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

    simrv::log::set_level(orig_level);

    report.wall_time_ms_stats = compute_stats(std::move(wall_ms_values));
    report.mips_stats = compute_stats(std::move(mips_values));
    report.ipc_stats = compute_stats(std::move(ipc_values));
    if (!report.iterations.empty()) {
        report.total_instructions_retired = report.iterations.front().retired_instructions;
    }

    print_table(report);

    SpikeBenchmarkReport spike_report{};
    if (!options.benchmark_spike_bin.empty()) {
        std::string spike_path = find_spike_binary(options.benchmark_spike_bin);
        if (!spike_path.empty()) {
            simrv::log::info("Running comparative benchmark with Spike: {}", spike_path);
            uint64_t limit = (options.fincnt == std::numeric_limits<simrv::xlen::Counter>::max())
                                 ? 0
                                 : options.fincnt;
            spike_report =
                run_spike_benchmark(spike_path, target_elf, runs, warmups, limit,
                                    options.benchmark_isa_str, report.total_instructions_retired);
            if (spike_report.retired_instructions == 0) {
                spike_report.retired_instructions = report.total_instructions_retired;
            }
            if (spike_report.success) {
                print_comparative_table(report, spike_report);
            }
        } else {
            simrv::log::warn("Spike binary '{}' could not be found; skipping Spike comparison.",
                             options.benchmark_spike_bin);
        }
    }

    if (!options.benchmark_json_out.empty()) {
        if (!write_json(report, options.benchmark_json_out,
                        spike_report.success ? &spike_report : nullptr)) {
            simrv::log::error("Failed to write benchmark JSON to {}", options.benchmark_json_out);
            return simrv::core::to_exit_code(simrv::core::HostExitCode::GeneralError);
        }
        simrv::log::info("Wrote benchmark results to {}", options.benchmark_json_out);
    }

    return simrv::core::to_exit_code(simrv::core::HostExitCode::Success);
}

}  // namespace simrv::util
