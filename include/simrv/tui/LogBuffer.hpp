/**
 * @file LogBuffer.hpp
 * @brief Structured log buffer with dynamic ANSI-aware word-wrapping and continuation indents.
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/core/Logger.hpp"

namespace simrv::tui {

struct LogEntry {
    std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now();
    simrv::log::Level level = simrv::log::Level::Info;
    std::string text;
};

class LogBuffer {
   public:
    explicit LogBuffer(std::size_t max_capacity = 2000);

    void push(std::string message);
    void push(simrv::log::Level level, std::string message,
              std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now());
    void push(LogEntry entry);
    void clear();

    [[nodiscard]] auto get_wrapped_lines(int max_width, int max_rows,
                                         std::optional<simrv::log::Level> min_level = std::nullopt,
                                         std::string_view search_filter = "") const
        -> std::vector<std::string>;

    [[nodiscard]] auto get_all_entries() const -> std::vector<LogEntry>;

    [[nodiscard]] auto size() const -> std::size_t;
    [[nodiscard]] auto empty() const -> bool;
    [[nodiscard]] auto warn_count() const -> std::size_t;
    [[nodiscard]] auto error_count() const -> std::size_t;

   private:
    std::size_t max_capacity_;
    mutable std::mutex mutex_;
    std::deque<LogEntry> entries_;
    std::size_t warn_count_ = 0;
    std::size_t error_count_ = 0;
    std::uint64_t revision_ = 0;
    mutable std::uint64_t cached_revision_ = static_cast<std::uint64_t>(-1);
    mutable int cached_width_ = 0;
    mutable int cached_rows_ = 0;
    mutable std::optional<simrv::log::Level> cached_min_level_ = std::nullopt;
    mutable std::string cached_filter_;
    mutable std::vector<std::string> cached_wrapped_lines_;
};

}  // namespace simrv::tui
