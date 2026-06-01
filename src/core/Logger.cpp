/**
 * @file Logger.cpp
 * @brief Implementation of centralized logger.
 */

#include "simrv/core/Logger.hpp"

#include <iostream>
#include <print>

namespace simrv::log {

namespace {
simrv::log::LogCallback g_tui_callback = nullptr;
}

void set_tui_callback(LogCallback cb) {
    g_tui_callback = std::move(cb);
}

void print_info(const std::string& msg) {
    std::string formatted = std::format("[INFO] {}", msg);
    if (g_tui_callback) {
        g_tui_callback(formatted + "\n");
    } else {
        std::println(stdout, "{}", formatted);
    }
}

void print_warn(const std::string& msg) {
    std::string formatted = std::format("[WARN] {}", msg);
    if (g_tui_callback) {
        g_tui_callback("\033[93m" + formatted + "\033[0m\n");
    } else {
        std::println(stderr, "{}", formatted);
    }
}

void print_error(const std::string& msg) {
    std::string formatted = std::format("[ERROR] {}", msg);
    if (g_tui_callback) {
        g_tui_callback("\033[91m" + formatted + "\033[0m\n");
    } else {
        std::println(stderr, "{}", formatted);
    }
}

}  // namespace simrv::log
