/**
 * @file LogBuffer.cpp
 * @brief Implementation of LogBuffer with ANSI word-wrapping and continuation indents.
 */

#include "simrv/tui/LogBuffer.hpp"

#include <sstream>

#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui {

namespace {

struct AnsiToken {
    std::string text;
    bool is_ansi;
    int display_width;
};

auto tokenize_ansi(std::string_view line) -> std::vector<AnsiToken> {
    std::vector<AnsiToken> tokens;
    std::size_t i = 0;
    std::string text_accum;

    while (i < line.size()) {
        if (line[i] == '\033' && i + 1 < line.size() && line[i + 1] == '[') {
            if (!text_accum.empty()) {
                tokens.push_back({.text = text_accum,
                                  .is_ansi = false,
                                  .display_width = get_display_width(text_accum)});
                text_accum.clear();
            }
            std::size_t end = line.find('m', i + 2);
            if (end != std::string_view::npos) {
                tokens.push_back({.text = std::string(line.substr(i, end - i + 1)),
                                  .is_ansi = true,
                                  .display_width = 0});
                i = end + 1;
                continue;
            }
        }
        text_accum += line[i];
        ++i;
    }

    if (!text_accum.empty()) {
        tokens.push_back(
            {.text = text_accum, .is_ansi = false, .display_width = get_display_width(text_accum)});
    }
    return tokens;
}

void wrap_single_line(std::string_view raw_line, int max_width,
                      std::vector<std::string>& out_lines) {
    if (raw_line.empty()) {
        out_lines.emplace_back("");
        return;
    }
    if (max_width <= 4) {
        out_lines.emplace_back(std::string(raw_line));
        return;
    }

    auto tokens = tokenize_ansi(raw_line);
    std::string current_line;
    int current_width = 0;
    std::string active_ansi;
    bool is_continuation = false;

    for (const auto& token : tokens) {
        if (token.is_ansi) {
            if (token.text == "\033[0m") {
                active_ansi.clear();
            } else {
                active_ansi += token.text;
            }
            current_line += token.text;
            continue;
        }

        std::string_view text_sv = token.text;
        std::size_t pos = 0;

        while (pos < text_sv.size()) {
            std::size_t space_pos = text_sv.find_first_of(" \t", pos);
            std::string_view word;
            std::string_view delimiter;

            if (space_pos == std::string_view::npos) {
                word = text_sv.substr(pos);
                pos = text_sv.size();
            } else {
                word = text_sv.substr(pos, space_pos - pos);
                delimiter = text_sv.substr(space_pos, 1);
                pos = space_pos + 1;
            }

            int const target_limit = is_continuation ? (max_width - 2) : max_width;
            int const word_w = get_display_width(word);
            int const delim_w = get_display_width(delimiter);

            if (current_width + word_w > target_limit && current_width > 0) {
                current_line += "\033[0m";
                out_lines.push_back(current_line);
                current_line = "  " + active_ansi;
                current_width = 2;
                is_continuation = true;
            }

            if (word_w > (max_width - 2) && current_width == 2) {
                // Word is excessively long, split by characters
                for (char c : word) {
                    if (current_width >= max_width) {
                        current_line += "\033[0m";
                        out_lines.push_back(current_line);
                        current_line = "  " + active_ansi;
                        current_width = 2;
                    }
                    current_line += c;
                    current_width += 1;
                }
            } else {
                current_line += word;
                current_width += word_w;
            }

            if (!delimiter.empty()) {
                if (current_width + delim_w > max_width) {
                    current_line += "\033[0m";
                    out_lines.push_back(current_line);
                    current_line = "  " + active_ansi;
                    current_width = 2;
                    is_continuation = true;
                } else {
                    current_line += delimiter;
                    current_width += delim_w;
                }
            }
        }
    }

    if (!current_line.empty()) {
        current_line += "\033[0m";
        out_lines.push_back(current_line);
    }
}

}  // namespace

LogBuffer::LogBuffer(std::size_t max_capacity) : max_capacity_(max_capacity) {}

void LogBuffer::push(std::string message) {
    std::scoped_lock lock(mutex_);
    if (entries_.size() >= max_capacity_) {
        entries_.pop_front();
    }
    entries_.push_back(LogEntry{.text = std::move(message)});
}

void LogBuffer::clear() {
    std::scoped_lock lock(mutex_);
    entries_.clear();
}

auto LogBuffer::size() const -> std::size_t {
    std::scoped_lock lock(mutex_);
    return entries_.size();
}

auto LogBuffer::empty() const -> bool {
    std::scoped_lock lock(mutex_);
    return entries_.empty();
}

auto LogBuffer::get_wrapped_lines(int max_width, int max_rows) const -> std::vector<std::string> {
    std::scoped_lock lock(mutex_);
    if (entries_.empty() || max_width <= 0 || max_rows <= 0) {
        return {};
    }

    std::vector<std::string> all_wrapped;
    for (const auto& entry : entries_) {
        std::stringstream ss(entry.text);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            wrap_single_line(line, max_width, all_wrapped);
        }
    }

    if (static_cast<int>(all_wrapped.size()) <= max_rows) {
        return all_wrapped;
    }

    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(max_rows));
    auto start_it = all_wrapped.end() - max_rows;
    result.insert(result.end(), start_it, all_wrapped.end());
    return result;
}

}  // namespace simrv::tui
