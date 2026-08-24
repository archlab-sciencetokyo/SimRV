/**
 * @file LeftPane.cpp
 * @brief Implements LeftPane widget rendering base and infrastructure.
 */
#include "simrv/tui/panels/LeftPane.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <string>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/tui/TuiGuidance.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/framework/Text.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::tui {

namespace {

[[nodiscard]] auto style_inline_separators(std::string row) -> std::string {
    constexpr std::string_view rule = "│";
    std::string const midpoint = std::format("{}·\033[0m", kThemeMuted);
    std::size_t pos = 0;
    while ((pos = row.find(rule, pos)) != std::string::npos) {
        row.replace(pos, rule.size(), midpoint);
        pos += midpoint.size();
    }
    return row;
}

}  // namespace

auto LeftPane::is_single_column(int width) const -> bool {
    bool const is_reg_page =
        (page_ == TuiRegPage::GPR || page_ == TuiRegPage::FPR || page_ == TuiRegPage::VEC);
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
    int base_rows = machine_.runtime_profile.is_cycle_mode() ? 43 : 35;
    if (single_column) {
        base_rows += 16;
    }
    int debug_rows = machine_.s_debug_mode ? 4 : 0;
    return base_rows + debug_rows;
}

auto LeftPane::section_line(const std::string& title, int width) -> std::string {
    auto const& glyphs = get_theme_glyphs(get_active_theme_style());
    std::string safe_title = title;
    if (get_display_width(safe_title) + 4 > width && width >= 10) {
        safe_title = safe_title.substr(0, static_cast<std::size_t>(width - 6)) + "…";
    }
    if (safe_title.starts_with("─") || safe_title.starts_with("-") || safe_title.starts_with(" ")) {
        std::string full = safe_title + " ";
        int dash_len = width - get_display_width(full);
        if (dash_len < 0) dash_len = 0;
        return std::format("\033[1m{}{} \033[0m{}{}", kThemeText, safe_title, kThemeBorder,
                           make_repeated_string(glyphs.horiz, dash_len));
    } else {
        std::string text = " " + safe_title + " ";
        int dash_len = width - get_display_width(text);
        if (dash_len < 0) dash_len = 0;
        int left_dashes = std::min(4, dash_len / 2);
        int right_dashes = dash_len - left_dashes;
        return std::format("{}{} \033[1m{}{}\033[0m {}{}", kThemeBorder,
                           make_repeated_string(glyphs.horiz, left_dashes), kThemeText, safe_title,
                           kThemeBorder, make_repeated_string(glyphs.horiz, right_dashes));
    }
}

auto LeftPane::make_field(const std::string& label, const std::string& value,
                          const char* value_color, int label_pad) -> std::string {
    if (label_pad == 0) {
        return std::format(" {}{}\033[0m: {}{}\033[0m", kThemeText, label, value_color, value);
    } else {
        return std::format(" {}{:<{}}\033[0m: {}{}\033[0m", kThemeText, label, label_pad,
                           value_color, value);
    }
}

auto LeftPane::render_pair(const std::string& l1, const std::string& v1, const char* c1,
                           const std::string& l2, const std::string& v2, const char* c2,
                           int col_width, int right_width, int label_pad) -> std::string {
    return format_to_width(make_field(l1, v1, c1, label_pad), col_width) +
           format_to_width(make_field(l2, v2, c2, label_pad), right_width);
}

auto LeftPane::get_running_label_start_row() const -> int {
    int const top_view_height = (visible_rows_ > 12) ? std::min(24, visible_rows_ - 12) : 10;
    return std::max(0, (top_view_height - 3) / 2);
}

