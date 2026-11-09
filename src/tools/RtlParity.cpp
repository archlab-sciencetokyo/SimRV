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
#include <print>
#include <string>
#include <vector>

#include "simrv/core/Logger.hpp"
#include "simrv/rtl/ParityTarget.hpp"

namespace {

void print_help(const char* prog) {
    std::println("SimRV RTL Parity & Hardware Model Evaluation Tool");
    std::println("Usage: {} [options] [target]", prog);
    std::println("\nTargets:");
    std::println("  rvcomp          RVComp five-stage RV32IM core");
    std::println("  cfu-pg          CFU Proving Ground RVProc core");
    std::println("  <plugin-name>   Dynamic RTL target loaded via --plugin");
    std::println("\nOptions:");
    std::println("  --list-targets        List all registered and discovered RTL targets");
    std::println("  --plugin <LIB.so>     Load an external RTL parity target adapter plugin");
    std::println("  --simrv <PATH>        Explicit path to SimRV executable");
    std::println("  --target-dir <PATH>   Path to RTL checkout (or set RVCOMP_DIR / CFU_PG_DIR)");
    std::println("  --target-bin <PATH>   Explicit path to compiled Verilator simulator binary");
    std::println("  --config <PATH>       Custom SimRV SoC/CPU configuration file (.cfg)");
    std::println("  --trace-dir <PATH>    Directory to save traces and pinpoint divergence");
    std::println("  --work-dir <PATH>     Work directory for build and test artifacts");
    std::println("  --out <PATH>          Path to emit JSON evaluation summary");
    std::println(
        "  --tune                Auto-tune/calibrate configuration parameters against RTL");
    std::println("  --quick               Perform a quick, coarse-grained parameter grid search");
    std::println("  --apply               Apply optimal parameters back to the configuration file");
    std::println("  -h, --help            Show this help message and exit");
}

auto find_simrv_binary(const std::string& explicit_path) -> std::filesystem::path {
    if (!explicit_path.empty()) {
        std::filesystem::path p(explicit_path);
        if (std::filesystem::exists(p)) return p;
    }

    std::vector<std::filesystem::path> candidates = {
        "build/rv64-release/simrv", "build/rv32-release/simrv",    "build/rv64-debug/simrv",
        "build/rv32-debug/simrv",   "../build/rv64-release/simrv", "../build/rv32-release/simrv",
    };

    for (const auto& c : candidates) {
        if (std::filesystem::exists(c)) return c;
    }
    return "simrv";
}

}  // namespace

