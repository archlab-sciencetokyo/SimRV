/**
 * @file Logger.cpp
 * @brief Implementation of centralized logger.
 */

#include "simrv/core/Logger.hpp"

#include <unistd.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <print>
#include <string_view>

#include "simrv/util/FormatUtil.hpp"

namespace simrv::log {

namespace {

struct PendingLog {
    Level level;
    std::string message;
};

constexpr std::size_t kStartupLogLimit = 256;
std::atomic<Level> g_log_level{Level::Info};
std::atomic<Format> g_log_format{Format::Text};
bool g_tui_mode = false;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
simrv::log::LogCallback
    g_tui_callback;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::deque<PendingLog>
    g_startup_logs;        // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::mutex g_log_mutex;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::ofstream g_log_file;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::string g_log_path;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const auto g_log_epoch = std::chrono::steady_clock::now();

auto json_escape(std::string_view value) -> std::string {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (ch < 0x20) {
                    escaped += std::format("\\u{:04x}", static_cast<unsigned int>(ch));
                } else {
                    escaped += static_cast<char>(ch);
                }
        }
    }
    return escaped;
}

auto log_timestamp() -> std::string {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::floor<std::chrono::seconds>(now);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds).count();
    return std::format("{:%FT%T}.{:03}Z", seconds, millis);
}

auto json_record(Level level, const std::string& msg, bool pretty) -> std::string {
    const auto timestamp = json_escape(log_timestamp());
    const auto severity = json_escape(level_name(level));
    const auto message = json_escape(msg);
    if (pretty) {
        return std::format(
            "{{\n  \"schema_version\": 1,\n  \"timestamp\": \"{}\",\n"
            "  \"level\": \"{}\",\n  \"message\": \"{}\"\n}}",
            timestamp, severity, message);
    }
    return std::format(
        "{{\"schema_version\":1,\"timestamp\":\"{}\",\"level\":\"{}\","
        "\"message\":\"{}\"}}",
        timestamp, severity, message);
}

auto tui_message(Level level, const std::string& message) -> std::string {
    switch (level) {
        case Level::Trace:
            return "\033[38;5;244m" + message + "\033[0m\n";
        case Level::Debug:
            return "\033[38;5;141m" + message + "\033[0m\n";
        case Level::Info:
            return "\033[36m" + message + "\033[0m\n";
        case Level::Warn:
            return "\033[93m" + message + "\033[0m\n";
        case Level::Error:
            return "\033[91m" + message + "\033[0m\n";
        case Level::Off:
            return "";
    }
    return message;
}

void emit_log(Level level, FILE* stream, std::string_view ansi_color, std::string_view plain_tag,
              const std::string& msg) {
    LogCallback callback;
    const auto format = g_log_format.load(std::memory_order_relaxed);
    {
        std::scoped_lock lock(g_log_mutex);
        if (g_log_file.is_open()) {
            if (format == Format::Text) {
                const auto elapsed =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - g_log_epoch);
                std::println(g_log_file, "[+{:012.6f}s] [{:5}] {}", elapsed.count(),
                             level_name(level), msg);
            } else {
                std::println(g_log_file, "{}",
                             json_record(level, msg, format == Format::JsonPretty));
            }
            g_log_file.flush();
        }
        if (g_tui_callback) {
            callback = g_tui_callback;
        } else if (g_tui_mode) {
            if (level >= Level::Warn) {
                // Before the TUI owns the terminal, make actionable diagnostics visible without
                // printing routine startup/shutdown chatter or buffering an invisible error.
                if (simrv::util::terminal_color_enabled(STDERR_FILENO)) {
                    std::println(stderr, "{}{}\033[0m", ansi_color, msg);
                } else {
                    std::println(stderr, "[{}] {}", plain_tag, msg);
                }
                std::fflush(stderr);
            } else {
                if (g_startup_logs.size() == kStartupLogLimit) g_startup_logs.pop_front();
                g_startup_logs.push_back({level, msg});
            }
            return;
        } else {
            const int fd = (stream == stderr) ? STDERR_FILENO : STDOUT_FILENO;
            if (format != Format::Text) {
                std::println(stream, "{}", json_record(level, msg, format == Format::JsonPretty));
            } else if (simrv::util::terminal_color_enabled(fd)) {
                std::println(stream, "{}{}\033[0m", ansi_color, msg);
            } else {
                std::println(stream, "[{}] {}", plain_tag, msg);
            }
            // CLI consumers such as the VS Code launcher may read logs through a pipe.
            // Keep readiness and guest-console diagnostics observable without waiting for
            // stdout's block buffer to fill.
            std::fflush(stream);
        }
    }
    if (callback) {
        callback(tui_message(level, msg));
    }
}

}  // namespace

