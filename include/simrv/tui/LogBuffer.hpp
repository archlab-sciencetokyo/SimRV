/**
 * @file LogBuffer.hpp
 * @brief Structured log buffer with dynamic ANSI-aware word-wrapping and continuation indents.
 */
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace simrv::tui {

struct LogEntry {
    std::string text;
};

class LogBuffer {
   public:
    explicit LogBuffer(std::size_t max_capacity = 1000);

    void push(std::string message);
    void clear();

    [[nodiscard]] auto get_wrapped_lines(int max_width, int max_rows) const
        -> std::vector<std::string>;

    [[nodiscard]] auto size() const -> std::size_t;
    [[nodiscard]] auto empty() const -> bool;

   private:
    std::size_t max_capacity_;
    mutable std::mutex mutex_;
    std::deque<LogEntry> entries_;
    std::uint64_t revision_ = 0;
    mutable std::uint64_t cached_revision_ = static_cast<std::uint64_t>(-1);
    mutable int cached_width_ = 0;
    mutable int cached_rows_ = 0;
    mutable std::vector<std::string> cached_wrapped_lines_;
};

}  // namespace simrv::tui