auto LeftPane::render_active_spinner(int logical_row, int width) -> std::string {
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count();
    constexpr std::array<const char*, 10> spinner = {"⠋", "⠙", "⠹", "⠸", "⠼",
                                                     "⠴", "⠦", "⠧", "⠇", "⠏"};
    std::string spin = spinner.at((static_cast<std::size_t>(now_ms / 80)) % 10);

    int const start_row = get_running_label_start_row();

    if (logical_row == start_row) {
        std::string text =
            std::format("{}●\033[0m \033[1m{}SIMULATOR ACTIVE\033[0m", kThemePink, kThemePink);
        int spaces = std::max(0, (width - 18) / 2);
        std::string line = std::string(spaces, ' ') + text;
        return format_to_width(line, width);
    }
    if (logical_row == start_row + 1) {
        std::string text =
            std::format("[  {}{}\033[0m  Executing instructions... ]", kThemeMint, spin);
        int spaces = std::max(0, (width - 33) / 2);
        std::string line = std::string(spaces, ' ') + text;
        return format_to_width(line, width);
    }
    if (logical_row == start_row + 2) {
        std::string text =
            std::format("Press \033[1m{}[Ctrl-P]\033[0m or \033[1m{}[Click Here]\033[0m to pause",
                        kThemeSky, kThemeSky);
        int spaces = std::max(0, (width - 39) / 2);
        std::string line = std::string(spaces, ' ') + text;
        return format_to_width(line, width);
    }
    return format_to_width("", width);
}

auto LeftPane::is_running_label_click(int logical_row, int col, int width) const -> bool {
    int const start_row = get_running_label_start_row();

    if (logical_row == start_row) {
        int text_len = 18;  // "● SIMULATOR ACTIVE"
        int spaces = std::max(0, (width - text_len) / 2);
        return col >= (spaces - 1) && col <= (spaces + text_len + 1);
    }
    if (logical_row == start_row + 1) {
        int text_len = 33;  // "[  ⠋  Executing instructions... ]"
        int spaces = std::max(0, (width - text_len) / 2);
        return col >= (spaces - 1) && col <= (spaces + text_len + 1);
    }
    if (logical_row == start_row + 2) {
        int text_len = 39;  // "Press [Ctrl-P] or [Click Here] to pause"
        int spaces = std::max(0, (width - text_len) / 2);
        return col >= (spaces - 1) && col <= (spaces + text_len + 1);
    }
    return false;
}

void LeftPane::set_page(TuiRegPage page) {
    if (!machine_.runtime_profile.is_cycle_mode()) {
        if (page == TuiRegPage::CACHE) page = TuiRegPage::STACK;
        if (page == TuiRegPage::BPRED || page == TuiRegPage::HAZARD) page = TuiRegPage::PIPELINE;
    }
    if (page_ != page) horizontal_scroll_offset_ = 0;
    page_ = page;
}

void LeftPane::set_selected_hart(size_t hart) {
    if (machine_.num_harts() > 0) {
        selected_hart_ = hart % machine_.num_harts();
    } else {
        selected_hart_ = 0;
    }
    cached_left_rows_.fill("");
    update_cache();
}

auto LeftPane::current_cpu() const -> const simrv::core::CPU& {
    return machine_.hart(selected_hart_);
}

auto LeftPane::current_cpu() -> simrv::core::CPU& { return machine_.hart(selected_hart_); }

auto LeftPane::get_row_uncached(int logical_row, int width) -> std::string {
    auto const& cpu = current_cpu();
    auto const& st = cpu.state();
    int const col_width = width / 2;
    int const right_width = width - col_width;
    bool const single_column = is_single_column(width);

    std::string res = render_registers_or_pipeline(cpu, st, logical_row, col_width, right_width,
                                                   width, single_column);
    if (!res.empty()) {
        return res;
    }

    res =
        render_system_or_pipeline_extended(cpu, logical_row, col_width, right_width, single_column);
    if (!res.empty()) {
        return res;
    }

    return render_perf_or_debug(cpu, logical_row, width, single_column);
}

