/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Archlab, Science Tokyo
 */

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

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

auto run_cmd_capture(const std::vector<std::string>& args, const std::filesystem::path& cwd,
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

auto write_verilog_memory_include(const std::filesystem::path& out_path,
                                  std::string_view memory_name,
                                  const std::filesystem::path& bin_path, std::size_t max_bytes)
    -> bool {
    std::ifstream in(bin_path, std::ios::binary);
    if (!in.is_open()) return false;

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    in.close();

    if (bytes.size() > max_bytes) {
        bytes.resize(max_bytes);
    }
    if (bytes.size() % 4 != 0) {
        bytes.resize(bytes.size() + (4 - (bytes.size() % 4)), 0);
    }

    std::ofstream out(out_path);
    if (!out.is_open()) return false;

    out << "initial begin\n";
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        uint32_t word = 0;
        std::memcpy(&word, &bytes[i], 4);
        out << std::format("    {}[{}] = 32'h{:08x};\n", memory_name, i / 4, word);
    }
    out << "end\n";
    return true;
}

auto extract_cfu_metric(const std::string& pattern, const std::string& text) -> uint64_t {
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

class CfuPgTarget final : public ParityTarget {
   public:
    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "cfu-pg"; }

    [[nodiscard]] auto description() const noexcept -> std::string_view override {
        return "CFU Proving Ground RVProc core";
    }

    auto prepare(const SimRvRtlContext& ctx) -> bool override {
        std::filesystem::path work_dir(ctx.work_dir ? ctx.work_dir : "build/rtl_parity/cfu-pg");
        std::filesystem::create_directories(work_dir);

        return resolve_cfu_dir(ctx).has_value();
    }

    auto run_benchmark(const SimRvRtlContext& ctx, std::string_view bench_name,
                       const std::filesystem::path& elf_path, SimRvParityResult& out_result)
        -> bool override {
        out_result.benchmark_name = bench_name.data();
        out_result.matched = 0;

        auto cfu_dir = resolve_cfu_dir(ctx);
        if (!cfu_dir) {
            std::snprintf(
                out_result.error_message, sizeof(out_result.error_message),
                "CFU Proving Ground directory not found (set CFU_PG_DIR or pass --cfu-dir)");
            return false;
        }

        std::filesystem::path work_dir(ctx.work_dir ? ctx.work_dir : "build/rtl_parity/cfu-pg");
        std::filesystem::path rtl_dir = work_dir / "rtl";
        std::filesystem::create_directories(rtl_dir);

        // Copy and instrument RTL sources if not already prepared
        constexpr std::array<const char*, 5> kRtlFiles = {"top.v", "main.v", "proc.v", "cfu.v",
                                                          "config.vh"};
        for (const auto* fname : kRtlFiles) {
            auto src_path = *cfu_dir / fname;
            auto dst_path = rtl_dir / fname;
            if (!std::filesystem::exists(dst_path) && std::filesystem::exists(src_path)) {
                std::ifstream in(src_path);
                std::stringstream ss;
                ss << in.rdbuf();
                std::string content = ss.str();

                if (std::string_view(fname) == "top.v") {
                    std::string marker = "    wire sda, scl, dc, res;";
                    std::string inst =
                        "    always @(posedge clk) begin\n"
                        "        if ($test$plusargs(\"parity_trace\") && !m0.rst && !cpu_sim_fini "
                        "&&\n"
                        "            !m0.cpu.stall && m0.cpu.MaWb_v && !m0.cpu.stall_i) begin\n"
                        "            $write(\"CYCLETRACE cycle=%0d inst=%0d pc=%08x ir=%08x\\n\",\n"
                        "                   mcycle, minstret, m0.cpu.MaWb_pc, m0.cpu.MaWb_ir);\n"
                        "        end\n"
                        "    end\n\n";
                    auto pos = content.find(marker);
                    if (pos != std::string::npos) {
                        content.insert(pos, inst);
                    }
                }
                std::ofstream out(dst_path);
                out << content;
            }
        }

        // Generate imem and dmem from ELF
        std::filesystem::path imem_bin = work_dir / "imem.bin";
        std::filesystem::path dmem_bin = work_dir / "dmem.bin";
        std::string objcopy =
            ctx.objcopy_binary ? ctx.objcopy_binary : "riscv64-unknown-elf-objcopy";

        std::string dummy;
        run_cmd_capture(
            {objcopy, "-O", "binary", "--only-section=.text", elf_path.string(), imem_bin.string()},
            work_dir, dummy);
        run_cmd_capture({objcopy, "-O", "binary", "--only-section=.data", "--only-section=.rodata",
                         "--only-section=.bss", elf_path.string(), dmem_bin.string()},
                        work_dir, dummy);

        write_verilog_memory_include(rtl_dir / "memi.txt", "imem", imem_bin, 32 * 1024);
        write_verilog_memory_include(rtl_dir / "memd.txt", "dmem", dmem_bin, 16 * 1024);

        // Compile RTL with Verilator if simulator binary doesn't exist
        std::filesystem::path verilator_bin =
            ctx.verilator_binary ? ctx.verilator_binary : "verilator";
        std::filesystem::path top_sim = rtl_dir / "obj_dir/top";
        if (!std::filesystem::exists(top_sim)) {
            std::vector<std::string> vcmd = {verilator_bin.string(),
                                             "--binary",
                                             "--top-module",
                                             "top",
                                             "--Wno-WIDTHTRUNC",
                                             "--Wno-WIDTHEXPAND",
                                             "-o",
                                             "top",
                                             "top.v",
                                             "main.v",
                                             "proc.v",
                                             "cfu.v"};
            int vrc = run_cmd_capture(vcmd, rtl_dir, dummy);
            if (vrc != 0 || !std::filesystem::exists(top_sim)) {
                std::snprintf(out_result.error_message, sizeof(out_result.error_message),
                              "Verilator compilation failed for CFU-PG: %s", dummy.c_str());
                return false;
            }
        }

        // Run Verilator simulator
        std::vector<std::string> rtl_cmd = {top_sim.string()};
        if (ctx.trace_dir && ctx.trace_dir[0] != '\0') {
            rtl_cmd.push_back("+parity_trace");
        }
        std::string rtl_output;
        run_cmd_capture(rtl_cmd, rtl_dir, rtl_output);

        out_result.rtl_cycles = extract_cfu_metric(R"re(===> mcycle\s*:\s*(\d+))re", rtl_output);
        out_result.rtl_instructions =
            extract_cfu_metric(R"re(===> minstret\s*:\s*(\d+))re", rtl_output);

        // Run SimRV
        std::string simrv = ctx.simrv_binary ? ctx.simrv_binary : "build/rv64-release/simrv";
        std::vector<std::string> simrv_cmd = {simrv, "--cli",     "--ca", "-m", elf_path.string(),
                                              "-H",  "0x80000000"};
        if (ctx.config_path && ctx.config_path[0] != '\0') {
            simrv_cmd.push_back("--config");
            simrv_cmd.push_back(ctx.config_path);
        }

        std::string simrv_output;
        run_cmd_capture(simrv_cmd, work_dir, simrv_output);

        out_result.simrv_cycles = extract_cfu_metric(
            R"re(Elapsed cycles \(clocks\)\s*:\s*[\d\.]*K?\s*\(([0-9,]+)\))re", simrv_output);
        out_result.simrv_instructions = extract_cfu_metric(
            R"re(Executed instructions\s*:\s*[\d\.]*K?\s*\(([0-9,]+)\))re", simrv_output);

        out_result.matched = (out_result.rtl_instructions > 0 &&
                              out_result.rtl_instructions == out_result.simrv_instructions &&
                              out_result.rtl_cycles == out_result.simrv_cycles);
        return true;
    }

    void cleanup(const SimRvRtlContext& /*ctx*/) override {}

   private:
    auto resolve_cfu_dir(const SimRvRtlContext& ctx) -> std::optional<std::filesystem::path> {
        if (ctx.target_dir && ctx.target_dir[0] != '\0') {
            std::filesystem::path p(ctx.target_dir);
            if (std::filesystem::exists(p)) return p;
        }

        const char* env_dir = std::getenv("CFU_PG_DIR");
        if (env_dir) {
            std::filesystem::path p(env_dir);
            if (std::filesystem::exists(p)) return p;
        }

        return std::nullopt;
    }
};

}  // namespace

auto create_cfu_pg_target() -> std::unique_ptr<ParityTarget> {
    return std::make_unique<CfuPgTarget>();
}

}  // namespace simrv::rtl
