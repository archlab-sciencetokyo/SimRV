/**
 * @file E2EHelper.hpp
 * @brief Portable C++23 native subprocess execution and fixture helpers for SimRV E2E tests.
 */
#pragma once

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace simrv::test {

struct ProcessResult {
    int exit_code{-1};
    std::string stdout_output;
    std::string stderr_output;
    bool timed_out{false};

    [[nodiscard]] auto success() const noexcept -> bool { return exit_code == 0 && !timed_out; }
};

inline auto resolve_repo_root() -> std::filesystem::path {
    if (const char* env_root = std::getenv("SIMRV_SOURCE_DIR")) {
        if (std::filesystem::is_directory(env_root)) {
            return std::filesystem::canonical(env_root);
        }
    }
    // Walk upwards from current working directory or executable location
    auto current = std::filesystem::current_path();
    for (int i = 0; i < 5; ++i) {
        if (std::filesystem::is_regular_file(current / "CMakeLists.txt") &&
            std::filesystem::is_directory(current / "include/simrv")) {
            return current;
        }
        if (current.has_parent_path() && current.parent_path() != current) {
            current = current.parent_path();
        } else {
            break;
        }
    }
    return std::filesystem::current_path();
}

inline auto resolve_simrv_binary(int xlen = 64) -> std::filesystem::path {
    const char* env_var = (xlen == 32) ? "SIMRV32_BIN" : "SIMRV_BIN";
    if (const char* path = std::getenv(env_var)) {
        if (std::filesystem::is_regular_file(path)) {
            return std::filesystem::canonical(path);
        }
    }

    const auto root = resolve_repo_root();
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path() / "simrv",
        root / std::format("build/rv{}-release/simrv", xlen),
        root / std::format("build/rv{}-debug/simrv", xlen),
        root / std::format("build/rv{}-dev/simrv", xlen),
        root / std::format("build/rv{}-native-release/simrv", xlen),
    };

    for (const auto& cand : candidates) {
        if (std::filesystem::is_regular_file(cand) && (::access(cand.c_str(), X_OK) == 0)) {
            return std::filesystem::canonical(cand);
        }
    }

    // Default to the current directory binary or release path
    return candidates[0];
}

inline auto find_example_elf(std::string_view name, int xlen = 64)
    -> std::optional<std::filesystem::path> {
    const auto root = resolve_repo_root();
    const std::vector<std::filesystem::path> candidates = {
        root / std::format("examples/isa/build-rv{}/{}.elf", xlen, name),
        root / std::format("examples/hello/build-rv{}/{}.elf", xlen, name),
    };

    for (const auto& cand : candidates) {
        if (std::filesystem::is_regular_file(cand)) {
            return std::filesystem::canonical(cand);
        }
    }
    return std::nullopt;
}

inline auto run_process(const std::filesystem::path& binary, const std::vector<std::string>& args,
                        std::chrono::milliseconds timeout = std::chrono::seconds(30),
                        std::string_view stdin_input = {},
                        const std::vector<std::pair<std::string, std::string>>& extra_env = {})
    -> ProcessResult {
    ProcessResult result{};

    int stdout_pipe[2];
    int stderr_pipe[2];
    int stdin_pipe[2];

    if (::pipe(stdout_pipe) != 0 || ::pipe(stderr_pipe) != 0 || ::pipe(stdin_pipe) != 0) {
        result.stderr_output = "Failed to create pipes";
        return result;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        result.stderr_output = "Failed to fork subprocess";
        ::close(stdout_pipe[0]);
        ::close(stdout_pipe[1]);
        ::close(stderr_pipe[0]);
        ::close(stderr_pipe[1]);
        ::close(stdin_pipe[0]);
        ::close(stdin_pipe[1]);
        return result;
    }

    if (pid == 0) {
        // Child process
        ::close(stdout_pipe[0]);
        ::close(stderr_pipe[0]);
        ::close(stdin_pipe[1]);

        ::dup2(stdin_pipe[0], STDIN_FILENO);
        ::dup2(stdout_pipe[1], STDOUT_FILENO);
        ::dup2(stderr_pipe[1], STDERR_FILENO);

        ::close(stdin_pipe[0]);
        ::close(stdout_pipe[1]);
        ::close(stderr_pipe[1]);

        // Disable colors and enable non-interactive testing mode
        ::setenv("NO_COLOR", "1", 1);
        for (const auto& [key, val] : extra_env) {
            ::setenv(key.c_str(), val.c_str(), 1);
        }

        std::vector<char*> argv;
        std::string bin_str = binary.string();
        argv.push_back(bin_str.data());
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        ::execv(binary.c_str(), argv.data());
        ::exit(127);
    }

    // Parent process
    ::close(stdin_pipe[0]);
    ::close(stdout_pipe[1]);
    ::close(stderr_pipe[1]);

    if (!stdin_input.empty()) {
        [[maybe_unused]] auto written =
            ::write(stdin_pipe[1], stdin_input.data(), stdin_input.size());
    }
    ::close(stdin_pipe[1]);

    // Set read pipes to non-blocking
    ::fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    ::fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    std::vector<pollfd> fds(2);
    fds[0].fd = stdout_pipe[0];
    fds[0].events = POLLIN;
    fds[1].fd = stderr_pipe[0];
    fds[1].events = POLLIN;

    auto start_time = std::chrono::steady_clock::now();
    bool stdout_open = true;
    bool stderr_open = true;
    char buffer[4096];

    while (stdout_open || stderr_open) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        if (elapsed > timeout) {
            result.timed_out = true;
            ::kill(pid, SIGKILL);
            break;
        }

        int poll_timeout = std::min(100, static_cast<int>((timeout - elapsed).count()));
        int poll_res = ::poll(fds.data(), fds.size(), poll_timeout);

        if (poll_res > 0) {
            if (fds[0].revents & (POLLIN | POLLHUP)) {
                ssize_t bytes = ::read(stdout_pipe[0], buffer, sizeof(buffer));
                if (bytes > 0) {
                    result.stdout_output.append(buffer, bytes);
                } else if (bytes == 0 || (fds[0].revents & POLLHUP)) {
                    stdout_open = false;
                    fds[0].fd = -1;
                }
            }
            if (fds[1].revents & (POLLIN | POLLHUP)) {
                ssize_t bytes = ::read(stderr_pipe[0], buffer, sizeof(buffer));
                if (bytes > 0) {
                    result.stderr_output.append(buffer, bytes);
                } else if (bytes == 0 || (fds[1].revents & POLLHUP)) {
                    stderr_open = false;
                    fds[1].fd = -1;
                }
            }
        }
    }

    ::close(stdout_pipe[0]);
    ::close(stderr_pipe[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }

    return result;
}

inline auto run_simrv(const std::vector<std::string>& args, int xlen = 64,
                      std::chrono::milliseconds timeout = std::chrono::seconds(30),
                      std::string_view stdin_input = {}) -> ProcessResult {
    const auto bin = resolve_simrv_binary(xlen);
    return run_process(bin, args, timeout, stdin_input);
}

}  // namespace simrv::test