namespace {

struct TabSlot {
    std::string text;
    int display_width;
    TuiCategoryGroup grp = TuiCategoryGroup::Regs;
    std::optional<TuiRegPage> page = std::nullopt;
};

struct ComputedTabSpan {
    int start_col;
    int width;
    TuiCategoryGroup grp;
    std::optional<TuiRegPage> page;
};

auto layout_tabs_equally(const std::vector<TabSlot>& slots, int available_width)
    -> std::pair<std::string, std::vector<ComputedTabSpan>> {
    if (slots.empty()) return {"", {}};

    int total_item_w = 0;
    for (const auto& slot : slots) {
        total_item_w += slot.display_width;
    }
    constexpr int kTabGap = 2;
    int const num_gaps = static_cast<int>(slots.size()) - 1;
    int raw_w = total_item_w + num_gaps * kTabGap;
    int left_pad = std::max(0, (available_width - raw_w) / 2);

    std::string out;
    out.append(static_cast<std::size_t>(left_pad), ' ');
    int cur_col = left_pad;

    std::vector<ComputedTabSpan> spans;
    spans.reserve(slots.size());

    for (size_t i = 0; i < slots.size(); ++i) {
        if (i > 0) {
            out.append(kTabGap, ' ');
            cur_col += kTabGap;
        }
        spans.push_back({.start_col = cur_col,
                         .width = slots[i].display_width,
                         .grp = slots[i].grp,
                         .page = slots[i].page});
        out += slots[i].text;
        cur_col += slots[i].display_width;
    }

    if (get_display_width(out) < available_width) {
        out.append(static_cast<std::size_t>(available_width - get_display_width(out)), ' ');
    } else if (get_display_width(out) > available_width) {
        out = format_to_width(out, available_width);
    }
    return {out, spans};
}

}  // namespace

auto LeftPane::render_tab_bar_tier1(int width) const -> std::string {
    TuiCategoryGroup const current_grp = get_category_group(page_);
    constexpr std::array<TuiCategoryGroup, 4> kGroups = {
        TuiCategoryGroup::Regs, TuiCategoryGroup::Memory, TuiCategoryGroup::Pipeline,
        TuiCategoryGroup::Tools};

    std::vector<TabSlot> slots;
    slots.reserve(4);
    for (auto grp : kGroups) {
        const char* name = get_category_name(grp);
        std::string text;
        if (grp == current_grp) {
            text = std::format("\033[1;7m {} \033[0m", name);
        } else {
            text = std::format(" {}{}\033[0m ", kThemeMuted, name);
        }
        slots.push_back({.text = text,
                         .display_width = static_cast<int>(std::strlen(name)) + 2,
                         .grp = grp,
                         .page = std::nullopt});
    }

    return layout_tabs_equally(slots, width).first;
}

auto LeftPane::render_tab_bar_tier2(int width) const -> std::string {
    TuiCategoryGroup const grp = get_category_group(page_);
    struct SubTab {
        TuiRegPage page;
        std::string name;
    };
    std::vector<SubTab> tabs;

    switch (grp) {
        case TuiCategoryGroup::Regs: {
            tabs.push_back({.page = TuiRegPage::GPR, .name = "GPR"});
            bool has_f = (current_cpu().state().misa & (1ULL << ('f' - 'a'))) != 0;
            bool has_d = (current_cpu().state().misa & (1ULL << ('d' - 'a'))) != 0;
            bool has_v = (current_cpu().state().misa & (1ULL << ('v' - 'a'))) != 0;
            if (has_f || has_d) {
                tabs.push_back({.page = TuiRegPage::FPR, .name = "FPR"});
            }
            if (has_v) {
                tabs.push_back({.page = TuiRegPage::VEC, .name = "VEC"});
            }
            break;
        }
        case TuiCategoryGroup::Memory: {
            tabs.push_back({.page = TuiRegPage::STACK, .name = "Stack"});
            if (machine_.runtime_profile.is_cycle_mode()) {
                std::string cache_name = (cache_inspect_type_ == 0) ? "Cache:IC" : "Cache:DC";
                tabs.push_back({.page = TuiRegPage::CACHE, .name = cache_name});
            }
            tabs.push_back({.page = TuiRegPage::TLB, .name = "TLB"});
            tabs.push_back({.page = TuiRegPage::BUS, .name = "Bus"});
            break;
        }
        case TuiCategoryGroup::Pipeline: {
            tabs.push_back({.page = TuiRegPage::PIPELINE, .name = "Pipe"});
            if (machine_.runtime_profile.is_cycle_mode()) {
                tabs.push_back({.page = TuiRegPage::BPRED, .name = "BPred"});
                tabs.push_back({.page = TuiRegPage::HAZARD, .name = "Hazard"});
            }
            break;
        }
        case TuiCategoryGroup::Tools: {
            tabs.push_back({.page = TuiRegPage::EXPLAIN, .name = "Explain"});
            tabs.push_back({.page = TuiRegPage::TRACE, .name = "Trace"});
            break;
        }
    }

    std::vector<TabSlot> slots;
    slots.reserve(tabs.size());
    for (const auto& t : tabs) {
        std::string text;
        if (page_ == t.page) {
            text = std::format("\033[1;4m{} {} \033[0m", kThemeSky, t.name);
        } else {
            text = std::format(" {}{}\033[0m ", kThemeMuted, t.name);
        }
        slots.push_back({.text = text,
                         .display_width = static_cast<int>(t.name.length()) + 2,
                         .grp = grp,
                         .page = t.page});
    }

    return layout_tabs_equally(slots, width).first;
}

