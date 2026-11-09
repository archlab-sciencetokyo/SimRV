/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Archlab, Science Tokyo
 */

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <print>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;

void print_help(const char* prog) {
    std::println("SimRV RISC-V Vector (RVV 1.0) Test Runner");
    std::println("Usage: {} [options]", prog);
    std::println("\nRequired Options:");
    std::println("  --simrv <PATH>              Path to SimRV binary");
    std::println("  --xlen <32|64>              Target XLEN (32 or 64)");
    std::println("  --gcc <PATH>                Path to RISC-V GCC cross-compiler");
    std::println("  --objcopy <PATH>            Path to objcopy binary");
    std::println("  --nm <PATH>                 Path to nm binary");
    std::println("  --work-dir <PATH>           Path to directory for artifacts");
    std::println(
        "  --vector-tests-dir <PATH>   Path to chipsalliance/riscv-vector-tests repository");
    std::println("\nOptional Options:");
    std::println("  --vlen <N>                  Vector register length in bits (default: 256)");
    std::println(
        "  --jobs, -j <N>              Number of parallel worker jobs (default: CPU count)");
    std::println("  --pattern <REGEX>           Generator instruction-name regex (default: .*)");
    std::println("  --generator <PATH>          Prebuilt vector test generator binary");
    std::println(
        "  --configs-dir <PATH>        Generator configs directory (default: <dir>/configs/v)");
    std::println("  -h, --help                  Show this help message and exit");
}

struct ProcessResult {
    int returncode = -1;
    bool timed_out = false;
    std::string stdout_output;
    std::string stderr_output;
};

auto run_cmd(const std::vector<std::string>& cmd, double timeout_sec = 60.0,
             const std::string& cwd = "") -> ProcessResult {
    ProcessResult res;
    if (cmd.empty()) return res;

    int out_pipe[2];
    int err_pipe[2];
    if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
        return res;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        return res;
    }

    if (pid == 0) {
        ::close(out_pipe[0]);
        ::close(err_pipe[0]);
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(err_pipe[1], STDERR_FILENO);
        ::close(out_pipe[1]);
        ::close(err_pipe[1]);

        if (!cwd.empty()) {
            if (::chdir(cwd.c_str()) != 0) {
                ::_exit(127);
            }
        }

        std::vector<char*> c_args;
        c_args.reserve(cmd.size() + 1);
        for (const auto& s : cmd) {
            c_args.push_back(const_cast<char*>(s.c_str()));
        }
        c_args.push_back(nullptr);

        ::execvp(c_args[0], c_args.data());
        ::_exit(127);
    }

    ::close(out_pipe[1]);
    ::close(err_pipe[1]);

    int out_flags = ::fcntl(out_pipe[0], F_GETFL, 0);
    ::fcntl(out_pipe[0], F_SETFL, out_flags | O_NONBLOCK);
    int err_flags = ::fcntl(err_pipe[0], F_GETFL, 0);
    ::fcntl(err_pipe[0], F_SETFL, err_flags | O_NONBLOCK);

    auto start_time = std::chrono::steady_clock::now();
    std::array<char, 4096> buffer{};

    while (true) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();
        if (elapsed > timeout_sec) {
            res.timed_out = true;
            ::kill(pid, SIGKILL);
            break;
        }

        ssize_t n_out = ::read(out_pipe[0], buffer.data(), buffer.size());
        if (n_out > 0) {
            res.stdout_output.append(buffer.data(), static_cast<std::size_t>(n_out));
        }

        ssize_t n_err = ::read(err_pipe[0], buffer.data(), buffer.size());
        if (n_err > 0) {
            res.stderr_output.append(buffer.data(), static_cast<std::size_t>(n_err));
        }

        int status = 0;
        pid_t waited = ::waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            while ((n_out = ::read(out_pipe[0], buffer.data(), buffer.size())) > 0) {
                res.stdout_output.append(buffer.data(), static_cast<std::size_t>(n_out));
            }
            while ((n_err = ::read(err_pipe[0], buffer.data(), buffer.size())) > 0) {
                res.stderr_output.append(buffer.data(), static_cast<std::size_t>(n_err));
            }
            res.returncode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    ::close(out_pipe[0]);
    ::close(err_pipe[0]);
    if (res.timed_out) {
        int status = 0;
        ::waitpid(pid, &status, 0);
    }
    return res;
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

