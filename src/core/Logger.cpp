/**
 * @file Logger.cpp
 * @brief Implementation of centralized logger.
 */

#include "simrv/core/Logger.hpp"

#include <unistd.h>

#include <deque>
#include <mutex>
#include <print>

#include "simrv/util/FormatUtil.hpp"

namespace simrv::log {

namespace {
enum class Level : uint8_t { Info, Warn, Error };
struct PendingLog {
    Level level;
    std::string message;
};

constexpr std::size_t kStartupLogLimit = 256;
simrv::log::LogCallback
    g_tui_callback;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::deque<PendingLog>
    g_startup_logs;      // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::mutex g_log_mutex;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

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
    if (g_startup_logs.size() == kStartupLogLimit) g_startup_logs.pop_front();
    g_startup_logs.push_back({level, message});
    return {};
}
}  // namespace

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

void print_info(const std::string& msg) {
    if (auto callback = callback_or_buffer(Level::Info, msg)) {
        callback(tui_message(Level::Info, msg));
    } else {
        if (simrv::util::is_terminal(STDOUT_FILENO)) {
            std::println(stdout, "\033[38;5;117m{}\033[0m", msg);  // Sakura Sky Blue
        } else {
            std::println(stdout, "[INFO] {}", msg);
        }
    }
}

void print_warn(const std::string& msg) {
    if (auto callback = callback_or_buffer(Level::Warn, msg)) {
        callback(tui_message(Level::Warn, msg));
    } else {
        if (simrv::util::is_terminal(STDERR_FILENO)) {
            std::println(stderr, "\033[38;5;223m{}\033[0m", msg);  // Sakura Peach
        } else {
            std::println(stderr, "[WARN] {}", msg);
        }
    }
}

void print_error(const std::string& msg) {
    if (auto callback = callback_or_buffer(Level::Error, msg)) {
        callback(tui_message(Level::Error, msg));
    } else {
        if (simrv::util::is_terminal(STDERR_FILENO)) {
            std::println(stderr, "\033[1;38;5;210m{}\033[0m", msg);  // Bold Sakura Coral
        } else {
            std::println(stderr, "[ERROR] {}", msg);
        }
    }
}

}  // namespace simrv::log