auto LeftPane::get_tab_at(int row, int col) const -> std::optional<TuiRegPage> {
    int const width = last_width_ > 0 ? last_width_ : 50;

    if (row == 0) {
        constexpr std::array<TuiCategoryGroup, 4> kGroups = {
            TuiCategoryGroup::Regs, TuiCategoryGroup::Memory, TuiCategoryGroup::Pipeline,
            TuiCategoryGroup::Tools};

        std::vector<TabSlot> slots;
        slots.reserve(4);
        for (auto grp : kGroups) {
            const char* name = get_category_name(grp);
            slots.push_back({.text = "",
                             .display_width = static_cast<int>(std::strlen(name)) + 2,
                             .grp = grp,
                             .page = std::nullopt});
        }

        auto const [_, spans] = layout_tabs_equally(slots, width);
        for (const auto& span : spans) {
            if (col >= span.start_col && col < span.start_col + span.width) {
                if (span.grp == get_category_group(page_)) {
                    // Clicking active group cycles within that group
                    if (span.grp == TuiCategoryGroup::Regs) {
                        if (page_ == TuiRegPage::GPR) return TuiRegPage::FPR;
                        if (page_ == TuiRegPage::FPR) return TuiRegPage::VEC;
                        return TuiRegPage::GPR;
                    }
                    if (span.grp == TuiCategoryGroup::Memory) {
                        if (page_ == TuiRegPage::STACK)
                            return machine_.runtime_profile.is_cycle_mode() ? TuiRegPage::CACHE
                                                                            : TuiRegPage::TLB;
                        if (page_ == TuiRegPage::CACHE) return TuiRegPage::TLB;
                        if (page_ == TuiRegPage::TLB) return TuiRegPage::BUS;
                        return TuiRegPage::STACK;
                    }
                    if (span.grp == TuiCategoryGroup::Pipeline) {
                        if (page_ == TuiRegPage::PIPELINE)
                            return machine_.runtime_profile.is_cycle_mode() ? TuiRegPage::BPRED
                                                                            : TuiRegPage::PIPELINE;
                        if (page_ == TuiRegPage::BPRED) return TuiRegPage::HAZARD;
                        return TuiRegPage::PIPELINE;
                    }
                    if (span.grp == TuiCategoryGroup::Tools) {
                        return (page_ == TuiRegPage::EXPLAIN) ? TuiRegPage::TRACE
                                                              : TuiRegPage::EXPLAIN;
                    }
                }
                return get_default_page_for_group(span.grp,
                                                  machine_.runtime_profile.is_cycle_mode());
            }
        }
        return std::nullopt;
    }

    if (row == 1) {
        TuiCategoryGroup const grp = get_category_group(page_);
        struct SubTab {
            TuiRegPage page;
            std::string name;
        };
        std::vector<SubTab> tabs;

        switch (grp) {
            case TuiCategoryGroup::Regs: {
                tabs.push_back({.page = TuiRegPage::GPR, .name = "GPR"});
                bool has_f = (current_cpu().state().misa & (1ULL << ('f' - 'a'))) != 0;
                bool has_d = (current_cpu().state().misa & (1ULL << ('d' - 'a'))) != 0;
                bool has_v = (current_cpu().state().misa & (1ULL << ('v' - 'a'))) != 0;
                if (has_f || has_d) {
                    tabs.push_back({.page = TuiRegPage::FPR, .name = "FPR"});
                }
                if (has_v) {
                    tabs.push_back({.page = TuiRegPage::VEC, .name = "VEC"});
                }
                break;
            }
            case TuiCategoryGroup::Memory: {
                tabs.push_back({.page = TuiRegPage::STACK, .name = "Stack"});
                if (machine_.runtime_profile.is_cycle_mode()) {
                    std::string cache_name = (cache_inspect_type_ == 0) ? "Cache:IC" : "Cache:DC";
                    tabs.push_back({.page = TuiRegPage::CACHE, .name = cache_name});
                }
                tabs.push_back({.page = TuiRegPage::TLB, .name = "TLB"});
                tabs.push_back({.page = TuiRegPage::BUS, .name = "Bus"});
                break;
            }
            case TuiCategoryGroup::Pipeline: {
                tabs.push_back({.page = TuiRegPage::PIPELINE, .name = "Pipe"});
                if (machine_.runtime_profile.is_cycle_mode()) {
                    tabs.push_back({.page = TuiRegPage::BPRED, .name = "BPred"});
                    tabs.push_back({.page = TuiRegPage::HAZARD, .name = "Hazard"});
                }
                break;
            }
            case TuiCategoryGroup::Tools: {
                tabs.push_back({.page = TuiRegPage::EXPLAIN, .name = "Explain"});
                tabs.push_back({.page = TuiRegPage::TRACE, .name = "Trace"});
                break;
            }
        }

        std::vector<TabSlot> slots;
        slots.reserve(tabs.size());
        for (const auto& t : tabs) {
            slots.push_back({.text = "",
                             .display_width = static_cast<int>(t.name.length()) + 2,
                             .grp = grp,
                             .page = t.page});
        }

        auto const [_, spans] = layout_tabs_equally(slots, width);
        for (const auto& span : spans) {
            if (col >= span.start_col && col < span.start_col + span.width) {
                return span.page;
            }
        }
    }

    return std::nullopt;
}

