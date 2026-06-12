/**
 * @file Logger.cpp
 * @brief Implementation of centralized logger.
 */

#include "simrv/core/Logger.hpp"

#include <iostream>
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
    std::string formatted = std::format("[INFO] {}", msg);
    if (g_tui_callback) {
        g_tui_callback("\033[36m" + formatted + "\033[0m\n"); // Mapped to Sakura Sky Blue
    } else {
        if (simrv::util::is_terminal(STDOUT_FILENO)) {
            std::println(stdout, "\033[38;5;117m{}\033[0m", formatted); // Sakura Sky Blue
        } else {
            std::println(stdout, "{}", formatted);
        }
    }
}

void print_warn(const std::string& msg) {
    std::string formatted = std::format("[WARN] {}", msg);
    if (g_tui_callback) {
        g_tui_callback("\033[93m" + formatted + "\033[0m\n"); // Mapped to Sakura Peach
    } else {
        if (simrv::util::is_terminal(STDERR_FILENO)) {
            std::println(stderr, "\033[38;5;223m{}\033[0m", formatted); // Sakura Peach
        } else {
            std::println(stderr, "{}", formatted);
        }
    }
}

void print_error(const std::string& msg) {
    std::string formatted = std::format("[ERROR] {}", msg);
    if (g_tui_callback) {
        g_tui_callback("\033[91m" + formatted + "\033[0m\n"); // Mapped to Sakura Pink/Coral
    } else {
        if (simrv::util::is_terminal(STDERR_FILENO)) {
            std::println(stderr, "\033[1;38;5;210m{}\033[0m", formatted); // Bold Sakura Coral
        } else {
            std::println(stderr, "{}", formatted);
        }
    }
}

}  // namespace simrv::log
