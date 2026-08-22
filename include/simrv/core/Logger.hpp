/**
 * @file Logger.hpp
 * @brief Centralized logging utility for SimRV.
 */
#pragma once

#include <format>
#include <functional>
#include <string>

namespace simrv::log {

using LogCallback = std::function<void(const std::string&)>;

/// Enable or disable TUI mode (suppresses raw stdout leaks during early startup).
void set_tui_mode(bool enable);

/// Register a callback to route logs to the TUI (if active).
void set_tui_callback(LogCallback cb);

/// Internal base print functions
void print_info(const std::string& msg);
void print_warn(const std::string& msg);
void print_error(const std::string& msg);

/// Formatted info logging
template <typename... Args>
inline void info(std::format_string<Args...> fmt, Args&&... args) {
    print_info(std::format(fmt, std::forward<Args>(args)...));
}

/// Formatted warning logging
template <typename... Args>
inline void warn(std::format_string<Args...> fmt, Args&&... args) {
    print_warn(std::format(fmt, std::forward<Args>(args)...));
}

/// Formatted error logging
template <typename... Args>
inline void error(std::format_string<Args...> fmt, Args&&... args) {
    print_error(std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace simrv::log
