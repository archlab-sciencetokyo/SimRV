/**
 * @file LogModal.cpp
 * @brief Implementation of interactive LogModal.
 */

#include "simrv/tui/modals/LogModal.hpp"

#include <algorithm>
#include <format>
#include <string_view>

#include "simrv/core/Logger.hpp"
#include "simrv/tui/LogBuffer.hpp"

namespace simrv::tui::modals {

void LogModal::render_content(std::vector<std::string>& content_rows, int width, int height) {
    auto add_row = [&](std::string s) { content_rows.push_back(std::move(s)); };
    if (log_buffer_) render(content_rows, add_row, *log_buffer_, height, width);
}

void LogModal::scroll_to_top() { scroll_page(-99999, 0, 0); }

void LogModal::scroll_to_bottom() { scroll_page(99999, 0, 0); }

void LogModal::handle_wheel(int delta) { scroll(delta * 2, 2000, 20); }

auto LogModal::handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction {
    if (is_search_mode()) {
        if (byte == 27 || key == simrv::tui::TuiKey::Esc) {
            toggle_search_mode();
            return framework::ModalAction::Continue;
        }
        if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) {
            toggle_search_mode();
            return framework::ModalAction::Continue;
        }
        if (byte == 8 || byte == 127 || key == simrv::tui::TuiKey::Backspace) {
            std::string q = search_query();
            if (!q.empty()) q.pop_back();
            set_search_query(std::move(q));
            return framework::ModalAction::Continue;
        }
        if (byte >= 32 && byte <= 126) {
            std::string q = search_query();
            q.push_back(static_cast<char>(byte));
            set_search_query(std::move(q));
            return framework::ModalAction::Continue;
        }
        return framework::ModalAction::Continue;
    }

    if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q') {
        return framework::ModalAction::Close;
    }
    if (byte == 'a' || byte == 'A') {
        set_filter_level(std::nullopt);
        return framework::ModalAction::Continue;
    }
    if (byte == 'i' || byte == 'I') {
        set_filter_level(simrv::log::Level::Info);
        return framework::ModalAction::Continue;
    }
    if (byte == 'w' || byte == 'W') {
        set_filter_level(simrv::log::Level::Warn);
        return framework::ModalAction::Continue;
    }
    if (byte == 'e' || byte == 'E') {
        set_filter_level(simrv::log::Level::Error);
        return framework::ModalAction::Continue;
    }
    if (byte == 'f' || byte == 'F') {
        toggle_follow();
        return framework::ModalAction::Continue;
    }
    if (byte == '/') {
        toggle_search_mode();
        return framework::ModalAction::Continue;
    }
    if (byte == 'j' || byte == 'J') {
        scroll(1, 2000, 20);
        return framework::ModalAction::Continue;
    }
    if (byte == 'k' || byte == 'K') {
        scroll(-1, 2000, 20);
        return framework::ModalAction::Continue;
    }
    return framework::ModalAction::Continue;
}

auto LogModal::handle_control_click(int /*content_row*/, size_t action_index)
    -> framework::ModalAction {
    return action_index == 0 ? framework::ModalAction::Submit : framework::ModalAction::Close;
}

auto LogModal::handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction {
    (void)local_pt;
    return framework::ModalAction::Continue;
}

namespace {

auto strip_ansi(std::string_view str) -> std::string {
    std::string out;
    out.reserve(str.size());
    bool in_escape = false;
    for (std::size_t i = 0; i < str.size(); ++i) {
        if (!in_escape && str[i] == '\033') {
            in_escape = true;
            continue;
        }
        if (in_escape) {
            if (str[i] == 'm') in_escape = false;
            continue;
        }
        out += str[i];
    }
    return out;
}

}  // namespace

void LogModal::open(const LogBuffer& /*log_buffer*/) {
    follow_tail_ = true;
    scroll_offset_y_ = 0;
    search_mode_ = false;
}

void LogModal::scroll(int delta, int total_lines, int visible_lines) {
    int max_offset = std::max(0, total_lines - visible_lines);
    scroll_offset_y_ = std::clamp(scroll_offset_y_ + delta, 0, max_offset);
    if (delta < 0) {
        follow_tail_ = false;
    } else if (scroll_offset_y_ >= max_offset) {
        follow_tail_ = true;
    }
}

void LogModal::scroll_page(int pages, int total_lines, int visible_lines) {
    scroll(pages * std::max(1, visible_lines - 2), total_lines, visible_lines);
}

void LogModal::toggle_follow() {
    follow_tail_ = !follow_tail_;
    if (follow_tail_) {
        scroll_offset_y_ = 999999;
    }
}

void LogModal::set_filter_level(std::optional<simrv::log::Level> level) {
    min_level_ = level;
    if (follow_tail_) {
        scroll_offset_y_ = 999999;
    }
}

void LogModal::cycle_filter_level() {
    if (!min_level_.has_value()) {
        min_level_ = simrv::log::Level::Info;
    } else if (*min_level_ == simrv::log::Level::Info) {
        min_level_ = simrv::log::Level::Warn;
    } else if (*min_level_ == simrv::log::Level::Warn) {
        min_level_ = simrv::log::Level::Error;
    } else {
        min_level_ = std::nullopt;
    }
    if (follow_tail_) {
        scroll_offset_y_ = 999999;
    }
}