auto LeftPane::get_tab_at_col(int col) const -> std::optional<TuiRegPage> {
    return get_tab_at(1, col);
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
    int const total = static_cast<int>(log_lines_.size());
    int const max_entries = num_rows - 1;
    if (row_idx == 0) {
        if (total > max_entries) {
            if (log_scroll_offset_ > 0) {
                const int more_above = std::max(0, total - max_entries - log_scroll_offset_);
                const int more_below = log_scroll_offset_;
                if (more_above > 0) {
                    return section_line(std::format("Log (▲ {} above · ▼ {} below · click to jump)",
                                                    more_above, more_below),
                                        width);
                }
                return section_line(std::format("Log (▼ {} below · click to jump)", more_below),
                                    width);
            }
            const int more_above = total - max_entries;
            return section_line(std::format("Log (▲ {} more in buffer)", more_above), width);
        }
        return section_line("Log", width);
    }
    if (log_lines_.empty()) {
        return format_to_width("", width);
    }
    int const log_idx = total - max_entries - log_scroll_offset_ + (row_idx - 1);
    if (log_idx < 0 || log_idx >= total) {
        return format_to_width("", width);
    }
    // Log lines come from vt_log_ with their own content — prepend space to align with pane section
    // text.
    return format_to_width(" " + log_lines_.at(static_cast<std::size_t>(log_idx)), width);
}

auto LeftPane::render_guidance_row(int row_idx, int width) -> std::string {
    auto const guidance = guidance_for_page(page_, machine_.runtime_profile.is_cycle_mode());
    switch (row_idx) {
        case 0:
            return section_line("Learn · " + std::string(guidance.title), width);
        case 1:
            return format_to_width(" Meaning: " + std::string(guidance.meaning), width);
        case 2:
            return format_to_width(" Connect: " + std::string(guidance.relationship), width);
        case 3: {
            auto const& binding = Keybindings::get(guidance.next_action);
            return format_to_width(
                " Next: " + binding.key_display + " " + std::string(guidance.next_hint), width);
        }
        default:
            return format_to_width("", width);
    }
}

