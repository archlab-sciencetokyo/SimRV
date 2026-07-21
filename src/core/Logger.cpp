/**
 * @file Logger.cpp
 * @brief Implementation of centralized logger.
 */

#include "simrv/core/Logger.hpp"

#include <print>
#include <unistd.h>
#include "simrv/util/FormatUtil.hpp"

namespace simrv::log {

namespace {
simrv::log::LogCallback g_tui_callback = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
}

void set_tui_callback(LogCallback cb) {
    g_tui_callback = std::move(cb);
}

void print_info(const std::string& msg) {
    if (g_tui_callback) {
        g_tui_callback("\033[36m" + msg + "\033[0m\n"); // Mapped to Sakura Sky Blue
    } else {
        if (simrv::util::is_terminal(STDOUT_FILENO)) {
            std::println(stdout, "\033[38;5;117m{}\033[0m", msg); // Sakura Sky Blue
        } else {
            std::println(stdout, "[INFO] {}", msg);
        }
    }
}

void print_warn(const std::string& msg) {
    if (g_tui_callback) {
        g_tui_callback("\033[93m" + msg + "\033[0m\n"); // Mapped to Sakura Peach
    } else {
        if (simrv::util::is_terminal(STDERR_FILENO)) {
            std::println(stderr, "\033[38;5;223m{}\033[0m", msg); // Sakura Peach
        } else {
            std::println(stderr, "[WARN] {}", msg);
        }
    }
}

void print_error(const std::string& msg) {
    if (g_tui_callback) {
        g_tui_callback("\033[91m" + msg + "\033[0m\n"); // Mapped to Sakura Pink/Coral
    } else {
        if (simrv::util::is_terminal(STDERR_FILENO)) {
            std::println(stderr, "\033[1;38;5;210m{}\033[0m", msg); // Bold Sakura Coral
        } else {
            std::println(stderr, "[ERROR] {}", msg);
        }
    }
}

}  // namespace simrv::log