auto resolve_tool(const std::string& tool, const std::vector<std::string>& fallbacks)
    -> std::string {
    std::string base = fs::path(tool).filename().string();
    if (base.find("riscv") != std::string::npos || base.find("cross") != std::string::npos) {
        return tool;
    }
    for (const auto& cand : fallbacks) {
        if (cand.front() == '/' && fs::exists(cand)) {
            return cand;
        }
        std::string found = which(cand);
        if (!found.empty()) {
            return found;
        }
    }
    return tool;
}

auto get_tohost_addr(const std::string& nm_bin, const std::string& elf_path) -> std::string {
    const std::string default_tohost = "0x80006000";
    if (!fs::exists(nm_bin)) {
        return default_tohost;
    }

    ProcessResult res = run_cmd({nm_bin, "-g", elf_path}, 10.0);
    if (res.returncode != 0) {
        return default_tohost;
    }

    static const std::regex tohost_re(R"re(([0-9a-fA-F]+)\s+[DdTtGgBb]\s+tohost)re");
    std::smatch m;
    if (std::regex_search(res.stdout_output, m, tohost_re)) {
        std::string hex_str = m[1].str();
        auto first_non_zero = hex_str.find_first_not_of('0');
        if (first_non_zero != std::string::npos) {
            return "0x" + hex_str.substr(first_non_zero);
        }
        return "0x0";
    }

    return default_tohost;
}

struct TestContext {
    std::string s_file;
    std::string test_name;
    std::string gcc;
    std::string objcopy;
    std::string nm;
    std::string simrv;
    std::string work_dir;
    int xlen = 64;
    int vlen = 256;
    std::string vector_tests_dir;
};

struct SingleTestResult {
    std::string name;
    std::string status;  // PASS, FAIL, COMPILE_FAIL, OBJCOPY_FAIL, TIMEOUT, EXEC_FAIL
    std::string error;
};

