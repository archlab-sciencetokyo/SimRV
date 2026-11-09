/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Archlab, Science Tokyo
 */

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

void print_help(const char* prog) {
    std::println("SimRV RVA23 Architecture Certification Test (ACT4) Runner");
    std::println("Usage: {} [options] [--elf-dir <DIR>]", prog);
    std::println("\nOptions:");
    std::println("  --elf-dir <PATH>      Directory containing ACT4 generated ELFs (required)");
    std::println("  --simrv <PATH>        Explicit path to SimRV executable");
    std::println("  --report <PATH>       Path to emit JSON evaluation report");
    std::println(
        "  --jobs, -j <N>        Number of parallel test jobs (default: hardware threads)");
    std::println("  --vlen <N>            Vector register length in bits (default: 128)");
    std::println("  --timeout <SEC>       Timeout per test in seconds (default: 30)");
    std::println("  --max-failures <N>    Maximum failure details printed (default: 30)");
    std::println("  --filter <PATTERN>    Only run ELFs matching substring pattern");
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

struct TestResult {
    std::string name;
    std::string path;
    bool passed = false;
    bool timed_out = false;
    int returncode = 0;
    double duration_seconds = 0.0;
    std::string detail;
};

auto run_single_test(const std::filesystem::path& simrv_bin, const std::filesystem::path& elf_path,
                     unsigned int vlen, double timeout_sec) -> TestResult {
    TestResult res;
    res.name = elf_path.filename().string();
    res.path = elf_path.string();

    auto start_time = std::chrono::steady_clock::now();

    int pipe_fd[2];
    if (::pipe(pipe_fd) != 0) {
        res.detail = "Failed to create pipe";
        return res;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
        res.detail = "Failed to fork process";
        return res;
    }

    if (pid == 0) {
        ::close(pipe_fd[0]);
        ::dup2(pipe_fd[1], STDOUT_FILENO);
        ::dup2(pipe_fd[1], STDERR_FILENO);
        ::close(pipe_fd[1]);

        std::string vlen_str = std::to_string(vlen);
        std::string elf_str = elf_path.string();
        std::string simrv_str = simrv_bin.string();

        std::vector<char*> c_args = {
            const_cast<char*>(simrv_str.c_str()),  const_cast<char*>("--cli"),
            const_cast<char*>("--baremetal"),      const_cast<char*>("--no-sbi"),
            const_cast<char*>("--act-test-hooks"), const_cast<char*>("--vlen"),
            const_cast<char*>(vlen_str.c_str()),   const_cast<char*>("-m"),
            const_cast<char*>(elf_str.c_str()),    nullptr,
        };

        ::execvp(c_args[0], c_args.data());
        ::_exit(127);
    }

    ::close(pipe_fd[1]);

    // Read output with non-blocking polling for timeout support
    int flags = ::fcntl(pipe_fd[0], F_GETFL, 0);
    ::fcntl(pipe_fd[0], F_SETFL, flags | O_NONBLOCK);

    std::string output;
    std::array<char, 4096> buffer{};
    bool timed_out = false;

    while (true) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time).count();
        if (elapsed > timeout_sec) {
            timed_out = true;
            ::kill(pid, SIGKILL);
            break;
        }

        ssize_t bytes = ::read(pipe_fd[0], buffer.data(), buffer.size());
        if (bytes > 0) {
            output.append(buffer.data(), static_cast<std::size_t>(bytes));
        } else if (bytes == 0) {
            break;  // EOF
        } else if (errno != EAGAIN) {
            break;
        }

        int status = 0;
        pid_t waited = ::waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            // Read remaining bytes
            while ((bytes = ::read(pipe_fd[0], buffer.data(), buffer.size())) > 0) {
                output.append(buffer.data(), static_cast<std::size_t>(bytes));
            }
            res.returncode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ::close(pipe_fd[0]);

    if (timed_out) {
        int status = 0;
        ::waitpid(pid, &status, 0);
        res.timed_out = true;
        res.detail = std::format("Exceeded timeout of {:.1f}s", timeout_sec);
        return res;
    }

    auto end_time = std::chrono::steady_clock::now();
    res.duration_seconds = std::chrono::duration<double>(end_time - start_time).count();

    const bool has_pass =
        (output.find("ISA TEST PASS") != std::string::npos ||
         output.find("Termination reason       :         guest poweroff") != std::string::npos);
    const bool has_fail = (output.find("ISA TEST FAIL") != std::string::npos ||
                           output.find("RVCP-SUMMARY: TEST FAILED") != std::string::npos ||
                           output.find("[FATAL]") != std::string::npos);

    res.passed = (res.returncode == 0 && has_pass && !has_fail);
    if (!res.passed) {
        if (output.size() > 4000) {
            res.detail = output.substr(output.size() - 4000);
        } else {
            res.detail = output;
        }
    }
    return res;
}

}  // namespace