auto LeftPane::render_row(int row_idx, int width) -> std::string {
    last_width_ = width;

    if (!machine_.runtime_profile.is_cycle_mode()) {
        if (page_ == TuiRegPage::CACHE) page_ = TuiRegPage::STACK;
        if (page_ == TuiRegPage::BPRED || page_ == TuiRegPage::HAZARD) page_ = TuiRegPage::PIPELINE;
    }

    if (row_idx == 0) {
        return render_tab_bar_tier1(width);
    }
    if (row_idx == 1) {
        return render_tab_bar_tier2(width);
    }

    constexpr int kGuidanceHeight = 4;
    constexpr int kLogAreaHeight = 6;
    bool const show_guidance = should_show_guidance(paused_, learn_enabled_, visible_rows_);
    int const guidance_start = visible_rows_ - kLogAreaHeight - kGuidanceHeight;
    if (page_ != TuiRegPage::EXPLAIN && page_ != TuiRegPage::TRACE) {
        if (show_guidance && row_idx >= guidance_start &&
            row_idx < guidance_start + kGuidanceHeight) {
            return render_guidance_row(row_idx - guidance_start, width);
        }
        if (visible_rows_ >= 15 && row_idx >= visible_rows_ - kLogAreaHeight) {
            int log_row_idx = row_idx - (visible_rows_ - kLogAreaHeight);
            return render_log_bottom_row(log_row_idx, kLogAreaHeight, width);
        }
    }

    int const content_row_idx = row_idx - 2;
    int const logical_row = content_row_idx + scroll_offset_;

    int const total_logical_rows =
        (page_ == TuiRegPage::EXPLAIN) ? static_cast<int>(get_explain_rows(width).size())
        : (page_ == TuiRegPage::TRACE) ? static_cast<int>(trace_buffer_ ? trace_buffer_->size() : 0)
                                       : get_total_rows(width);

    int const max_content_rows = get_visible_content_rows();

    if (scroll_offset_ > 0 && content_row_idx == 0) {
        return format_to_width(std::format(" {}▲ [{} more lines above - scroll up]\033[0m",
                                           kThemeMuted, scroll_offset_),
                               width);
    }
    if (max_content_rows > 1 && scroll_offset_ + max_content_rows < total_logical_rows &&
        content_row_idx == max_content_rows - 1) {
        int remaining = total_logical_rows - (scroll_offset_ + max_content_rows);
        return format_to_width(
            std::format(" {}▼ [{} more lines below - scroll down]\033[0m", kThemeMuted, remaining),
            width);
    }

    if (page_ == TuiRegPage::TRACE) {
        return render_trace_row(logical_row, width);
    }

    if (page_ == TuiRegPage::EXPLAIN) {
        auto explain_rows = get_explain_rows(width);
        if (logical_row >= total_logical_rows || logical_row < 0) {
            return format_to_width("", width);
        }
        return explain_rows.at(static_cast<std::size_t>(logical_row));
    }

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

    constexpr int kStackCanvasWidth = 104;
    bool const pan_stack_row =
        supports_horizontal_scroll() && logical_row >= 1 && logical_row <= 13;
    int const render_width = pan_stack_row ? std::max(width, kStackCanvasWidth) : width;
    auto finish_row = [&](std::string res) {
        if (page_ != TuiRegPage::PIPELINE && page_ != TuiRegPage::EXPLAIN) {
            res = style_inline_separators(std::move(res));
        }
        if (pan_stack_row) {
            int const viewport_width = std::max(1, width - 2);
            int const max_scroll = std::max(0, kStackCanvasWidth - viewport_width);
            std::string const left_marker = horizontal_scroll_offset_ > 0 ? "◀" : " ";
            std::string const right_marker = horizontal_scroll_offset_ < max_scroll ? "▶" : " ";
            res = std::format("{}{}\033[0m{}{}{}\033[0m", kThemeMuted, left_marker,
                              framework::crop_columns(res, horizontal_scroll_offset_,
                                                      viewport_width),
                              kThemeMuted, right_marker);
            res = format_to_width(res, width);
        }
        return res;
    };

    if (logical_row <= max_active_row) {
        std::string res = finish_row(get_row_uncached(logical_row, render_width));
        if (static_cast<std::size_t>(logical_row) < cached_left_rows_.size()) {
            cached_left_rows_.at(static_cast<std::size_t>(logical_row)) = res;
        }
        return res;
    }

    return finish_row(get_row_uncached(logical_row, render_width));
}