auto compile_and_run_test(const TestContext& ctx) -> SingleTestResult {
    SingleTestResult result;
    result.name = ctx.test_name;

    std::string elf_file = (fs::path(ctx.work_dir) / (ctx.test_name + ".elf")).string();
    std::string bin_file = (fs::path(ctx.work_dir) / (ctx.test_name + ".bin")).string();

    std::string vec_arch = (ctx.xlen == 64) ? "rv64gcv" : "rv32gcv";
    std::string vec_abi = (ctx.xlen == 64) ? "lp64d" : "ilp32d";

    fs::path vdir(ctx.vector_tests_dir);
    std::string env_dir = (vdir / "env").string();
    std::string test_env_dir = (vdir / "env" / "riscv-test-env").string();
    std::string p_dir = (vdir / "env" / "riscv-test-env" / "p").string();
    std::string macros_dir = (vdir / "macros" / "general").string();
    std::string link_script = (vdir / "env" / "riscv-test-env" / "p" / "link.ld").string();

    // 1. Compile
    std::vector<std::string> compile_cmd = {
        ctx.gcc,
        "-march=" + vec_arch,
        "-mabi=" + vec_abi,
        "-static",
        "-mcmodel=medany",
        "-fvisibility=hidden",
        "-nostdlib",
        "-nostartfiles",
        "-I",
        test_env_dir,
        "-I",
        p_dir,
        "-I",
        env_dir,
        "-I",
        macros_dir,
        "-T",
        link_script,
        ctx.s_file,
        "-o",
        elf_file,
    };

    ProcessResult res = run_cmd(compile_cmd, 30.0);
    if (res.returncode != 0) {
        result.status = "COMPILE_FAIL";
        result.error = res.stderr_output.empty() ? res.stdout_output : res.stderr_output;
        return result;
    }

    // 2. Objcopy
    ProcessResult obj_res = run_cmd({ctx.objcopy, "-O", "binary", elf_file, bin_file}, 30.0);
    if (obj_res.returncode != 0) {
        result.status = "OBJCOPY_FAIL";
        result.error = obj_res.stderr_output;
        return result;
    }

    // 3. Find tohost
    std::string tohost_addr = get_tohost_addr(ctx.nm, elf_file);

    // 4. Run SimRV
    std::vector<std::string> sim_cmd = {
        ctx.simrv, "--cli", "--vlen",    std::to_string(ctx.vlen), "-m", bin_file, "-e", "2000000",
        "-b",      "-H",    tohost_addr,
    };

    ProcessResult sim_res = run_cmd(sim_cmd, 10.0);
    if (sim_res.timed_out) {
        result.status = "TIMEOUT";
        result.error = "Execution timed out (10s)";
        return result;
    }

    if (sim_res.stdout_output.find("ISA TEST PASS") != std::string::npos) {
        result.status = "PASS";
        return result;
    }
    if (sim_res.stdout_output.find("ISA TEST FAIL") != std::string::npos) {
        result.status = "FAIL";
        result.error = sim_res.stdout_output + sim_res.stderr_output;
        return result;
    }

    result.status = "EXEC_FAIL";
    result.error = std::format("Exit code: {}\nStdout: {}\nStderr: {}", sim_res.returncode,
                               sim_res.stdout_output, sim_res.stderr_output);
    return result;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string simrv_bin;
    int xlen = 0;
    std::string gcc_bin;
    std::string objcopy_bin;
    std::string nm_bin;
    std::string work_dir;
    std::string vector_tests_dir;
    int vlen = 256;
    unsigned int jobs = std::max(1u, std::thread::hardware_concurrency());
    std::string pattern = ".*";
    std::string generator_path;
    std::string configs_path;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "--simrv" && i + 1 < argc) {
            simrv_bin = argv[++i];
        } else if (arg == "--xlen" && i + 1 < argc) {
            xlen = std::stoi(argv[++i]);
        } else if (arg == "--gcc" && i + 1 < argc) {
            gcc_bin = argv[++i];
        } else if (arg == "--objcopy" && i + 1 < argc) {
            objcopy_bin = argv[++i];
        } else if (arg == "--nm" && i + 1 < argc) {
            nm_bin = argv[++i];
        } else if (arg == "--work-dir" && i + 1 < argc) {
            work_dir = argv[++i];
        } else if (arg == "--vector-tests-dir" && i + 1 < argc) {
            vector_tests_dir = argv[++i];
        } else if (arg == "--vlen" && i + 1 < argc) {
            vlen = std::stoi(argv[++i]);
        } else if ((arg == "-j" || arg == "--jobs") && i + 1 < argc) {
            jobs = static_cast<unsigned int>(std::max(1, std::stoi(argv[++i])));
        } else if (arg == "--pattern" && i + 1 < argc) {
            pattern = argv[++i];
        } else if (arg == "--generator" && i + 1 < argc) {
            generator_path = argv[++i];
        } else if (arg == "--configs-dir" && i + 1 < argc) {
            configs_path = argv[++i];
        }
    }

    if (simrv_bin.empty() || xlen == 0 || gcc_bin.empty() || objcopy_bin.empty() ||
        nm_bin.empty() || work_dir.empty() || vector_tests_dir.empty()) {
        std::println(stderr, "Error: Missing required arguments.");
        print_help(argv[0]);
        return 1;
    }

    work_dir = fs::absolute(work_dir).string();
    fs::create_directories(work_dir);

    // Create unique run subdirectory
    auto now_epoch = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path run_dir = fs::path(work_dir) / std::format("vector-run-{}", now_epoch);
    fs::create_directories(run_dir);

    vector_tests_dir = fs::absolute(vector_tests_dir).string();

    if (generator_path.empty()) {
        fs::path p1 = fs::path(vector_tests_dir) / "bin" / "riscv-vector-tests-generator";
        fs::path p2 = fs::path(work_dir) / "riscv-vector-tests-generator";
        if (fs::exists(p1)) {
            generator_path = p1.string();
        } else if (fs::exists(p2)) {
            generator_path = p2.string();
        }
    }

    if (configs_path.empty()) {
        configs_path = (fs::path(vector_tests_dir) / "configs" / "v").string();
    } else {
        configs_path = fs::absolute(configs_path).string();
    }

    if (!fs::exists(generator_path)) {
        fs::path gen_src_dir = fs::path(vector_tests_dir) / "generator";
        if (fs::exists(gen_src_dir)) {
            ProcessResult bld =
                run_cmd({"go", "build", "-o", generator_path, "."}, 60.0, vector_tests_dir);
            if (bld.returncode != 0) {
                std::println(stderr, "Failed to build vector test generator: {}",
                             bld.stderr_output);
            }
        }
    }

    if (!fs::exists(generator_path)) {
        std::println(stderr, "Error: Vector test generator not found at '{}'", generator_path);
        return 1;
    }

    // 1. Run Generator
    std::println("Generating vector assembly files...");
    std::vector<std::string> gen_cmd = {
        generator_path,
        "-XLEN",
        std::to_string(xlen),
        "-VLEN",
        std::to_string(vlen),
        "-configs",
        configs_path,
        "-stage1output",
        run_dir.string(),
        "-march",
        "gcv",
        "-pattern",
        pattern,
    };

    ProcessResult gen_res = run_cmd(gen_cmd, 120.0, run_dir.string());
    if (gen_res.returncode != 0) {
        std::println(stderr, "Failed to generate vector tests: {}", gen_res.stderr_output);
        return 1;
    }

    std::vector<std::string> s_files;
    for (const auto& entry : fs::directory_iterator(run_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".S") {
            s_files.push_back(entry.path().string());
        }
    }

    if (s_files.empty()) {
        std::println(stderr, "No generated assembly (.S) files found!");
        return 1;
    }

    std::println("Found {} generated vector tests. Compiling and running with {} jobs...",
                 s_files.size(), jobs);

    // Toolchain fallback detection
    std::vector<std::string> gcc_cands = {
        "riscv64-unknown-elf-gcc",
        "riscv32-unknown-elf-gcc",
        "riscv64-linux-gnu-gcc",
        "riscv32-linux-gnu-gcc",
        "riscv-none-elf-gcc",
        "/var/archlab-modules/riscv-gnu-toolchain/2026.03.13/bin/riscv64-unknown-elf-gcc",
    };
    std::vector<std::string> objcopy_cands = {
        "riscv64-unknown-elf-objcopy",
        "riscv32-unknown-elf-objcopy",
        "riscv64-linux-gnu-objcopy",
        "riscv32-linux-gnu-objcopy",
        "riscv-none-elf-objcopy",
        "/var/archlab-modules/riscv-gnu-toolchain/2026.03.13/bin/riscv64-unknown-elf-objcopy",
    };
    std::vector<std::string> nm_cands = {
        "riscv64-unknown-elf-nm",
        "riscv32-unknown-elf-nm",
        "riscv64-linux-gnu-nm",
        "riscv32-linux-gnu-nm",
        "riscv-none-elf-nm",
        "/var/archlab-modules/riscv-gnu-toolchain/2026.03.13/bin/riscv64-unknown-elf-nm",
    };

    gcc_bin = resolve_tool(gcc_bin, gcc_cands);
    objcopy_bin = resolve_tool(objcopy_bin, objcopy_cands);
    nm_bin = resolve_tool(nm_bin, nm_cands);

    std::vector<TestContext> test_contexts;
    test_contexts.reserve(s_files.size());
    for (const auto& sf : s_files) {
        TestContext ctx;
        ctx.s_file = sf;
        ctx.test_name = fs::path(sf).stem().string();
        ctx.gcc = gcc_bin;
        ctx.objcopy = objcopy_bin;
        ctx.nm = nm_bin;
        ctx.simrv = simrv_bin;
        ctx.work_dir = run_dir.string();
        ctx.xlen = xlen;
        ctx.vlen = vlen;
        ctx.vector_tests_dir = vector_tests_dir;
        test_contexts.push_back(std::move(ctx));
    }

    std::size_t total = test_contexts.size();
    std::atomic<std::size_t> completed_count{0};
    std::atomic<std::size_t> passed_count{0};
    std::vector<SingleTestResult> failed_results;
    std::mutex results_mutex;

    std::atomic<std::size_t> next_idx{0};
    auto worker = [&]() {
        while (true) {
            std::size_t idx = next_idx.fetch_add(1);
            if (idx >= test_contexts.size()) break;

            SingleTestResult res = compile_and_run_test(test_contexts[idx]);
            if (res.status == "PASS") {
                passed_count.fetch_add(1);
            } else {
                std::lock_guard<std::mutex> lock(results_mutex);
                failed_results.push_back(std::move(res));
            }

            std::size_t done = completed_count.fetch_add(1) + 1;
            if (done % 100 == 0 || done == total) {
                std::println("Progress: {}/{} tests completed ({} passed, {} failed)...", done,
                             total, passed_count.load(), failed_results.size());
            }
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(jobs);
    for (unsigned int j = 0; j < jobs; ++j) {
        pool.emplace_back(worker);
    }
    for (auto& th : pool) {
        th.join();
    }

    std::println("\n--- Vector Test Summary ---");
    std::println("Total: {}", total);
    std::println("Passed: {}", passed_count.load());
    std::println("Failed: {}", failed_results.size());

    if (!failed_results.empty()) {
        std::println("\nFailed Tests Details:");
        std::size_t show_count = std::min<std::size_t>(10, failed_results.size());
        for (std::size_t k = 0; k < show_count; ++k) {
            const auto& f = failed_results[k];
            std::println("- {} ({}):", f.name, f.status);
            std::println("{}", f.error);
            std::println("{}", std::string(40, '-'));
        }
        if (failed_results.size() > 10) {
            std::println("... and {} more failures.", failed_results.size() - 10);
        }
        return 1;
    }

    std::println("All vector tests passed successfully!");
    return 0;
}