int main(int argc, char** argv) {
    std::string elf_dir;
    std::string simrv_path;
    std::string report_path;
    std::string filter_pattern;
    unsigned int jobs = std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
    unsigned int vlen = 128;
    double timeout_sec = 30.0;
    std::size_t max_failures = 30;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "--elf-dir" && i + 1 < argc) {
            elf_dir = argv[++i];
        } else if (arg == "--simrv" && i + 1 < argc) {
            simrv_path = argv[++i];
        } else if (arg == "--report" && i + 1 < argc) {
            report_path = argv[++i];
        } else if ((arg == "--jobs" || arg == "-j") && i + 1 < argc) {
            jobs = std::max(1u, static_cast<unsigned int>(std::stoul(argv[++i])));
        } else if (arg == "--vlen" && i + 1 < argc) {
            vlen = static_cast<unsigned int>(std::stoul(argv[++i]));
        } else if (arg == "--timeout" && i + 1 < argc) {
            timeout_sec = std::stod(argv[++i]);
        } else if (arg == "--max-failures" && i + 1 < argc) {
            max_failures = std::stoul(argv[++i]);
        } else if (arg == "--filter" && i + 1 < argc) {
            filter_pattern = argv[++i];
        } else if (!arg.starts_with("-") && elf_dir.empty()) {
            elf_dir = arg;
        } else {
            std::println(std::cerr, "Unknown argument: {}", arg);
            print_help(argv[0]);
            return 2;
        }
    }

    if (elf_dir.empty()) {
        std::println(std::cerr, "Error: No ACT ELF directory specified (use --elf-dir <DIR>).");
        print_help(argv[0]);
        return 2;
    }

    std::filesystem::path dir_path(elf_dir);
    if (!std::filesystem::exists(dir_path)) {
        std::println(std::cerr, "Error: ELF directory '{}' does not exist.", elf_dir);
        return 1;
    }

    std::vector<std::filesystem::path> elfs;
    if (std::filesystem::is_directory(dir_path)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir_path)) {
            if (entry.is_regular_file()) {
                auto path = entry.path();
                std::string fname = path.filename().string();
                if ((path.extension() == ".elf" || fname.ends_with(".elf")) &&
                    (filter_pattern.empty() || fname.find(filter_pattern) != std::string::npos)) {
                    elfs.push_back(path);
                }
            }
        }
    } else {
        elfs.push_back(dir_path);
    }

    if (elfs.empty()) {
        std::println(std::cerr, "Error: No matching ELF test binaries found in '{}'.", elf_dir);
        return 1;
    }

    auto resolved_simrv = find_simrv_binary(simrv_path);

    std::println("\033[1;34m=== SimRV RVA23 ACT4 Test Suite ===\033[0m");
    std::println("  SimRV Executable : {}", resolved_simrv.string());
    std::println("  ELF Directory    : {}", dir_path.string());
    std::println("  Total Tests      : {}", elfs.size());
    std::println("  Parallel Workers : {}", jobs);
    std::println("  VLEN             : {} bits", vlen);
    std::println("  Per-test Timeout : {:.1f}s", timeout_sec);

    std::vector<TestResult> results(elfs.size());
    std::atomic<std::size_t> next_idx{0};
    std::atomic<std::size_t> pass_count{0};
    std::atomic<std::size_t> fail_count{0};
    std::atomic<std::size_t> timeout_count{0};

    auto worker = [&]() {
        while (true) {
            std::size_t idx = next_idx.fetch_add(1);
            if (idx >= elfs.size()) break;

            const auto& elf = elfs[idx];
            auto res = run_single_test(resolved_simrv, elf, vlen, timeout_sec);
            if (res.passed) {
                pass_count.fetch_add(1);
            } else if (res.timed_out) {
                timeout_count.fetch_add(1);
            } else {
                fail_count.fetch_add(1);
            }
            results[idx] = std::move(res);
        }
    };

    std::vector<std::jthread> threads;
    threads.reserve(jobs);
    for (unsigned int i = 0; i < jobs; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    std::println("\n\033[1m=== ACT4 Test Results ===\033[0m");
    std::println("  Passed   : \033[1;32m{}\033[0m / {}", pass_count.load(), elfs.size());
    std::println("  Failed   : \033[1;31m{}\033[0m", fail_count.load());
    std::println("  Timeouts : \033[1;33m{}\033[0m", timeout_count.load());

    std::size_t printed_failures = 0;
    if (fail_count.load() > 0 || timeout_count.load() > 0) {
        std::println("\n\033[1;31mFailed Tests:\033[0m");
        for (const auto& res : results) {
            if (!res.passed) {
                if (printed_failures++ < max_failures) {
                    std::println("  - {:40} [{}] (exit {})", res.name,
                                 res.timed_out ? "TIMEOUT" : "FAIL", res.returncode);
                }
            }
        }
        if (printed_failures > max_failures) {
            std::println("  ... ({} additional failure details omitted)",
                         printed_failures - max_failures);
        }
    }

    if (!report_path.empty()) {
        std::ofstream report(report_path);
        if (report.is_open()) {
            report << "{\n";
            report << std::format("  \"total\": {},\n", elfs.size());
            report << std::format("  \"passed\": {},\n", pass_count.load());
            report << std::format("  \"failed\": {},\n", fail_count.load());
            report << std::format("  \"timeouts\": {},\n", timeout_count.load());
            report << "  \"tests\": [\n";
            for (std::size_t i = 0; i < results.size(); ++i) {
                const auto& r = results[i];
                report << "    {\n";
                report << std::format("      \"name\": \"{}\",\n", r.name);
                report << std::format("      \"status\": \"{}\",\n",
                                      r.passed ? "PASS" : (r.timed_out ? "TIMEOUT" : "FAIL"));
                report << std::format("      \"duration\": {:.3f}\n", r.duration_seconds);
                report << std::format("    }}{}\n", (i + 1 < results.size()) ? "," : "");
            }
            report << "  ]\n";
            report << "}\n";
            std::println("  Report written to: {}", report_path);
        }
    }

    return (fail_count.load() == 0 && timeout_count.load() == 0) ? 0 : 1;
}