void LogModal::set_search_query(std::string query) {
    search_query_ = std::move(query);
    if (follow_tail_) {
        scroll_offset_y_ = 999999;
    }
}

void LogModal::toggle_search_mode() { search_mode_ = !search_mode_; }

auto LogModal::is_search_mode() -> bool { return search_mode_; }

auto LogModal::search_query() -> const std::string& { return search_query_; }

auto LogModal::filter_level() -> std::optional<simrv::log::Level> { return min_level_; }

auto LogModal::is_following() -> bool { return follow_tail_; }

auto LogModal::copy_text(const LogBuffer& log_buffer) -> std::string {
    auto lines = log_buffer.get_wrapped_lines(200, 5000, min_level_, search_query_);
    std::string result;
    for (const auto& line : lines) {
        result += strip_ansi(line);
        result += '\n';
    }
    return result;
}

void LogModal::render([[maybe_unused]] std::vector<std::string>& content_rows,
                      const std::function<void(const std::string&)>& add_row,
                      const LogBuffer& log_buffer, int term_height, int term_width) {
    const int box_w = std::clamp(term_width - 4, 40, 100);
    const int visible_lines = std::max(6, term_height - 10);

    // Filter toolbar line
    auto filter_badge = [](std::string_view color, std::string_view text, bool selected) {
        if (selected) return std::format("\033[1;7m {} \033[0m", text);
        return std::format("{}{}\033[0m", color, text);
    };
    const std::string opt_all = filter_badge("\033[39m", "[a] ALL", !min_level_.has_value());
    const std::string opt_info = filter_badge(
        simrv::log::standard_ansi_color(simrv::log::Level::Info), "[i] INFO+",
        min_level_ == simrv::log::Level::Info);
    const std::string opt_warn = filter_badge(
        simrv::log::standard_ansi_color(simrv::log::Level::Warn), "[w] WARN+",
        min_level_ == simrv::log::Level::Warn);
    const std::string opt_err = filter_badge(
        simrv::log::standard_ansi_color(simrv::log::Level::Error), "[e] ERROR",
        min_level_ == simrv::log::Level::Error);
    const std::string follow_badge = follow_tail_
                                         ? std::format("{}[f] Follow: ON\033[0m",
                                                       simrv::log::standard_ansi_color(
                                                           simrv::log::Level::Info))
                                         : std::format("{}[f] Follow: OFF\033[0m",
                                                       simrv::log::standard_ansi_color(
                                                           simrv::log::Level::Warn));

    std::string stats_badge;
    if (log_buffer.error_count() > 0) {
        stats_badge += std::format("{}{} ✖\033[0m ",
                                   simrv::log::standard_ansi_color(simrv::log::Level::Error),
                                   log_buffer.error_count());
    }
    if (log_buffer.warn_count() > 0) {
        stats_badge += std::format("{}{} ⚠\033[0m",
                                   simrv::log::standard_ansi_color(simrv::log::Level::Warn),
                                   log_buffer.warn_count());
    }
    if (stats_badge.empty()) {
        stats_badge = std::format("{}0 err\033[0m",
                                  simrv::log::standard_ansi_color(simrv::log::Level::Info));
    }

    add_row(std::format(" {}  {}  {}  {}  │  {}  │  {}", opt_all, opt_info, opt_warn, opt_err,
                        follow_badge, stats_badge));

    std::string filter_line = " \033[1mFilter [/]:\033[0m ";
    if (search_query_.empty()) {
        filter_line += "\033[2m<type to filter logs>\033[0m";
    } else {
        filter_line +=
            std::format("\033[1;33m\"{}\"\033[0m (press [/] to edit/clear)", search_query_);
    }
    add_row(filter_line);
    add_row("");

    auto all_lines = log_buffer.get_wrapped_lines(box_w - 4, 3000, min_level_, search_query_);
    int total_lines = static_cast<int>(all_lines.size());

    int max_offset = std::max(0, total_lines - visible_lines);
    if (follow_tail_) {
        scroll_offset_y_ = max_offset;
    } else {
        scroll_offset_y_ = std::clamp(scroll_offset_y_, 0, max_offset);
    }

    if (all_lines.empty()) {
        add_row("  \033[2m[No log messages matching current filter]\033[0m");
        for (int i = 1; i < visible_lines; ++i) {
            add_row("");
        }
    } else {
        for (int i = 0; i < visible_lines; ++i) {
            int idx = scroll_offset_y_ + i;
            if (idx >= 0 && idx < total_lines) {
                add_row(" " + all_lines[static_cast<size_t>(idx)]);
            } else {
                add_row("");
            }
        }
    }

    add_row("");
    add_row(
        std::format("\033[2mLines {}-{} of {} · [↑/↓/PgUp/PgDn] Scroll · [f] Follow · [y] Copy · "
                    "[Esc] Close\033[0m",
                    std::min(total_lines, scroll_offset_y_ + 1),
                    std::min(total_lines, scroll_offset_y_ + visible_lines), total_lines));
}

}  // namespace simrv::tui::modals
