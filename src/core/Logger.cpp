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
        g_tui_callback(formatted + "\n");
    } else {
        if (simrv::util::is_terminal(STDOUT_FILENO)) {
            std::println(stdout, "\033[36m{}\033[0m", formatted); // Cyan for Info
        } else {
            std::println(stdout, "{}", formatted);
        }
    }
}

void print_warn(const std::string& msg) {
    std::string formatted = std::format("[WARN] {}", msg);
    if (g_tui_callback) {
        g_tui_callback("\033[93m" + formatted + "\033[0m\n");
    } else {
        if (simrv::util::is_terminal(STDERR_FILENO)) {
            std::println(stderr, "\033[93m{}\033[0m", formatted); // Yellow/gold for Warn
        } else {
            std::println(stderr, "{}", formatted);
        }
    }
}

void print_error(const std::string& msg) {
    std::string formatted = std::format("[ERROR] {}", msg);
    if (g_tui_callback) {
        g_tui_callback("\033[91m" + formatted + "\033[0m\n");
    } else {
        if (simrv::util::is_terminal(STDERR_FILENO)) {
            std::println(stderr, "\033[1;91m{}\033[0m", formatted); // Bold Red for Error
        } else {
            std::println(stderr, "{}", formatted);
        }
    }
}

}  // namespace simrv::log