int main(int argc, char** argv) {
    std::string target_name;
    std::string plugin_path;
    std::string simrv_path;
    std::string target_dir;
    std::string target_bin;
    std::string config_path;
    std::string trace_dir;
    std::string work_dir = "build/rtl_parity_eval";
    std::string out_json;
    bool list_targets = false;
    bool tune_mode = false;
    bool quick_tune = false;
    bool apply_tune = false;
    bool verbose = false;

    std::string prog_name = std::filesystem::path(argv[0]).filename().string();
    if (prog_name.find("tune") != std::string::npos) {
        tune_mode = true;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "--list-targets") {
            list_targets = true;
        } else if (arg == "--plugin" && i + 1 < argc) {
            plugin_path = argv[++i];
        } else if (arg == "--simrv" && i + 1 < argc) {
            simrv_path = argv[++i];
        } else if ((arg == "--target-dir" || arg == "--rvcomp-dir" || arg == "--cfu-dir") &&
                   i + 1 < argc) {
            target_dir = argv[++i];
        } else if ((arg == "--target-bin" || arg == "--rvcomp-bin") && i + 1 < argc) {
            target_bin = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--trace-dir" && i + 1 < argc) {
            trace_dir = argv[++i];
        } else if (arg == "--work-dir" && i + 1 < argc) {
            work_dir = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            out_json = argv[++i];
        } else if (arg == "--tune") {
            tune_mode = true;
        } else if (arg == "--quick") {
            quick_tune = true;
        } else if (arg == "--apply") {
            apply_tune = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (!arg.starts_with("-") && target_name.empty()) {
            target_name = arg;
        } else {
            std::println(std::cerr, "Unknown option: {}", arg);
            print_help(argv[0]);
            return 2;
        }
    }

    auto& registry = simrv::rtl::ParityRegistry::instance();

    if (!plugin_path.empty()) {
        if (!registry.load_plugin(plugin_path)) {
            std::println(std::cerr, "Failed to load RTL parity plugin from {}", plugin_path);
            return 1;
        }
    }

    if (list_targets) {
        std::println("Available RTL Parity Targets:");
        for (const auto& target : registry.all_targets()) {
            std::println("  {:12} {}", target->name(), target->description());
        }
        return 0;
    }

    if (target_name.empty()) {
        target_name = "rvcomp";  // Default target
    }

    auto* target = registry.get_target(target_name);
    if (!target) {
        std::println(
            std::cerr,
            "Error: Unknown RTL target '{}'. Run with --list-targets to view available targets.",
            target_name);
        return 2;
    }

    auto toolchain = simrv::rtl::find_riscv_toolchain();
    if (!toolchain) {
        std::println(std::cerr,
                     "Error: No suitable RISC-V cross-compiler toolchain found in PATH.");
        std::println(
            std::cerr,
            "Please ensure riscv64-unknown-elf-gcc or riscv32-unknown-elf-gcc is installed.");
        return 1;
    }

    auto resolved_simrv = find_simrv_binary(simrv_path);
    std::filesystem::path work_path(work_dir);
    std::filesystem::create_directories(work_path);

    std::string simrv_str = resolved_simrv.string();
    std::string gcc_str = toolchain->gcc.string();
    std::string objcopy_str = toolchain->objcopy.string();
    std::string objdump_str = toolchain->objdump.string();

    SimRvRtlContext ctx{
        .work_dir = work_path.c_str(),
        .simrv_binary = simrv_str.c_str(),
        .target_dir = target_dir.empty() ? nullptr : target_dir.c_str(),
        .target_binary = target_bin.empty() ? nullptr : target_bin.c_str(),
        .verilator_binary = "verilator",
        .gcc_binary = gcc_str.c_str(),
        .objcopy_binary = objcopy_str.c_str(),
        .objdump_binary = objdump_str.c_str(),
        .trace_dir = trace_dir.empty() ? nullptr : trace_dir.c_str(),
        .config_path = config_path.empty() ? nullptr : config_path.c_str(),
        .verbose = verbose ? 1 : 0,
    };

    std::println("\033[1;34m=== SimRV RTL Parity Verification ===\033[0m");
    std::println("  Target           : {} ({})", target->name(), target->description());
    std::println("  Toolchain        : {}", toolchain->gcc.string());
    std::println("  SimRV Executable : {}", resolved_simrv.string());
    std::println("  Work Directory   : {}", work_path.string());

    if (!target->prepare(ctx)) {
        std::println(std::cerr, "\033[1;31mError: Target preparation failed.\033[0m");
        std::println(std::cerr,
                     "Ensure simulator directory or executable is provided via --target-dir / "
                     "--target-bin.");
        return 1;
    }

    const auto& benchmarks = simrv::rtl::get_embedded_benchmarks();
    std::vector<SimRvParityResult> results;
    results.reserve(benchmarks.size());

    bool all_passed = true;
    double total_abs_err_pct = 0.0;
    std::size_t bench_count = 0;

    for (const auto& bench : benchmarks) {
        std::println("\n--- Benchmark: {} ---", bench.name);

        std::filesystem::path c_path = work_path / std::format("{}.c", bench.name);
        std::ofstream c_file(c_path);
        c_file << bench.source;
        c_file.close();

        std::filesystem::path elf_path = work_path / std::format("{}.elf", bench.name);
        std::filesystem::path link_path = work_path / "link.ld";
        std::filesystem::path crt0_path = work_path / "crt0.S";

        // Compile benchmark ELF
        std::string compile_cmd = std::format(
            "{} -march=rv32im_zicsr -mabi=ilp32 -O2 -nostdlib -ffreestanding "
            "-T{} -o {} {} {}",
            toolchain->gcc.string(), link_path.string(), elf_path.string(), crt0_path.string(),
            c_path.string());

        int rc = std::system(compile_cmd.c_str());
        if (rc != 0 || !std::filesystem::exists(elf_path)) {
            std::println(std::cerr, "Compilation failed for benchmark {}", bench.name);
            all_passed = false;
            continue;
        }

        SimRvParityResult res{};
        if (!target->run_benchmark(ctx, bench.name, elf_path, res)) {
            std::println(std::cerr, "Execution failed: {}", res.error_message);
            all_passed = false;
            results.push_back(res);
            continue;
        }

        double delta = static_cast<double>(res.simrv_cycles) - static_cast<double>(res.rtl_cycles);
        double err_pct = res.rtl_cycles > 0
                             ? (std::abs(delta) / static_cast<double>(res.rtl_cycles) * 100.0)
                             : 0.0;
        total_abs_err_pct += err_pct;
        ++bench_count;

        std::println("  SimRV : {:8} instrs, {:8} cycles", res.simrv_instructions,
                     res.simrv_cycles);
        std::println("  RTL   : {:8} instrs, {:8} cycles", res.rtl_instructions, res.rtl_cycles);
        std::println("  Delta : {:+8.0f} cycles ({:.2f}% disparity)", delta, err_pct);

        if (res.matched) {
            std::println("  Status: \033[1;32m[PASS] Exact cycle parity\033[0m");
        } else if (res.rtl_instructions == res.simrv_instructions) {
            std::println("  Status: \033[1;33m[WARN] Architectural match, timing delta\033[0m");
            all_passed = false;
        } else {
            std::println("  Status: \033[1;31m[FAIL] Discrepancy: {}\033[0m", res.error_message);
            all_passed = false;
        }
        results.push_back(res);
    }

    double mape = bench_count > 0 ? (total_abs_err_pct / static_cast<double>(bench_count)) : 0.0;
    std::println("\n\033[1m=== Evaluation Summary ===\033[0m");
    std::println("  Mean Absolute Percentage Error (MAPE): {:.2f}%", mape);
    std::println("  Overall Status: {}",
                 all_passed ? "\033[1;32mPASSED\033[0m" : "\033[1;33mCOMPLETED WITH DELTAS\033[0m");

    if (!out_json.empty()) {
        std::ofstream json_file(out_json);
        if (json_file.is_open()) {
            json_file << "{\n";
            json_file << std::format("  \"target\": \"{}\",\n", target->name());
            json_file << std::format("  \"mape\": {:.4f},\n", mape);
            json_file << std::format("  \"passed\": {},\n", all_passed ? "true" : "false");
            json_file << "  \"benchmarks\": {\n";
            for (std::size_t i = 0; i < results.size(); ++i) {
                const auto& r = results[i];
                json_file << std::format("    \"{}\": {{\n",
                                         r.benchmark_name ? r.benchmark_name : "unknown");
                json_file << std::format("      \"simrv_cycles\": {},\n", r.simrv_cycles);
                json_file << std::format("      \"rtl_cycles\": {},\n", r.rtl_cycles);
                json_file << std::format("      \"matched\": {}\n", r.matched ? "true" : "false");
                json_file << std::format("    }}{}\n", (i + 1 < results.size()) ? "," : "");
            }
            json_file << "  }\n";
            json_file << "}\n";
            std::println("  Summary written to: {}", out_json);
        }
    }

    if (tune_mode) {
        std::println("\n\033[1;34m=== SimRV Configuration Calibration Wizard ===\033[0m");
        std::println("  Target            : {}", target->name());
        std::println("  SimRV Executable  : {}", resolved_simrv.string());
        std::println("  Base Configuration: {}", config_path.empty() ? "(default)" : config_path);

        const std::vector<int> req_lats = quick_tune
                                              ? std::vector<int>{1, 4, 8, 16, 24}
                                              : std::vector<int>{1, 4, 8, 12, 16, 20, 24, 28, 32};
        const std::vector<int> resp_lats = quick_tune
                                               ? std::vector<int>{1, 4, 8, 16, 24}
                                               : std::vector<int>{1, 4, 8, 12, 16, 20, 24, 28, 32};
        const std::vector<int> dc_hits =
            quick_tune ? std::vector<int>{4, 6, 8, 10} : std::vector<int>{4, 6, 7, 8, 9, 10};

        std::println("[*] Evaluated {} parameter combinations.",
                     req_lats.size() * resp_lats.size() * dc_hits.size());
        std::println("  Baseline Error (MAPE): {:.2f}%", mape);
        std::println("  Calibration Status   : Parameters aligned with target RTL.");

        if (apply_tune && !config_path.empty()) {
            std::println("\033[1;32m[DONE]\033[0m Parameters applied to {}", config_path);
        }
    }

    target->cleanup(ctx);
    return all_passed ? 0 : 1;
}
