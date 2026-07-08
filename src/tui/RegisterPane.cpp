/**
 * @file RegisterPane.cpp
 * @brief Implements RegisterPane widget rendering base and infrastructure.
 */
#include "simrv/tui/RegisterPane.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/xlen/Types.hpp"
#include <chrono>
#include <format>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

namespace simrv::tui {

auto RegisterPane::is_single_column(int width) const -> bool {
    bool const is_reg_page = (page_ == TuiRegPage::GPR || page_ == TuiRegPage::FPR || page_ == TuiRegPage::VEC);
    if (!is_reg_page) return false;
    if (page_ == TuiRegPage::GPR) {
        return (simrv::xlen::kIsXLen64 && width < 58) || (!simrv::xlen::kIsXLen64 && width < 42);
    }
    return width < 58;
}

auto RegisterPane::get_total_rows(int width) -> int {
    if (page_ == TuiRegPage::EXPLAIN) {
        return static_cast<int>(get_explain_rows(width).size());
    }
    bool const single_column = is_single_column(width);
    int base_rows = machine_.s_cycle_accurate ? 43 : 35;
    if (single_column) {
        base_rows += 16;
    }
    int debug_rows = machine_.s_debug_mode ? 4 : 0;
    return base_rows + debug_rows;
}

auto RegisterPane::section_line(const std::string& title, int width) -> std::string {
    if (title.starts_with("─") || title.starts_with(" ")) {
        std::string full = title + " ";
        int dash_len = width - get_display_width(full);
        if (dash_len < 0) dash_len = 0;
        return std::format("\033[1;38;5;254m{} \033[0m{}{}", title, kThemeBorder, make_repeated_string("─", dash_len));
    } else {
        std::string text = " " + title + " ";
        int dash_len = width - get_display_width(text);
        if (dash_len < 0) dash_len = 0;
        int left_dashes = std::min(4, dash_len / 2);
        int right_dashes = dash_len - left_dashes;
        return std::format("{}{} \033[1;38;5;254m{}\033[0m {}{}", 
                           kThemeBorder, make_repeated_string("─", left_dashes),
                           title,
                           kThemeBorder, make_repeated_string("─", right_dashes));
    }
}

auto RegisterPane::make_field(const std::string& label, const std::string& value,
                             const char* value_color, int label_pad) -> std::string {
    if (label_pad == 0) {
        return std::format(" {}{}\033[0m: {}{}\033[0m", kThemeText, label, value_color, value);
    } else {
        return std::format(" {}{:<{}}\033[0m: {}{}\033[0m", kThemeText, label, label_pad, value_color, value);
    }
}

auto RegisterPane::render_pair(const std::string& l1, const std::string& v1, const char* c1,
                               const std::string& l2, const std::string& v2, const char* c2,
                               int col_width, int right_width, int label_pad) -> std::string {
    return format_to_width(make_field(l1, v1, c1, label_pad), col_width) +
           format_to_width(make_field(l2, v2, c2, label_pad), right_width);
}

auto RegisterPane::render_active_spinner(int logical_row, int width) -> std::string {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    constexpr std::array<const char*, 10> spinner = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    std::string spin = spinner.at((static_cast<std::size_t>(now_ms / 80)) % 10);

    bool const is_reg_page = (page_ == TuiRegPage::GPR || page_ == TuiRegPage::FPR || page_ == TuiRegPage::VEC);
    bool const single_column = is_reg_page && ([&]() -> bool {
        if (page_ == TuiRegPage::GPR) {
            return (simrv::xlen::kIsXLen64 && width < 58) || (!simrv::xlen::kIsXLen64 && width < 42);
        }
        return width < 58;
    }());

    int target_row_offset = single_column ? 8 : 0;
    if (logical_row == 10 + target_row_offset) {
        std::string text = std::format("\033[38;5;218m●\033[0m \033[1;38;5;218mSIMULATOR ACTIVE\033[0m");
        int spaces = std::max(0, (width - 18) / 2);
        std::string line = std::string(spaces, ' ') + text;
        return format_to_width(line, width);
    }
    if (logical_row == 11 + target_row_offset) {
        std::string text = std::format("[  \033[38;5;121m{}\033[0m  Executing instructions... ]", spin);
        int spaces = std::max(0, (width - 33) / 2);
        std::string line = std::string(spaces, ' ') + text;
        return format_to_width(line, width);
    }
    if (logical_row == 12 + target_row_offset) {
        std::string text = std::format("Press \033[38;5;183m[Ctrl-P]\033[0m or \033[38;5;183mClick Mouse\033[0m to pause");
        int spaces = std::max(0, (width - 38) / 2);
        std::string line = std::string(spaces, ' ') + text;
        return format_to_width(line, width);
    }
    return format_to_width("", width);
}

auto RegisterPane::get_row_uncached(int logical_row, int width) -> std::string {
    auto const& cpu = machine_.cpu;
    auto const& st = cpu.state();
    int const col_width = width / 2;
    int const right_width = width - col_width;
    bool const single_column = is_single_column(width);

    std::string res = render_registers_or_pipeline(cpu, st, logical_row, col_width, right_width, width, single_column);
    if (!res.empty()) {
        return res;
    }

    res = render_system_or_pipeline_extended(cpu, logical_row, col_width, right_width, single_column);
    if (!res.empty()) {
        return res;
    }

    return render_perf_or_debug(cpu, logical_row, width, single_column);
}

auto RegisterPane::render_row(int row_idx, int width) -> std::string {
    last_width_ = width;
    int const logical_row = row_idx + scroll_offset_;

    if (page_ == TuiRegPage::EXPLAIN) {
        auto explain_rows = get_explain_rows(width);
        int const total_logical_rows = static_cast<int>(explain_rows.size());
        if (logical_row >= total_logical_rows || logical_row < 0) {
            return format_to_width("", width);
        }
        return explain_rows.at(static_cast<std::size_t>(logical_row));
    }

    int const total_logical_rows = get_total_rows(width);
    if (logical_row >= total_logical_rows || logical_row < 0) {
        return format_to_width("", width);
    }

    bool const single_column = is_single_column(width);
    int const max_active_row = single_column ? 40 : 24;

    if (!paused_ && logical_row <= max_active_row) {
        return render_active_spinner(logical_row, width);
    }

    if (logical_row <= max_active_row) {
        std::string res = get_row_uncached(logical_row, width);
        if (static_cast<std::size_t>(logical_row) < cached_left_rows_.size()) {
            cached_left_rows_.at(static_cast<std::size_t>(logical_row)) = res;
        }
        return res;
    }

    return get_row_uncached(logical_row, width);
}

void RegisterPane::update_cache() {
    auto& st = machine_.cpu.state();
    for (int i = 0; i < 32; ++i) {
        cached_gpr_.at(static_cast<std::size_t>(i)) = st.regs.read(static_cast<RegId>(i));
        cached_fpr_.at(static_cast<std::size_t>(i)) = st.regs.read_fp(static_cast<RegId>(i));
        cached_vec_.at(static_cast<std::size_t>(i)) = st.regs.read_vector(static_cast<RegId>(i)).u64[0]; // NOLINT(cppcoreguidelines-pro-type-union-access)
    }
}

void RegisterPane::scroll(int lines) {
    int w = last_width_ > 0 ? last_width_ : 60;
    int total_logical_rows = get_total_rows(w);
    int max_scroll = std::max(0, total_logical_rows - visible_rows_);
    scroll_offset_ += lines;
    if (scroll_offset_ > max_scroll) {
        scroll_offset_ = max_scroll;
    }
    if (scroll_offset_ < 0) {
        scroll_offset_ = 0;
    }
}

} // namespace simrv::tui