void LeftPane::update_cache() {
    auto& st = current_cpu().state();
    for (int i = 0; i < 32; ++i) {
        cached_gpr_.at(static_cast<std::size_t>(i)) = st.regs.read(static_cast<RegId>(i));
        cached_fpr_.at(static_cast<std::size_t>(i)) = st.regs.read_fp(static_cast<RegId>(i));
        cached_vec_.at(static_cast<std::size_t>(i)) = st.regs.read_vector(static_cast<RegId>(i));
    }
}

auto LeftPane::get_visible_content_rows() const -> int {
    constexpr int kLogAreaHeight = 6;
    constexpr int kGuidanceHeight = 4;
    bool const show_guidance = should_show_guidance(paused_, learn_enabled_, visible_rows_);
    int max_content_rows = visible_rows_ - 2;
    if (page_ != TuiRegPage::EXPLAIN && page_ != TuiRegPage::TRACE) {
        if (visible_rows_ >= 15) {
            max_content_rows -= kLogAreaHeight;
        }
        if (show_guidance) {
            max_content_rows -= kGuidanceHeight;
        }
    }
    return std::max(1, max_content_rows);
}

void LeftPane::scroll(int lines) {
    int w = last_width_ > 0 ? last_width_ : 60;
    int const total_logical_rows =
        (page_ == TuiRegPage::EXPLAIN) ? static_cast<int>(get_explain_rows(w).size())
        : (page_ == TuiRegPage::TRACE) ? static_cast<int>(trace_buffer_ ? trace_buffer_->size() : 0)
                                       : get_total_rows(w);
    int const content_rows = get_visible_content_rows();
    int const max_scroll = std::max(0, total_logical_rows - content_rows);
    scroll_offset_ = std::clamp(scroll_offset_ + lines, 0, max_scroll);
}

void LeftPane::scroll_horizontal(int columns) {
    if (!supports_horizontal_scroll()) return;
    constexpr int kStackCanvasWidth = 104;
    int const viewport_width = last_width_ > 0 ? last_width_ : 60;
    int const max_scroll = std::max(0, kStackCanvasWidth - std::max(1, viewport_width - 2));
    horizontal_scroll_offset_ =
        std::clamp(horizontal_scroll_offset_ + columns, 0, max_scroll);
}

auto LeftPane::supports_horizontal_scroll() const -> bool {
    if (page_ != TuiRegPage::STACK) return false;
    auto const& cpu = current_cpu();
    Register const sp = cpu.state().regs.read(RegId::Sp);
    if (sp == 0) return false;
    auto const physical = translate_safe(cpu, sp);
    return physical.has_value() && simrv::memory::is_dram_addr(*physical);
}

void LeftPane::scroll_log(int lines) {
    constexpr int kLogAreaHeight = 6;
    int const total = static_cast<int>(log_lines_.size());
    int const max_entries = kLogAreaHeight - 1;
    int const max_scroll = std::max(0, total - max_entries);
    log_scroll_offset_ = std::clamp(log_scroll_offset_ + lines, 0, max_scroll);
}

auto LeftPane::get_text_in_range(int start_row, int start_col, int end_row, int end_col, int width)
    -> std::string {
    if (start_row > end_row || (start_row == end_row && start_col > end_col)) {
        std::swap(start_row, end_row);
        std::swap(start_col, end_col);
    }
    std::string res;
    for (int r = start_row; r <= end_row; ++r) {
        std::string raw_row = render_row(r, width);
        // Strip ANSI escape sequences to get plain text
        std::string plain;
        bool in_esc = false;
        for (char c : raw_row) {
            if (c == '\033')
                in_esc = true;
            else if (in_esc) {
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == 'm') in_esc = false;
            } else {
                plain += c;
            }
        }
        int col_from = (r == start_row) ? start_col : 0;
        int col_to = (r == end_row) ? end_col : static_cast<int>(plain.size()) - 1;
        col_from = std::clamp(col_from, 0, static_cast<int>(plain.size()) - 1);
        col_to = std::clamp(col_to, 0, static_cast<int>(plain.size()) - 1);
        std::string line = (col_from <= col_to && col_from < static_cast<int>(plain.size()))
                               ? plain.substr(col_from, col_to - col_from + 1)
                               : "";
        while (!line.empty() && line.back() == ' ') {
            line.pop_back();
        }
        res += line;
        if (r < end_row) {
            res += "\n";
        }
    }
    return res;
}

}  // namespace simrv::tui
