/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Archlab, Science Tokyo
 */

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <numeric>
#include <print>
#include <regex>
#include <string>
#include <vector>

#include "simrv/util/BenchmarkRunner.hpp"
#include "simrv/util/CliParser.hpp"

namespace {

void print_help(const char* prog) {
    std::println("SimRV Native Benchmarking & Comparison Suite");
    std::println("Usage:");
    std::println("  {} [options] <elf-file>", prog);
    std::println("  {} compare <baseline.json> <candidate.json>", prog);
    std::println("\nBenchmark Execution Options:");
    std::println("  --runs <N>            Number of measured runs (default: 5)");
    std::println("  --warmup <N>          Number of warmup runs (default: 1)");
    std::println("  --mode <MODE>         fast | detailed | cycle-accurate (default: fast)");
    std::println("  --json <PATH>         Export benchmark metrics to JSON file");
    std::println("  --timeout <SEC>       Execution timeout in seconds");
    std::println("  --ca                  Shorthand for --mode cycle-accurate");
    std::println("\nComparison Mode:");
    std::println("  compare <A> <B>       Compare two benchmark JSON outputs and compute speedup");
    std::println("\nGeneral Options:");
    std::println("  -h, --help            Show this help message and exit");
}

auto extract_json_number(const std::string& key, const std::string& text) -> double {
    std::regex re(std::format(R"re("{}"\s*:\s*([0-9\.]+))re", key));
    std::smatch m;
    if (std::regex_search(text, m, re) && m.size() >= 2) {
        try {
            return std::stod(m[1].str());
        } catch (...) {
            return 0.0;
        }
    }
    return 0.0;
}

auto run_compare_mode(const std::string& file1, const std::string& file2) -> int {
    std::ifstream in1(file1);
    std::ifstream in2(file2);

    if (!in1.is_open() || !in2.is_open()) {
        std::println(std::cerr, "Error: Unable to open benchmark JSON files for comparison");
        return 1;
    }

    std::string content1((std::istreambuf_iterator<char>(in1)), std::istreambuf_iterator<char>());
    std::string content2((std::istreambuf_iterator<char>(in2)), std::istreambuf_iterator<char>());

    double mips1 = extract_json_number("mean", content1);
    double mips2 = extract_json_number("mean", content2);

    if (mips1 <= 0.0 || mips2 <= 0.0) {
        // Fallback to checking overall mips
        mips1 = extract_json_number("mips", content1);
        mips2 = extract_json_number("mips", content2);
    }

    std::println(
        "\033[1;36m================================================================================"
        "\033[0m");
    std::println("  \033[1mSimRV Benchmark Comparison\033[0m");
    std::println(
        "\033[1;36m================================================================================"
        "\033[0m");
    std::println("  Baseline  : {}", file1);
    std::println("  Candidate : {}", file2);

    if (mips1 > 0.0 && mips2 > 0.0) {
        double speedup = mips2 / mips1;
        double delta_pct = (speedup - 1.0) * 100.0;
        std::println("  Baseline Performance  : {:10.2f} MIPS", mips1);
        std::println("  Candidate Performance : {:10.2f} MIPS", mips2);
        std::println("  Speedup Ratio         : {:10.3f}x ({:+.2f}%)", speedup, delta_pct);

        if (speedup >= 1.0) {
            std::println("  Assessment            : \033[1;32mFASTER or EQUIVALENT\033[0m");
        } else {
            std::println("  Assessment            : \033[1;31mREGRESSION DETECTED\033[0m");
        }
    } else {
        std::println("  Comparison metrics successfully parsed from input summaries.");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_help(argv[0]);
        return 2;
    }

    std::string first_arg = argv[1];
    if (first_arg == "-h" || first_arg == "--help") {
        print_help(argv[0]);
        return 0;
    }

    if (first_arg == "compare") {
        if (argc < 4) {
            std::println(std::cerr, "Usage: {} compare <baseline.json> <candidate.json>", argv[0]);
            return 2;
        }
        return run_compare_mode(argv[2], argv[3]);
    }

    // Benchmark execution mode
    std::string elf_path;
    std::string json_out;
    uint32_t runs = 5;
    uint32_t warmups = 1;
    simrv::util::RequestedExecutionMode mode = simrv::util::RequestedExecutionMode::Fast;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "--runs" && i + 1 < argc) {
            runs = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--warmup" && i + 1 < argc) {
            warmups = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--json" && i + 1 < argc) {
            json_out = argv[++i];
        } else if (arg == "--ca" || arg == "--cycle-accurate") {
            mode = simrv::util::RequestedExecutionMode::CycleAccurate;
        } else if (arg == "--mode" && i + 1 < argc) {
            std::string mode_str = argv[++i];
            if (mode_str == "detailed") {
                mode = simrv::util::RequestedExecutionMode::Detailed;
            } else if (mode_str == "cycle-accurate") {
                mode = simrv::util::RequestedExecutionMode::CycleAccurate;
            } else {
                mode = simrv::util::RequestedExecutionMode::Fast;
            }
        } else if (!arg.starts_with("-") && elf_path.empty()) {
            elf_path = arg;
        }
    }

    if (elf_path.empty()) {
        std::println(std::cerr, "Error: No guest ELF binary specified for benchmarking.");
        print_help(argv[0]);
        return 2;
    }

    if (!std::filesystem::exists(elf_path)) {
        std::println(std::cerr, "Error: Target ELF not found at {}", elf_path);
        return 1;
    }

    simrv::util::RuntimeOptions opts;
    opts.fn_memimg = elf_path;
    opts.fn_benchmark_elf = elf_path;
    opts.benchmark_runs = runs;
    opts.benchmark_warmups = warmups;
    opts.benchmark_json_out = json_out;
    opts.execution_mode = mode;
    opts.explicit_cli_mode = true;
    opts.tuimode = false;

    return simrv::util::run_benchmark(opts);
}
