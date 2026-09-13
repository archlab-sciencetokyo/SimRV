/**
 * @file Logger.hpp
 * @brief Centralized logging utility for SimRV.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace simrv::log {

enum class Level : uint8_t { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4, Off = 5 };

using LogCallback = std::function<void(const std::string&)>;

/// Parse a log level string case-insensitively.
[[nodiscard]] auto parse_level(std::string_view str) noexcept -> std::optional<Level>;

/// Human-readable textual representation of a log severity level.
[[nodiscard]] auto level_name(Level level) noexcept -> std::string_view;

/// Set the minimum severity level for active log emission.
void set_level(Level level) noexcept;

/// Retrieve the active log severity level.
[[nodiscard]] auto get_level() noexcept -> Level;

/// Check if a log level is currently enabled for emission.
[[nodiscard]] inline auto is_level_enabled(Level level) noexcept -> bool {
    return std::to_underlying(level) >= std::to_underlying(get_level());
}

/// Enable or disable TUI mode (suppresses raw stdout leaks during early startup).
void set_tui_mode(bool enable);

/// Register a callback to route logs to the TUI (if active).
void set_tui_callback(LogCallback cb);

/// Mirror all subsequent log records to a timestamped UTF-8 developer log.
[[nodiscard]] auto set_log_file(std::string_view path) -> bool;
void close_log_file();

/// Internal base print functions
void print_trace(const std::string& msg);
void print_debug(const std::string& msg);
void print_info(const std::string& msg);
void print_warn(const std::string& msg);
void print_error(const std::string& msg);

/// Formatted trace logging
template <typename... Args>
inline void trace(std::format_string<Args...> fmt, Args&&... args) {
    if (is_level_enabled(Level::Trace)) {
        print_trace(std::format(fmt, std::forward<Args>(args)...));
    }
}

/// Formatted debug logging
template <typename... Args>
inline void debug(std::format_string<Args...> fmt, Args&&... args) {
    if (is_level_enabled(Level::Debug)) {
        print_debug(std::format(fmt, std::forward<Args>(args)...));
    }
}

/// Formatted info logging
template <typename... Args>
inline void info(std::format_string<Args...> fmt, Args&&... args) {
    if (is_level_enabled(Level::Info)) {
        print_info(std::format(fmt, std::forward<Args>(args)...));
    }
}

/// Formatted warning logging
template <typename... Args>
inline void warn(std::format_string<Args...> fmt, Args&&... args) {
    if (is_level_enabled(Level::Warn)) {
        print_warn(std::format(fmt, std::forward<Args>(args)...));
    }
}

/// Formatted error logging
template <typename... Args>
inline void error(std::format_string<Args...> fmt, Args&&... args) {
    if (is_level_enabled(Level::Error)) {
        print_error(std::format(fmt, std::forward<Args>(args)...));
    }
}

}  // namespace simrv::log
