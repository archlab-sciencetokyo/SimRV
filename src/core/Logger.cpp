/**
 * @file Logger.cpp
 * @brief Implementation of centralized logger.
 */

#include "simrv/core/Logger.hpp"

#include <unistd.h>

#include <chrono>
#include <deque>
#include <fstream>
#include <mutex>
#include <print>
#include <string_view>

#include "simrv/util/FormatUtil.hpp"

namespace simrv::log {

namespace {
enum class Level : uint8_t { Info, Warn, Error };
struct PendingLog {
    Level level;
    std::string message;
};

constexpr std::size_t kStartupLogLimit = 256;
bool g_tui_mode = false;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
simrv::log::LogCallback
    g_tui_callback;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::deque<PendingLog>
    g_startup_logs;        // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::mutex g_log_mutex;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::ofstream g_log_file;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::string g_log_path;    // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
const auto g_log_epoch = std::chrono::steady_clock::now();

auto level_name(Level level) -> std::string_view {
    switch (level) {
        case Level::Info:
            return "INFO";
        case Level::Warn:
            return "WARN";
        case Level::Error:
            return "ERROR";
    }
    return "UNKNOWN";
}

void mirror_to_file(Level level, const std::string& message) {
    std::scoped_lock lock(g_log_mutex);
    if (!g_log_file.is_open()) return;
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - g_log_epoch);
    std::println(g_log_file, "[+{:012.6f}s] [{:5}] {}", elapsed.count(), level_name(level),
                 message);
    g_log_file.flush();
}

auto tui_message(Level level, const std::string& message) -> std::string {
    switch (level) {
        case Level::Info:
            return "\033[36m" + message + "\033[0m\n";
        case Level::Warn:
            return "\033[93m" + message + "\033[0m\n";
        case Level::Error:
            return "\033[91m" + message + "\033[0m\n";
    }
    return message;
}

auto callback_or_buffer(Level level, const std::string& message) -> LogCallback {
    std::scoped_lock lock(g_log_mutex);
    if (g_tui_callback) return g_tui_callback;
    if (!g_tui_mode) return {};
    if (g_startup_logs.size() == kStartupLogLimit) g_startup_logs.pop_front();
    g_startup_logs.push_back({level, message});
    return {};
}
}  // namespace

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
    g_log_file.open(g_log_path, std::ios::out | std::ios::trunc);
    return g_log_file.is_open();
}

void close_log_file() {
    std::scoped_lock lock(g_log_mutex);
    g_log_file.close();
    g_log_path.clear();
}

void print_info(const std::string& msg) {
    mirror_to_file(Level::Info, msg);
    if (auto callback = callback_or_buffer(Level::Info, msg)) {
        callback(tui_message(Level::Info, msg));
    } else {
        if (g_tui_mode) return;
        if (simrv::util::is_terminal(STDOUT_FILENO)) {
            std::println(stdout, "\033[38;5;117m{}\033[0m", msg);  // Sakura Sky Blue
        } else {
            std::println(stdout, "[INFO] {}", msg);
        }
    }
}

void print_warn(const std::string& msg) {
    mirror_to_file(Level::Warn, msg);
    if (auto callback = callback_or_buffer(Level::Warn, msg)) {
        callback(tui_message(Level::Warn, msg));
    } else {
        if (g_tui_mode) return;
        if (simrv::util::is_terminal(STDERR_FILENO)) {
            std::println(stderr, "\033[38;5;223m{}\033[0m", msg);  // Sakura Peach
        } else {
            std::println(stderr, "[WARN] {}", msg);
        }
    }
}

void print_error(const std::string& msg) {
    mirror_to_file(Level::Error, msg);
    if (auto callback = callback_or_buffer(Level::Error, msg)) {
        callback(tui_message(Level::Error, msg));
    } else {
        if (g_tui_mode) return;
        if (simrv::util::is_terminal(STDERR_FILENO)) {
            std::println(stderr, "\033[1;38;5;210m{}\033[0m", msg);  // Bold Sakura Coral
        } else {
            std::println(stderr, "[ERROR] {}", msg);
        }
    }
}

}  // namespace simrv::log
