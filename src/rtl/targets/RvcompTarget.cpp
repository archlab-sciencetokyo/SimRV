/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Archlab, Science Tokyo
 */

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <regex>
#include <sstream>

#include "simrv/core/Logger.hpp"
#include "simrv/rtl/ParityTarget.hpp"

namespace simrv::rtl {

namespace {

auto run_child_capture(const std::vector<std::string>& args, const std::filesystem::path& cwd,
                       std::string& out_output) -> int {
    int pipe_fd[2];
    if (::pipe(pipe_fd) != 0) {
        return -1;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
        return -1;
    }

    if (pid == 0) {
        ::close(pipe_fd[0]);
        ::dup2(pipe_fd[1], STDOUT_FILENO);
        ::dup2(pipe_fd[1], STDERR_FILENO);
        ::close(pipe_fd[1]);

        if (!cwd.empty()) {
            std::error_code ec;
            std::filesystem::current_path(cwd, ec);
        }

        std::vector<char*> c_args;
        c_args.reserve(args.size() + 1);
        for (const auto& a : args) {
            c_args.push_back(const_cast<char*>(a.c_str()));
        }
        c_args.push_back(nullptr);

        ::execvp(c_args[0], c_args.data());
        ::_exit(127);
    }

    ::close(pipe_fd[1]);
    out_output.clear();
    std::array<char, 4096> buffer{};
    ssize_t bytes_read = 0;
    while ((bytes_read = ::read(pipe_fd[0], buffer.data(), buffer.size())) > 0) {
        out_output.append(buffer.data(), static_cast<std::size_t>(bytes_read));
    }
    ::close(pipe_fd[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

auto convert_binary_to_hex128(const std::filesystem::path& bin_path,
                              const std::filesystem::path& hex_path) -> bool {
    std::ifstream in(bin_path, std::ios::binary);
    if (!in.is_open()) return false;

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    in.close();

    // Pad to at least 16 KiB as expected by RVComp Verilator testbench
    constexpr std::size_t kCapacity = 16 * 1024;
    if (bytes.size() < kCapacity) {
        bytes.resize(kCapacity, 0);
    } else if (bytes.size() % 16 != 0) {
        bytes.resize(bytes.size() + (16 - (bytes.size() % 16)), 0);
    }

    std::ofstream out(hex_path);
    if (!out.is_open()) return false;

    for (std::size_t i = 0; i < bytes.size(); i += 16) {
        uint64_t low = 0;
        uint64_t high = 0;
        std::memcpy(&low, &bytes[i], 8);
        std::memcpy(&high, &bytes[i + 8], 8);
        out << std::format("{:016x}{:016x}\n", high, low);
    }
    return true;
}

auto extract_metric(const std::string& pattern, const std::string& text) -> uint64_t {
    std::regex re(pattern);
    std::smatch m;
    if (std::regex_search(text, m, re) && m.size() >= 2) {
        std::string val_str = m[1].str();
        val_str.erase(std::remove(val_str.begin(), val_str.end(), ','), val_str.end());
        try {
            return std::stoull(val_str);
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

class RvcompTarget final : public ParityTarget {
   public:
    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "rvcomp"; }

    [[nodiscard]] auto description() const noexcept -> std::string_view override {
        return "RVComp five-stage RV32IM core";
    }

    auto prepare(const SimRvRtlContext& ctx) -> bool override {
        std::filesystem::path work_dir(ctx.work_dir ? ctx.work_dir : "build/rvcomp_eval");
        std::filesystem::create_directories(work_dir);

        // Write crt0.S and link.ld
        std::ofstream crt0(work_dir / "crt0.S");
        if (crt0.is_open()) crt0 << get_standard_crt0();

        std::ofstream link(work_dir / "link.ld");
        if (link.is_open()) link << get_standard_linker_script();

        return resolve_simulator_binary(ctx).has_value();
    }

    auto run_benchmark(const SimRvRtlContext& ctx, std::string_view bench_name,
                       const std::filesystem::path& elf_path, SimRvParityResult& out_result)
        -> bool override {
        out_result.benchmark_name = bench_name.data();
        out_result.matched = 0;

        auto sim_bin = resolve_simulator_binary(ctx);
        if (!sim_bin) {
            std::snprintf(out_result.error_message, sizeof(out_result.error_message),
                          "RVComp Verilator simulator binary not found");
            return false;
        }

        std::filesystem::path work_dir(ctx.work_dir ? ctx.work_dir : "build/rvcomp_eval");
        std::filesystem::path bin_path = work_dir / std::format("{}.bin", bench_name);
        std::filesystem::path hex_path = work_dir / std::format("{}.128.hex", bench_name);

        // 1. objcopy ELF to binary
        std::string objcopy =
            ctx.objcopy_binary ? ctx.objcopy_binary : "riscv64-unknown-elf-objcopy";
        std::string out_dummy;
        int objcopy_rc = run_child_capture(
            {objcopy, "-O", "binary", elf_path.string(), bin_path.string()}, work_dir, out_dummy);
        if (objcopy_rc != 0) {
            std::snprintf(out_result.error_message, sizeof(out_result.error_message),
                          "objcopy failed for %s", elf_path.string().c_str());
            return false;
        }

        // 2. Convert binary to 128-bit hex
        if (!convert_binary_to_hex128(bin_path, hex_path)) {
            std::snprintf(out_result.error_message, sizeof(out_result.error_message),
                          "Failed to convert binary to hex128 for %s", bin_path.string().c_str());
            return false;
        }

        // 3. Run Verilator simulation
        std::vector<std::string> rtl_cmd = {
            sim_bin->string(),
            std::format("+mem_file={}", std::filesystem::absolute(hex_path).string()),
            "+max_cycles=10000000"};
        if (ctx.trace_dir && ctx.trace_dir[0] != '\0') {
            rtl_cmd.push_back("+parity_trace");
        }

        std::string rtl_output;
        run_child_capture(rtl_cmd, sim_bin->parent_path(), rtl_output);

        out_result.rtl_cycles = extract_metric(R"re(===> mcycle\s*:\s*(\d+))re", rtl_output);
        out_result.rtl_instructions =
            extract_metric(R"re(===> minstret\s*:\s*(\d+))re", rtl_output);

        // 4. Run SimRV
        std::string simrv = ctx.simrv_binary ? ctx.simrv_binary : "build/rv64-release/simrv";
        std::vector<std::string> simrv_cmd = {
            simrv, "--cli",           "--ca", "--mode",    "five-stage",
            "-m",  elf_path.string(), "-H",   "0x80000000"};
        if (ctx.config_path && ctx.config_path[0] != '\0') {
            simrv_cmd.push_back("--config");
            simrv_cmd.push_back(ctx.config_path);
        } else {
            simrv_cmd.push_back("--cpu-preset");
            simrv_cmd.push_back("rvcomp");
        }

        std::string simrv_output;
        run_child_capture(simrv_cmd, work_dir, simrv_output);

        out_result.simrv_cycles = extract_metric(
            R"re(Elapsed cycles \(clocks\)\s*:\s*[\d\.]*K?\s*\(([0-9,]+)\))re", simrv_output);
        out_result.simrv_instructions = extract_metric(
            R"re(Executed instructions\s*:\s*[\d\.]*K?\s*\(([0-9,]+)\))re", simrv_output);

        // 5. Compare traces if trace_dir is set
        if (ctx.trace_dir && ctx.trace_dir[0] != '\0') {
            std::filesystem::path trace_dir(ctx.trace_dir);
            std::filesystem::create_directories(trace_dir);

            auto rtl_trace = parse_retirement_trace(rtl_output);
            auto simrv_trace = parse_retirement_trace(simrv_output);
            auto comp = compare_retirement_traces(rtl_trace, simrv_trace);

            if (!comp.is_match()) {
                if (comp.first_architectural_divergence.has_value()) {
                    out_result.divergence_cycle = comp.first_architectural_divergence.value();
                    std::snprintf(out_result.error_message, sizeof(out_result.error_message),
                                  "Architectural divergence at event #%zu",
                                  comp.first_architectural_divergence.value());
                } else if (comp.first_timing_divergence.has_value()) {
                    out_result.divergence_cycle = comp.first_timing_divergence->index;
                    std::snprintf(
                        out_result.error_message, sizeof(out_result.error_message),
                        "Timing divergence at event #%zu (pc 0x%llx)",
                        comp.first_timing_divergence->index,
                        static_cast<unsigned long long>(comp.first_timing_divergence->pc.raw()));
                }
                return true;
            }
        }

        out_result.matched = (out_result.rtl_instructions > 0 &&
                              out_result.rtl_instructions == out_result.simrv_instructions &&
                              out_result.rtl_cycles == out_result.simrv_cycles);
        return true;
    }

    void cleanup(const SimRvRtlContext& /*ctx*/) override {}

   private:
    auto resolve_simulator_binary(const SimRvRtlContext& ctx)
        -> std::optional<std::filesystem::path> {
        if (ctx.target_binary && ctx.target_binary[0] != '\0') {
            std::filesystem::path p(ctx.target_binary);
            if (std::filesystem::exists(p)) return p;
        }

        const char* dir_hint = ctx.target_dir ? ctx.target_dir : std::getenv("RVCOMP_DIR");
        if (dir_hint) {
            std::filesystem::path p = std::filesystem::path(dir_hint) / "obj_dir/rvcom";
            if (std::filesystem::exists(p)) return p;
        }

        return std::nullopt;
    }
};

}  // namespace

auto create_rvcomp_target() -> std::unique_ptr<ParityTarget> {
    return std::make_unique<RvcompTarget>();
}

}  // namespace simrv::rtl