auto parse_level(std::string_view str) noexcept -> std::optional<Level> {
    if (str == "trace" || str == "TRACE") return Level::Trace;
    if (str == "debug" || str == "DEBUG") return Level::Debug;
    if (str == "info" || str == "INFO") return Level::Info;
    if (str == "warn" || str == "warning" || str == "WARN" || str == "WARNING") return Level::Warn;
    if (str == "error" || str == "ERROR") return Level::Error;
    if (str == "off" || str == "none" || str == "OFF" || str == "NONE") return Level::Off;
    return std::nullopt;
}

auto parse_format(std::string_view str) noexcept -> std::optional<Format> {
    std::string value(str);
    for (auto& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (value == "text") return Format::Text;
    if (value == "json") return Format::Json;
    if (value == "json-pretty") return Format::JsonPretty;
    return std::nullopt;
}

void set_format(Format format) noexcept { g_log_format.store(format, std::memory_order_relaxed); }

auto get_format() noexcept -> Format { return g_log_format.load(std::memory_order_relaxed); }

auto level_name(Level level) noexcept -> std::string_view {
    switch (level) {
        case Level::Trace:
            return "TRACE";
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return "INFO";
        case Level::Warn:
            return "WARN";
        case Level::Error:
            return "ERROR";
        case Level::Off:
            return "OFF";
    }
    return "UNKNOWN";
}

void set_level(Level level) noexcept { g_log_level.store(level, std::memory_order_relaxed); }

auto get_level() noexcept -> Level { return g_log_level.load(std::memory_order_relaxed); }

void set_tui_mode(bool enable) {
    std::scoped_lock lock(g_log_mutex);
    g_tui_mode = enable;
}

void set_tui_callback(LogCallback cb) {
    std::deque<PendingLog> pending;
    LogCallback callback;
    {
        std::scoped_lock lock(g_log_mutex);
        g_tui_callback = std::move(cb);
        callback = g_tui_callback;
        if (callback) pending.swap(g_startup_logs);
    }
    if (callback) {
        for (const auto& entry : pending) callback(tui_message(entry.level, entry.message));
    }
}

auto set_log_file(std::string_view path) -> bool {
    std::scoped_lock lock(g_log_mutex);
    if (g_log_file.is_open() && g_log_path == path) return true;
    g_log_file.close();
    g_log_file.clear();
    g_log_path = std::string(path);

    std::error_code ec;
    const std::filesystem::path fs_path(g_log_path);
    if (fs_path.has_parent_path()) {
        std::filesystem::create_directories(fs_path.parent_path(), ec);
    }

    g_log_file.open(g_log_path, std::ios::out | std::ios::trunc);
    return g_log_file.is_open();
}

void close_log_file() {
    std::scoped_lock lock(g_log_mutex);
    g_log_file.close();
    g_log_path.clear();
}

void print_trace(const std::string& msg) {
    emit_log(Level::Trace, stdout, "\033[38;5;244m", "TRACE", msg);
}

void print_debug(const std::string& msg) {
    emit_log(Level::Debug, stdout, "\033[38;5;141m", "DEBUG", msg);
}

void print_info(const std::string& msg) {
    emit_log(Level::Info, stdout, "\033[38;5;117m", "INFO", msg);
}

void print_warn(const std::string& msg) {
    emit_log(Level::Warn, stderr, "\033[38;5;223m", "WARN", msg);
}

void print_error(const std::string& msg) {
    emit_log(Level::Error, stderr, "\033[1;38;5;210m", "ERROR", msg);
}

}  // namespace simrv::log
