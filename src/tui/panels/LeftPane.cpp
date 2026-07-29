/**
 * @file LeftPane.cpp
 * @brief Implements LeftPane widget rendering base and infrastructure.
 */
#include "simrv/tui/panels/LeftPane.hpp"
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

auto LeftPane::is_single_column(int width) const -> bool {
    bool const is_reg_page = (page_ == TuiRegPage::GPR || page_ == TuiRegPage::FPR || page_ == TuiRegPage::VEC);
    if (!is_reg_page) return false;
    if (page_ == TuiRegPage::GPR) {
        return (simrv::xlen::kIsXLen64 && width < 58) || (!simrv::xlen::kIsXLen64 && width < 42);
    }
    if (page_ == TuiRegPage::VEC) {
        return width < 58;
    }
    return width < 58;
}

auto LeftPane::get_total_rows(int width) -> int {
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

auto LeftPane::section_line(const std::string& title, int width) -> std::string {
    if (title.starts_with("─") || title.starts_with(" ")) {
        std::string full = title + " ";
        int dash_len = width - get_display_width(full);
        if (dash_len < 0) dash_len = 0;
        return std::format("\033[1m{}{} \033[0m{}{}", kThemeText, title, kThemeBorder, make_repeated_string("─", dash_len));
    } else {
        std::string text = " " + title + " ";
        int dash_len = width - get_display_width(text);
        if (dash_len < 0) dash_len = 0;
        int left_dashes = std::min(4, dash_len / 2);
        int right_dashes = dash_len - left_dashes;
        return std::format("{}{} \033[1m{}{}\033[0m {}{}", 
                           kThemeBorder, make_repeated_string("─", left_dashes),
                           kThemeText, title,
                           kThemeBorder, make_repeated_string("─", right_dashes));
    }
}

auto LeftPane::make_field(const std::string& label, const std::string& value,
                          const char* value_color, int label_pad) -> std::string {
    if (label_pad == 0) {
        return std::format(" {}{}\033[0m: {}{}\033[0m", kThemeText, label, value_color, value);
    } else {
        return std::format(" {}{:<{}}\033[0m: {}{}\033[0m", kThemeText, label, label_pad, value_color, value);
    }
}

auto LeftPane::render_pair(const std::string& l1, const std::string& v1, const char* c1,
                            const std::string& l2, const std::string& v2, const char* c2,
                            int col_width, int right_width, int label_pad) -> std::string {
    return format_to_width(make_field(l1, v1, c1, label_pad), col_width) +
           format_to_width(make_field(l2, v2, c2, label_pad), right_width);
}

auto LeftPane::render_active_spinner(int logical_row, int width) -> std::string {
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
        std::string text = std::format("{}●\033[0m \033[1m{}SIMULATOR ACTIVE\033[0m", kThemePink, kThemePink);
        int spaces = std::max(0, (width - 18) / 2);
        std::string line = std::string(spaces, ' ') + text;
        return format_to_width(line, width);
    }
    if (logical_row == 11 + target_row_offset) {
        std::string text = std::format("[  {}{}\033[0m  Executing instructions... ]", kThemeMint, spin);
        int spaces = std::max(0, (width - 33) / 2);
        std::string line = std::string(spaces, ' ') + text;
        return format_to_width(line, width);
    }
    if (logical_row == 12 + target_row_offset) {
        std::string text = std::format("Press {}[Ctrl-P]\033[0m or {}[Click Mouse]\033[0m to pause", kThemeSky, kThemeSky);
        int spaces = std::max(0, (width - 38) / 2);
        std::string line = std::string(spaces, ' ') + text;
        return format_to_width(line, width);
    }
    return format_to_width("", width);
}

