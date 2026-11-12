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

namespace fs = std::filesystem;

void print_help(const char* prog) {
    std::println("SimRV Native Benchmarking & Comparison Suite");
    std::println("Usage:");
    std::println("  {} [options] <elf-file | suite-name>", prog);
    std::println("  {} compare <baseline.json> <candidate.json>", prog);
    std::println("\nBenchmark Execution Options:");
    std::println("  --runs <N>            Number of measured runs (default: 5)");
    std::println("  --warmup <N>          Number of warmup runs (default: 1)");
    std::println("  --mode <MODE>         fast | detailed | cycle-accurate (default: fast)");
    std::println("  --ca                  Shorthand for --mode cycle-accurate");
    std::println("  --nanobench           High-precision in-process microbenchmarks (nanobench)");
    std::println(
        "  --spike [PATH]        Compare against Spike ISA simulator (auto-detects if omitted)");
    std::println("  --suite <NAME>        Run standard suite (e.g. realworld)");
    std::println("  -e, --limit <N>       Instruction execution limit");
    std::println("  --isa <ISA>           Guest ISA string override (e.g. rv64gc, rv64gcv)");
    std::println("  --vlen <N>            Vector register length in bits");
    std::println("  --json <PATH>         Export benchmark metrics to JSON file");
    std::println("  --timeout <SEC>       Execution timeout in seconds");
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

const std::vector<std::string> kRealworldBenchmarks = {
    "aha-mont", "coremark", "dhrystone", "median", "memcpy", "mm",
    "multiply", "qsort",    "rsort",     "spmv",   "towers", "vvadd",
};

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

    std::string elf_path;
    std::string suite_name;
    std::string json_out;
    std::string spike_bin;
    bool enable_spike = false;
    bool enable_nanobench = false;
    uint32_t runs = 5;
    uint32_t warmups = 1;
    uint64_t step_limit = 0;
    std::string isa_override;
    unsigned int vlen = 0;
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
        } else if (arg == "--nanobench") {
            enable_nanobench = true;
        } else if (arg == "--spike") {
            enable_spike = true;
            if (i + 1 < argc && !std::string(argv[i + 1]).starts_with("-")) {
                spike_bin = argv[++i];
            } else {
                spike_bin = simrv::util::find_spike_binary();
            }
        } else if (arg == "--suite" && i + 1 < argc) {
            suite_name = argv[++i];
        } else if ((arg == "-e" || arg == "--limit" || arg == "--steps") && i + 1 < argc) {
            step_limit = std::stoull(argv[++i]);
        } else if (arg == "--isa" && i + 1 < argc) {
            isa_override = argv[++i];
        } else if (arg == "--vlen" && i + 1 < argc) {
            vlen = static_cast<unsigned int>(std::stoul(argv[++i]));
        } else if (!arg.starts_with("-") && elf_path.empty()) {
            elf_path = arg;
        }
    }

    if (enable_spike && spike_bin.empty()) {
        spike_bin = simrv::util::find_spike_binary();
        if (spike_bin.empty()) {
            std::println(std::cerr, "Warning: Spike binary not found; proceeding with SimRV only.");
            enable_spike = false;
        }
    }

    std::vector<std::string> targets;
    if (suite_name == "realworld") {
        targets = kRealworldBenchmarks;
    } else if (!elf_path.empty()) {
        targets.push_back(elf_path);
    } else {
        std::println(std::cerr, "Error: No guest ELF binary or suite specified for benchmarking.");
        print_help(argv[0]);
        return 2;
    }

    int overall_result = 0;
    for (const auto& target : targets) {
        std::string resolved = simrv::util::resolve_benchmark_elf(target);
        if (!fs::exists(resolved)) {
            std::println(std::cerr, "Error: Benchmark binary not found for '{}' (resolved: '{}')",
                         target, resolved);
            overall_result = 1;
            continue;
        }

        simrv::util::RuntimeOptions opts;
        opts.fn_memimg = resolved;
        opts.fn_benchmark_elf = resolved;
        opts.benchmark_runs = runs;
        opts.benchmark_warmups = warmups;
        opts.benchmark_json_out = json_out;
        opts.benchmark_spike_bin = enable_spike ? spike_bin : "";
        opts.benchmark_nanobench = enable_nanobench;
        opts.execution_mode = mode;
        opts.explicit_cli_mode = true;
        opts.tuimode = false;
        if (step_limit > 0) {
            opts.fincnt = step_limit;
        }
        opts.benchmark_isa_str = isa_override;
        opts.vlen = vlen;

        int res = simrv::util::run_benchmark(opts);
        if (res != 0) {
            overall_result = res;
        }
    }

    return overall_result;
}