auto LeftPane::get_row_uncached(int logical_row, int width) -> std::string {
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

auto LeftPane::render_tab_bar(int width) const -> std::string {
    // Determine if we're in the Regs group (GPR/FPR/VEC)
    bool const is_regs = (page_ == TuiRegPage::GPR || page_ == TuiRegPage::FPR || page_ == TuiRegPage::VEC);
    const char* reg_sub = "GPR";
    if (page_ == TuiRegPage::FPR) reg_sub = "FPR";
    else if (page_ == TuiRegPage::VEC) reg_sub = "VEC";

    // Build the grouped Regs tab
    std::string line;
    if (is_regs) {
        line += std::format("\033[1m{}[Regs:{}]\033[0m", kThemeSky, reg_sub);
    } else {
        line += std::format("{}Regs\033[0m", kThemeMuted);
    }

    // Remaining tool tabs
    struct ToolTab { TuiRegPage page; const char* name; };
    std::vector<ToolTab> tool_tabs;
    tool_tabs.push_back({.page = TuiRegPage::PIPELINE, .name = "Pipe"});
    if (machine_.s_cycle_accurate) {
        tool_tabs.push_back({.page = TuiRegPage::CACHE, .name = "Cache"});
    }
    tool_tabs.push_back({.page = TuiRegPage::TRACE, .name = "Trace"});
    tool_tabs.push_back({.page = TuiRegPage::EXPLAIN, .name = "Exp"});
    tool_tabs.push_back({.page = TuiRegPage::STACK, .name = "Stack"});

    for (auto const& tab : tool_tabs) {
        line += std::format("{}│\033[0m", kThemeBorder);
        if (page_ == tab.page) {
            line += std::format("\033[1m{}[{}]\033[0m", kThemeSky, tab.name);
        } else {
            line += std::format("{}{}\033[0m", kThemeMuted, tab.name);
        }
    }

    return format_to_width(line, width);
}

auto LeftPane::get_tab_at_col(int col) const -> std::optional<TuiRegPage> {
    bool const is_regs = (page_ == TuiRegPage::GPR || page_ == TuiRegPage::FPR || page_ == TuiRegPage::VEC);
    int regs_width = is_regs ? 10 : 4;

    int current_x = 0;
    if (col < current_x + regs_width) {
        return std::nullopt; // Clicked on Regs tab
    }
    current_x += regs_width + 1; // +1 for │

    struct ToolTab { TuiRegPage page; const char* name; };
    std::vector<ToolTab> tool_tabs;
    tool_tabs.push_back({.page = TuiRegPage::PIPELINE, .name = "Pipe"});
    if (machine_.s_cycle_accurate) {
        tool_tabs.push_back({.page = TuiRegPage::CACHE, .name = "Cache"});
    }
    tool_tabs.push_back({.page = TuiRegPage::TRACE, .name = "Trace"});
    tool_tabs.push_back({.page = TuiRegPage::EXPLAIN, .name = "Exp"});
    tool_tabs.push_back({.page = TuiRegPage::STACK, .name = "Stack"});

    for (auto const& tab : tool_tabs) {
        int tab_width = (page_ == tab.page) ? (static_cast<int>(std::strlen(tab.name)) + 2) : static_cast<int>(std::strlen(tab.name));
        if (col < current_x + tab_width) {
            return tab.page;
        }
        current_x += tab_width + 1;
    }

    return std::nullopt;
}

auto LeftPane::render_trace_row(int logical_row, int width) -> std::string {
    if (!trace_buffer_ || trace_buffer_->empty()) {
        if (logical_row == 1) {
            return format_to_width(" [No execution trace recorded yet]", width);
        }
        return format_to_width("", width);
    }
    int total = static_cast<int>(trace_buffer_->size());
    if (logical_row < 0 || logical_row >= total) {
        return format_to_width("", width);
    }
    return format_to_width(" " + trace_buffer_->at(static_cast<std::size_t>(logical_row)), width);
}

auto LeftPane::render_log_bottom_row(int row_idx, int num_rows, int width) -> std::string {
    if (row_idx == 0) {
        return section_line("Log", width);
    }
    if (log_lines_.empty()) {
        return format_to_width("", width);
    }
    int total = static_cast<int>(log_lines_.size());
    int max_entries = num_rows - 1;
    int log_idx = total - max_entries + (row_idx - 1);
    if (log_idx < 0 || log_idx >= total) {
        return format_to_width("", width);
    }
    // Log lines come from vt_log_ with their own content — prepend space to align with pane section text.
    return format_to_width(" " + log_lines_.at(static_cast<std::size_t>(log_idx)), width);
}

auto LeftPane::render_row(int row_idx, int width) -> std::string {
    last_width_ = width;

    if (row_idx == 0) {
        return render_tab_bar(width);
    }

    constexpr int kLogAreaHeight = 10;
    if (visible_rows_ >= 15 && row_idx >= visible_rows_ - kLogAreaHeight) {
        int log_row_idx = row_idx - (visible_rows_ - kLogAreaHeight);
        return render_log_bottom_row(log_row_idx, kLogAreaHeight, width);
    }

    int const content_row_idx = row_idx - 1;
    int const logical_row = content_row_idx + scroll_offset_;

    if (page_ == TuiRegPage::TRACE) {
        return render_trace_row(logical_row, width);
    }

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

    bool show_spinner = !paused_;
    if (show_spinner && machine_.tui) {
        uint64_t delay = machine_.tui->step_delay_us_.load(std::memory_order_relaxed);
        if (delay >= 10000) {
            show_spinner = false;
        }
    }

    if (show_spinner && logical_row <= max_active_row) {
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

void LeftPane::update_cache() {
    auto& st = machine_.cpu.state();
    for (int i = 0; i < 32; ++i) {
        cached_gpr_.at(static_cast<std::size_t>(i)) = st.regs.read(static_cast<RegId>(i));
        cached_fpr_.at(static_cast<std::size_t>(i)) = st.regs.read_fp(static_cast<RegId>(i));
        cached_vec_.at(static_cast<std::size_t>(i)) = st.regs.read_vector(static_cast<RegId>(i));
    }
}

void LeftPane::scroll(int lines) {
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
