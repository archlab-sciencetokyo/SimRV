/**
 * @file StatusBar.cpp
 * @brief Implements StatusBar widget rendering.
 */
#include "simrv/tui/panels/StatusBar.hpp"

#include <format>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/tui/TuiKeybindings.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/util/FormatUtil.hpp"

namespace simrv::tui {

StatusBar::StatusBar(simrv::core::Machine& machine) : machine_(machine) {}

void StatusBar::update_kips(uint64_t current_kips) { kips_ = current_kips; }

auto StatusBar::is_pos_on_status_badge(int x, int width) const -> bool {
    std::string binary_name = machine_.s_fn_memimg;
    auto last_slash = binary_name.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        binary_name = binary_name.substr(last_slash + 1);
    }
    if (binary_name.empty()) {
        binary_name = "application";
    }
    std::string mode_str = machine_.s_appmode ? "Application" : "OS/RTOS";
    int target_width = layout_ == TuiLayout::Split ? left_width_ : width - 2;
    std::string prefix;
    if (target_width < 35) {
        prefix = " SimRV | ";
    } else if (target_width < 50) {
        prefix = std::format(" SimRV [{}] | ", binary_name);
    } else {
        prefix = std::format(" SimRV [{}] ({}) | ", binary_name, mode_str);
    }

    int prefix_len = get_display_width(prefix);
    std::string status_text = paused_ ? " PAUSED " : " RUNNING ";
    if (!status_override_.empty()) {
        status_text = " TRAPPED ";
    }
    int badge_len = static_cast<int>(status_text.length());

    int x_start = 2 + prefix_len;  // 1-indexed column + 1 border char ║
    int x_end = x_start + badge_len - 1;

    return (x >= x_start && x <= x_end);
}

auto StatusBar::is_pos_on_right_panel_mode(int x) const -> bool {
    if (layout_ != TuiLayout::Split && layout_ != TuiLayout::FullRight) {
        return false;
    }

    int const right_x0 = (layout_ == TuiLayout::Split) ? (left_width_ + 3) : 2;
    int const target_right_w = (layout_ == TuiLayout::Split) ? right_width_ : 100;
    std::string mode_label;
    switch (right_panel_mode_) {
        case TuiRightPanelMode::Terminal:
            if (target_right_w < 45) {
                mode_label = "Term";
            } else {
                mode_label = trace_enabled_ ? "Terminal [Trace ON]" : "Terminal";
            }
            break;
        case TuiRightPanelMode::Display:
        default:
            mode_label = "Display";
            break;
    }
    int const mode_len = 2 + static_cast<int>(mode_label.length());  // "[" + mode_label + "]"
    int const x_end = right_x0 + mode_len - 1;

    return (x >= right_x0 && x <= x_end);
}

auto StatusBar::is_pos_on_right_panel_attached(int x) const -> bool {
    if (right_panel_mode_ != TuiRightPanelMode::Terminal) {
        return false;
    }
    if (layout_ != TuiLayout::Split && layout_ != TuiLayout::FullRight) {
        return false;
    }

    int const right_x0 = (layout_ == TuiLayout::Split) ? (left_width_ + 3) : 2;
    int const target_right_w = (layout_ == TuiLayout::Split) ? right_width_ : 100;
    std::string const term_title =
        (target_right_w < 45) ? "Term" : (trace_enabled_ ? "Terminal [Trace ON]" : "Terminal");
    int const mode_len = 2 + static_cast<int>(term_title.length());

    int const badge_start = right_x0 + mode_len + 1;
    int const badge_len = (target_right_w < 45) ? 3 : 8;
    int const badge_end = badge_start + badge_len - 1;

    return (x >= badge_start && x <= badge_end);
}

auto StatusBar::get_header_action_at_col(int col, int terminal_width) const -> HeaderHitResult {
    if (col < 1 || terminal_width < 2) return {};

    // Check Left Pane header region
    if (layout_ == TuiLayout::Split) {
        if (col <= left_width_ + 1) {
            if (is_pos_on_status_badge(col, terminal_width)) {
                return {.action = HeaderAction::RunPause};
            }
            // Check mode string click
            std::string binary_name = machine_.s_fn_memimg;
            auto last_slash = binary_name.find_last_of("/\\");
            if (last_slash != std::string::npos) binary_name = binary_name.substr(last_slash + 1);
            if (binary_name.empty()) binary_name = "application";
            int bin_start = 2 + get_display_width(" SimRV [");
            int bin_end = bin_start + get_display_width(binary_name) + 1;
            if (col >= bin_start && col <= bin_end + 15) {
                return {.action = HeaderAction::ToggleMode};
            }
            return {};
        }
    }

    // Right Pane header region
    int const right_x0 = (layout_ == TuiLayout::Split) ? (left_width_ + 3) : 2;
    int const target_right_w = (layout_ == TuiLayout::Split) ? right_width_ : (terminal_width - 2);

    if (is_pos_on_right_panel_mode(col)) {
        return {.action = HeaderAction::TogglePanelMode};
    }
    if (is_pos_on_right_panel_attached(col)) {
        return {.action = HeaderAction::ToggleAttached};
    }

    // Check Quick Action icons at far right: [CFG] [ ? ] [THM] [PWR] (or [RST] in ANSI)
    int const action_w = 24;
    int const action_bar_start = right_x0 + target_right_w - action_w;
    if (col >= action_bar_start && col < right_x0 + target_right_w) {
        int rel = col - action_bar_start;
        // "[CFG] [ ? ] [THM] [PWR] "
        if (rel >= 0 && rel < 5) return {.action = HeaderAction::OpenSettings};
        if (rel >= 6 && rel < 11) return {.action = HeaderAction::OpenGlossary};
        if (rel >= 12 && rel < 17) return {.action = HeaderAction::ToggleTheme};
        if (rel >= 18 && rel < 23) return {.action = HeaderAction::Reboot};
    }

    // Check Speed badge or Hart pills in middle region
    if (machine_.num_harts() > 1) {
        for (size_t h = 0; h < machine_.num_harts(); ++h) {
            int h_start = right_x0 + 15 + static_cast<int>(h * 8);
            if (col >= h_start && col <= h_start + 7) {
                return {.action = HeaderAction::SelectHart, .hart_index = h};
            }
        }
    }

    return {};
}

enum class FooterCategory : uint8_t { Exec, Debug, Inspect, Nav, Separator, Spacer };

enum class FooterPriority : uint8_t { Core, Extended };

struct FooterEntry {
    const char* text;
    std::optional<TuiFooterAction> action;
    FooterCategory category = FooterCategory::Spacer;
    FooterPriority priority = FooterPriority::Core;
};

static const auto paused_row1_entries = std::to_array<FooterEntry>({
    {.text = "EXEC: ",
     .action = std::nullopt,
     .category = FooterCategory::Exec,
     .priority = FooterPriority::Core},
    {.text = "[s] Step",
     .action = TuiFooterAction::Step,
     .category = FooterCategory::Exec,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[b] Back",
     .action = TuiFooterAction::StepBack,
     .category = FooterCategory::Exec,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[c] Run",
     .action = TuiFooterAction::RunPause,
     .category = FooterCategory::Exec,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[f] Speed",
     .action = TuiFooterAction::SetSpeed,
     .category = FooterCategory::Exec,
     .priority = FooterPriority::Core},
    {.text = "  │  ",
     .action = std::nullopt,
     .category = FooterCategory::Separator,
     .priority = FooterPriority::Core},
    {.text = "DEBUG: ",
     .action = std::nullopt,
     .category = FooterCategory::Debug,
     .priority = FooterPriority::Core},
    {.text = "[:] BP",
     .action = TuiFooterAction::SetBreakpoint,
     .category = FooterCategory::Debug,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Extended},
    {.text = "[w] WP",
     .action = TuiFooterAction::SetWatchpoint,
     .category = FooterCategory::Debug,
     .priority = FooterPriority::Extended},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Extended},
    {.text = "[k] Toggle",
     .action = TuiFooterAction::TogglePcBreakpoint,
     .category = FooterCategory::Debug,
     .priority = FooterPriority::Extended},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Extended},
    {.text = "[m] List",
     .action = TuiFooterAction::ManageBreakpoints,
     .category = FooterCategory::Debug,
     .priority = FooterPriority::Extended},
});

static const auto paused_row2_entries = std::to_array<FooterEntry>({
    {.text = "INSPECT: ",
     .action = std::nullopt,
     .category = FooterCategory::Inspect,
     .priority = FooterPriority::Core},
    {.text = "[i] Mem",
     .action = TuiFooterAction::InspectMem,
     .category = FooterCategory::Inspect,
     .priority = FooterPriority::Core},
    {.text = "  │  ",
     .action = std::nullopt,
     .category = FooterCategory::Separator,
     .priority = FooterPriority::Core},
    {.text = "NAV: ",
     .action = std::nullopt,
     .category = FooterCategory::Nav,
     .priority = FooterPriority::Core},
    {.text = "[Tab] Layout",
     .action = TuiFooterAction::CycleLayout,
     .category = FooterCategory::Nav,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[g] Learn",
     .action = TuiFooterAction::ToggleLearn,
     .category = FooterCategory::Nav,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[o] Load",
     .action = TuiFooterAction::LoadBinary,
     .category = FooterCategory::Nav,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[Ctrl-Q] Quit",
     .action = TuiFooterAction::Quit,
     .category = FooterCategory::Nav,
     .priority = FooterPriority::Core},
});

static const auto running_row1_entries = std::to_array<FooterEntry>({
    {.text = "EXEC: ",
     .action = std::nullopt,
     .category = FooterCategory::Exec,
     .priority = FooterPriority::Core},
    {.text = "[Ctrl-P] Pause",
     .action = TuiFooterAction::RunPause,
     .category = FooterCategory::Exec,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[f] Speed",
     .action = TuiFooterAction::SetSpeed,
     .category = FooterCategory::Exec,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Extended},
    {.text = "[v] Trace",
     .action = TuiFooterAction::ToggleTrace,
     .category = FooterCategory::Exec,
     .priority = FooterPriority::Extended},
    {.text = "  │  ",
     .action = std::nullopt,
     .category = FooterCategory::Separator,
     .priority = FooterPriority::Core},
    {.text = "DEBUG: ",
     .action = std::nullopt,
     .category = FooterCategory::Debug,
     .priority = FooterPriority::Core},
    {.text = "[:] BP",
     .action = TuiFooterAction::SetBreakpoint,
     .category = FooterCategory::Debug,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Extended},
    {.text = "[w] WP",
     .action = TuiFooterAction::SetWatchpoint,
     .category = FooterCategory::Debug,
     .priority = FooterPriority::Extended},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Extended},
    {.text = "[k] Toggle",
     .action = TuiFooterAction::TogglePcBreakpoint,
     .category = FooterCategory::Debug,
     .priority = FooterPriority::Extended},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Extended},
    {.text = "[m] List",
     .action = TuiFooterAction::ManageBreakpoints,
     .category = FooterCategory::Debug,
     .priority = FooterPriority::Extended},
});

static const auto running_row2_entries = std::to_array<FooterEntry>({
    {.text = "INSPECT: ",
     .action = std::nullopt,
     .category = FooterCategory::Inspect,
     .priority = FooterPriority::Core},
    {.text = "[i] Mem",
     .action = TuiFooterAction::InspectMem,
     .category = FooterCategory::Inspect,
     .priority = FooterPriority::Core},
    {.text = "  │  ",
     .action = std::nullopt,
     .category = FooterCategory::Separator,
     .priority = FooterPriority::Core},
    {.text = "NAV: ",
     .action = std::nullopt,
     .category = FooterCategory::Nav,
     .priority = FooterPriority::Core},
    {.text = "[Tab] Layout",
     .action = TuiFooterAction::CycleLayout,
     .category = FooterCategory::Nav,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[g] Learn",
     .action = TuiFooterAction::ToggleLearn,
     .category = FooterCategory::Nav,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[o] Load",
     .action = TuiFooterAction::LoadBinary,
     .category = FooterCategory::Nav,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[Ctrl-Q] Quit",
     .action = TuiFooterAction::Quit,
     .category = FooterCategory::Nav,
     .priority = FooterPriority::Core},
});

namespace {

[[nodiscard]] auto footer_entry_text(const FooterEntry& entry) -> std::string {
    if (entry.action.has_value()) {
        return Keybindings::get_footer_text(key_action_for_footer(*entry.action));
    }
    return entry.text;
}

auto filter_footer_entries(std::span<const FooterEntry> entries, int inner_w, bool is_debug_mode,
                           bool is_smp) -> std::vector<FooterEntry> {
    std::vector<FooterEntry> raw;
    raw.reserve(entries.size());
    for (const auto& e : entries) {
        if (!is_smp && e.action == TuiFooterAction::SwitchHart) {
            continue;
        }
        if (!is_debug_mode) {
            if (e.category == FooterCategory::Debug) continue;
            if (e.category == FooterCategory::Exec && e.action != TuiFooterAction::RunPause &&
                e.action != TuiFooterAction::SetSpeed && e.action != TuiFooterAction::Step) {
                continue;
            }
        }
        raw.push_back(e);
    }

    auto cleanup = [](const std::vector<FooterEntry>& list) -> std::vector<FooterEntry> {
        std::vector<FooterEntry> out;
        out.reserve(list.size());
        for (const auto& e : list) {
            if (e.category == FooterCategory::Spacer || e.category == FooterCategory::Separator) {
                if (out.empty()) continue;
                if (out.back().category == FooterCategory::Spacer ||
                    out.back().category == FooterCategory::Separator) {
                    continue;
                }
            }
            out.push_back(e);
        }
        while (!out.empty() && (out.back().category == FooterCategory::Spacer ||
                                out.back().category == FooterCategory::Separator)) {
            out.pop_back();
        }
        return out;
    };

    auto measure = [](const std::vector<FooterEntry>& list) -> int {
        int len = 0;
        for (const auto& e : list) {
            len += get_display_width(footer_entry_text(e));
        }
        return len;
    };

    auto full = cleanup(raw);
    if (measure(full) <= inner_w) {
        return full;
    }

    // When full catalog exceeds width, start with Core and greedily fit Extended items
    std::vector<FooterEntry> result;
    for (const auto& e : raw) {
        if (e.priority == FooterPriority::Core) {
            result.push_back(e);
        }
    }
    result = cleanup(result);

    for (const auto& e : raw) {
        if (e.priority != FooterPriority::Core && e.action.has_value()) {
            std::vector<FooterEntry> candidate = result;
            candidate.push_back(
                {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer});
            candidate.push_back(e);
            auto candidate_cleaned = cleanup(candidate);
            if (measure(candidate_cleaned) <= inner_w) {
                result = candidate_cleaned;
            }
        }
    }

    // If even Core items exceed inner width on very small terminals, prune from right
    while (!result.empty() && measure(result) > inner_w) {
        result.pop_back();
        result = cleanup(result);
    }

    return result;
}

}  // namespace

auto process_footer_row(std::span<const FooterEntry> entries, int inner_w, bool is_debug_mode,
                        bool is_smp, std::optional<int> hit_col = std::nullopt)
    -> std::pair<std::string, std::optional<TuiFooterAction>> {
    const auto style = get_active_theme_style();
    const bool is_ansi = (style == TuiThemeStyle::ClassicAnsi);

    std::vector<FooterEntry> active_entries =
        filter_footer_entries(entries, inner_w, is_debug_mode, is_smp);

    int content_len = 0;
    for (const auto& e : active_entries) {
        std::string text = footer_entry_text(e);
        if (e.category == FooterCategory::Separator) {
            text = is_ansi ? "  |  " : "  │  ";
        }
        content_len += get_display_width(text);
    }
    int pad = inner_w - content_len;
    int left_pad = (pad > 0) ? (pad / 2) : 0;
    int right_pad = (pad > 0) ? (pad - left_pad) : 0;

    std::string row_str;
    if (pad > 0) {
        row_str.append(static_cast<std::size_t>(left_pad), ' ');
    }

    std::optional<TuiFooterAction> hit_action = std::nullopt;
    int current_col = left_pad;

    for (const auto& e : active_entries) {
        std::string text = footer_entry_text(e);
        if (e.category == FooterCategory::Separator) {
            text = is_ansi ? "  |  " : "  │  ";
        }
        int item_len = get_display_width(text);
        if (hit_col.has_value() && e.action.has_value()) {
            if (*hit_col >= current_col && *hit_col < current_col + item_len) {
                hit_action = e.action;
            }
        }

        const char* color_tag = nullptr;
        switch (e.category) {
            case FooterCategory::Exec:
                color_tag = kThemePeach;
                break;
            case FooterCategory::Debug:
                color_tag = kThemeMint;
                break;
            case FooterCategory::Inspect:
                color_tag = kThemeSky;
                break;
            case FooterCategory::Nav:
                color_tag = kThemePink;
                break;
            case FooterCategory::Separator:
                color_tag = kThemeBorder;
                break;
            case FooterCategory::Spacer:
                color_tag = nullptr;
                break;
        }

        if (color_tag != nullptr) {
            std::string_view text_sv{text};
            std::size_t close_bracket = text_sv.find(']');
            if (text_sv.starts_with('[') && close_bracket != std::string_view::npos) {
                row_str += "\033[1m";
                row_str += color_tag;
                row_str += text_sv.substr(0, close_bracket + 1);
                row_str += "\033[22m";
                row_str += text_sv.substr(close_bracket + 1);
            } else if (text_sv.ends_with(": ")) {
                row_str += "\033[1m";
                row_str += color_tag;
                row_str += text;
            } else {
                row_str += color_tag;
                row_str += text;
            }
            row_str += "\033[0m";
        } else {
            row_str += text;
        }
        current_col += item_len;
    }

    if (pad > 0) {
        row_str.append(static_cast<std::size_t>(right_pad), ' ');
    }

    if (get_display_width(row_str) > inner_w) {
        row_str = format_to_width(row_str, inner_w);
    } else if (get_display_width(row_str) < inner_w) {
        row_str += std::string(static_cast<std::size_t>(inner_w - get_display_width(row_str)), ' ');
    }
    return {row_str, hit_action};
}

auto StatusBar::get_footer_action_at_col(int col, int row_idx, int terminal_width) const
    -> std::optional<TuiFooterAction> {
    if (col < 0 || terminal_width < 2) return std::nullopt;

    bool is_dbg = machine_.s_debug_mode;
    bool is_smp = machine_.num_harts() > 1;
    if (paused_) {
        if (row_idx == 0)
            return process_footer_row(paused_row1_entries, terminal_width - 2, is_dbg, is_smp, col)
                .second;
        if (row_idx == 1)
            return process_footer_row(paused_row2_entries, terminal_width - 2, is_dbg, is_smp, col)
                .second;
    } else {
        if (row_idx == 0)
            return process_footer_row(running_row1_entries, terminal_width - 2, is_dbg, is_smp, col)
                .second;
        if (row_idx == 1)
            return process_footer_row(running_row2_entries, terminal_width - 2, is_dbg, is_smp, col)
                .second;
    }

    return std::nullopt;
}

auto StatusBar::render_row(int row_idx, int width) -> std::string {
    if (row_idx == 0) {
        // Header
        std::string binary_name = machine_.s_fn_memimg;
        auto last_slash = binary_name.find_last_of("/\\");
        if (last_slash != std::string::npos) {
            binary_name = binary_name.substr(last_slash + 1);
        }
        if (binary_name.empty()) {
            binary_name = "application";
        }
        std::string mode_str;
        if (machine_.s_cycle_accurate) {
            const auto ptype = machine_.cpu.pipeline_sim.config.pipeline_type;
            if (ptype == simrv::pipeline::PipelineType::ThreeStage) {
                mode_str = machine_.s_appmode ? "App (3-Stage)" : "OS (3-Stage)";
            } else if (ptype == simrv::pipeline::PipelineType::DualIssue) {
                mode_str = machine_.s_appmode ? "App (Dual-Issue)" : "OS (Dual-Issue)";
            } else {
                mode_str = machine_.s_appmode ? "App (5-Stage)" : "OS (5-Stage)";
            }
        } else {
            mode_str = machine_.s_appmode ? "Application" : "OS/RTOS";
        }
        std::string status_badge = status_override_;
        if (status_badge.empty()) {
            bool use_ansi = (get_tui_theme() == TuiTheme::Adaptive ||
                             get_tui_theme() == TuiTheme::HighContrast ||
                             get_tui_theme() == TuiTheme::ClassicAnsi ||
                             get_active_theme_style() == TuiThemeStyle::ClassicAnsi);
            const auto st = machine_.execution_state();
            if (machine_.is_shutdown_ || st == simrv::core::ExecutionState::Stopped) {
                status_badge = use_ansi ? "\033[41;37m SHUTDOWN \033[0m"
                                        : "\033[48;5;196m\033[38;5;231m SHUTDOWN \033[0m";
            } else if (st == simrv::core::ExecutionState::Stepping) {
                status_badge = use_ansi ? "\033[46;30m STEPPING \033[0m"
                                        : "\033[48;5;117m\033[38;5;232m STEPPING \033[0m";
            } else if (paused_ || st == simrv::core::ExecutionState::Paused) {
                status_badge = use_ansi ? "\033[43;30m PAUSED \033[0m"
                                        : "\033[48;5;223m\033[38;5;232m PAUSED \033[0m";
            } else {
                status_badge = use_ansi ? "\033[42;30m RUNNING \033[0m"
                                        : "\033[48;5;121m\033[38;5;232m RUNNING \033[0m";
            }
        } else if (status_badge == "\033[1;38;5;234;48;5;210m TRAPPED \033[0m" &&
                   (get_tui_theme() == TuiTheme::HighContrast ||
                    get_tui_theme() == TuiTheme::Adaptive ||
                    get_tui_theme() == TuiTheme::ClassicAnsi ||
                    get_active_theme_style() == TuiThemeStyle::ClassicAnsi)) {
            status_badge = "\033[1;41;37m TRAPPED \033[0m";
        }
        int target_width = layout_ == TuiLayout::Split ? left_width_ : width - 2;
        std::string left_render;
        if (target_width < 35) {
            left_render = std::format(" SimRV | {}", status_badge);
        } else if (target_width < 55) {
            left_render = std::format(" SimRV [{}] | {}", binary_name, status_badge);
        } else {
            left_render = std::format(" SimRV [{}] ({}) | {}", binary_name, mode_str, status_badge);
        }

        if (get_display_width(left_render) > target_width) {
            left_render = format_to_width(left_render, target_width);
        } else if (get_display_width(left_render) < target_width) {
            left_render += std::string(
                static_cast<std::size_t>(target_width - get_display_width(left_render)), ' ');
        }

        int target_right_width = layout_ == TuiLayout::Split ? right_width_ : width - 2;
        // Build Left Mode Prefix
        std::string mode_prefix;
        switch (right_panel_mode_) {
            case TuiRightPanelMode::Terminal: {
                const bool term_focused = machine_.tui && machine_.tui->is_terminal_attached();
                if (target_right_width < 70) {
                    mode_prefix = std::format(
                        " {}[Term]\033[0m{}", kThemeSky,
                        term_focused ? std::format(" \033[1m{}ATT\033[0m", kThemeMint)
                                     : std::format(" \033[1m{}DET\033[0m", kThemeMuted));
                } else {
                    std::string const focus_badge =
                        term_focused ? std::format(" \033[1m{}ATT\033[0m", kThemeMint)
                                     : std::format(" \033[1m{}DET\033[0m", kThemeMuted);
                    std::string const term_title =
                        trace_enabled_ ? "Term [Trace]" : "Terminal";
                    mode_prefix =
                        std::format(" {}[{}]\033[0m{}", kThemeSky, term_title, focus_badge);
                }
                break;
            }
            case TuiRightPanelMode::Display:
            default:
                mode_prefix = std::format(" {}[Display]\033[0m", kThemeSky);
                break;
        }

        // Build Right Quick Action Icons (Always keep all 4 buttons visible)
        const auto style = get_active_theme_style();
        const bool is_ansi = (style == TuiThemeStyle::ClassicAnsi);
        std::string action_buttons =
            std::format("\033[1m{}[CFG] {}[ ? ] {}[THM] {}[{}]\033[0m ", kThemeSky, kThemeMint,
                        kThemePeach, kThemeCoral, is_ansi ? "RST" : "PWR");

        int const prefix_w = get_display_width(mode_prefix);
        int const action_w = get_display_width(action_buttons);
        int const available_mid_w = target_right_width - prefix_w - action_w - 2;

        std::string mid_text;
        if (scroll_offset_ > 0) {
            mid_text = std::format("═══ SCROLLBACK (-{}) ['c'/'Enter' Live] ═══", scroll_offset_);
            if (get_display_width(mid_text) > available_mid_w) {
                mid_text = std::format("SCROLL (-{})", scroll_offset_);
            }
        } else if (available_mid_w >= 10) {
            const auto cycles = machine_.cpu.clint_mmio.mcycle;
            const auto icount = machine_.cpu.e_icount;
            const double cpi =
                icount == 0 ? 0.0 : static_cast<double>(cycles) / static_cast<double>(icount);
            const double mips = static_cast<double>(kips_) / 1000.0;
            std::string speed_str = (mips >= 1.0)
                                        ? std::format("{:.2f} MIPS", mips)
                                        : std::format("{:.1f} KIPS", static_cast<double>(kips_));

            uint64_t delay = 0;
            if (machine_.tui) {
                delay = machine_.tui->step_delay_us_.load(std::memory_order_relaxed);
            }
            std::string dbg_info;
            if (machine_.num_harts() > 1) {
                const size_t selected = (machine_.tui) ? machine_.tui->selected_hart() : 0;
                dbg_info += std::format("*H{}/{} | ", selected, machine_.num_harts());
            }
            if (delay > 0) {
                double hz = 1000000.0 / static_cast<double>(delay);
                dbg_info += (hz >= 1000.0) ? std::format("Speed: {:.0f}kHz | ", hz / 1000.0)
                                           : std::format("Speed: {:.1f}Hz | ", hz);
            } else if (paused_) {
                dbg_info += "Speed: Max | ";
            }

            if (machine_.s_cycle_accurate) {
                std::string full_stats =
                    std::format("{}Cycles: {} | Insns: {} | CPI: {:.2f}", dbg_info,
                                simrv::util::format_with_commas(cycles),
                                simrv::util::format_with_commas(icount), cpi);
                if (get_display_width(full_stats) <= available_mid_w) {
                    mid_text = full_stats;
                } else {
                    std::string med_stats = std::format("{}C: {} | I: {} | CPI: {:.2f}", dbg_info,
                                                        simrv::util::format_scaled(cycles),
                                                        simrv::util::format_scaled(icount), cpi);
                    if (get_display_width(med_stats) <= available_mid_w) {
                        mid_text = med_stats;
                    } else {
                        std::string short_stats = std::format("{}C: {} | I: {}", dbg_info,
                                                              simrv::util::format_scaled(cycles),
                                                              simrv::util::format_scaled(icount));
                        if (get_display_width(short_stats) <= available_mid_w) {
                            mid_text = short_stats;
                        }
                    }
                }
            } else {
                std::string full_stats =
                    std::format("{}Insns: {} | Speed: {}", dbg_info,
                                simrv::util::format_with_commas(icount), speed_str);
                if (get_display_width(full_stats) <= available_mid_w) {
                    mid_text = full_stats;
                } else {
                    std::string med_stats = std::format(
                        "{}I: {} | {}", dbg_info, simrv::util::format_scaled(icount), speed_str);
                    if (get_display_width(med_stats) <= available_mid_w) {
                        mid_text = med_stats;
                    } else {
                        std::string short_stats =
                            std::format("{}I: {}", dbg_info, simrv::util::format_scaled(icount));
                        if (get_display_width(short_stats) <= available_mid_w) {
                            mid_text = short_stats;
                        }
                    }
                }
            }
        }

        int const mid_w = get_display_width(mid_text);
        int const pad_total = std::max(0, target_right_width - (prefix_w + mid_w + action_w));
        int const mid_pad_left = pad_total / 2;
        int const mid_pad_right = pad_total - mid_pad_left;

        std::string right_render =
            mode_prefix + std::string(static_cast<std::size_t>(mid_pad_left), ' ') + mid_text +
            std::string(static_cast<std::size_t>(mid_pad_right), ' ') + action_buttons;
        if (get_display_width(right_render) > target_right_width) {
            right_render = format_to_width(right_render, target_right_width);
        } else if (get_display_width(right_render) < target_right_width) {
            right_render += std::string(
                static_cast<std::size_t>(target_right_width - get_display_width(right_render)),
                ' ');
        }

        std::string screen;
        if (style == TuiThemeStyle::ClassicAnsi) {
            switch (layout_) {
                case TuiLayout::Split:
                    screen += std::string(kThemeBorder) + "+" +
                              make_repeated_string("-", left_width_) + "+" +
                              make_repeated_string("-", right_width_) + "+\033[0m\n";
                    screen += std::string(kThemeBorder) + "|\033[0m" + left_render + kThemeBorder +
                              "|\033[0m" + right_render + kThemeBorder + "|\033[0m\n";
                    screen += std::string(kThemeBorder) + "+" +
                              make_repeated_string("-", left_width_) + "+" +
                              make_repeated_string("-", right_width_) + "+\033[0m\n";
                    break;
                case TuiLayout::FullRight:
                    screen += std::string(kThemeBorder) + "+" +
                              make_repeated_string("-", width - 2) + "+\033[0m\n";
                    screen += std::string(kThemeBorder) + "|\033[0m" + right_render + kThemeBorder +
                              "|\033[0m\n";
                    screen += std::string(kThemeBorder) + "+" +
                              make_repeated_string("-", width - 2) + "+\033[0m\n";
                    break;
                default:
                    screen += std::string(kThemeBorder) + "+" +
                              make_repeated_string("-", width - 2) + "+\033[0m\n";
                    screen += std::string(kThemeBorder) + "|\033[0m" + left_render + kThemeBorder +
                              "|\033[0m\n";
                    screen += std::string(kThemeBorder) + "+" +
                              make_repeated_string("-", width - 2) + "+\033[0m\n";
                    break;
            }
        } else {
            switch (layout_) {
                case TuiLayout::Split:
                    screen += std::string(kThemeBorder) + "╔" +
                              make_repeated_string("═", left_width_) + "╤" +
                              make_repeated_string("═", right_width_) + "╗\033[0m\n";
                    screen += std::string(kThemeBorder) + "║\033[0m" + left_render + kThemeBorder +
                              "│\033[0m" + right_render + kThemeBorder + "║\033[0m\n";
                    screen += std::string(kThemeBorder) + "╠" +
                              make_repeated_string("═", left_width_) + "╪" +
                              make_repeated_string("═", right_width_) + "╣\033[0m\n";
                    break;
                case TuiLayout::FullRight:
                    screen += std::string(kThemeBorder) + "╔" +
                              make_repeated_string("═", width - 2) + "╗\033[0m\n";
                    screen += std::string(kThemeBorder) + "║\033[0m" + right_render + kThemeBorder +
                              "║\033[0m\n";
                    screen += std::string(kThemeBorder) + "╠" +
                              make_repeated_string("═", width - 2) + "╣\033[0m\n";
                    break;
                default:
                    screen += std::string(kThemeBorder) + "╔" +
                              make_repeated_string("═", width - 2) + "╗\033[0m\n";
                    screen += std::string(kThemeBorder) + "║\033[0m" + left_render + kThemeBorder +
                              "║\033[0m\n";
                    screen += std::string(kThemeBorder) + "╠" +
                              make_repeated_string("═", width - 2) + "╣\033[0m\n";
                    break;
            }
        }
        return screen;
    } else if (row_idx == 1) {
        // 2-Row Footer (Centered & Grouped)
        std::string footer_line1;
        std::string footer_line2;
        bool is_dbg = machine_.s_debug_mode;
        bool is_smp = machine_.num_harts() > 1;
        if (paused_) {
            footer_line1 = process_footer_row(paused_row1_entries, width - 2, is_dbg, is_smp).first;
            footer_line2 = process_footer_row(paused_row2_entries, width - 2, is_dbg, is_smp).first;
        } else {
            footer_line1 =
                process_footer_row(running_row1_entries, width - 2, is_dbg, is_smp).first;
            footer_line2 =
                process_footer_row(running_row2_entries, width - 2, is_dbg, is_smp).first;
        }

        const auto style = get_active_theme_style();
        if (style == TuiThemeStyle::ClassicAnsi) {
            std::string screen =
                std::string(kThemeBorder) + "|\033[0m" + footer_line1 + kThemeBorder + "|\033[0m\n";
            screen +=
                std::string(kThemeBorder) + "|\033[0m" + footer_line2 + kThemeBorder + "|\033[0m\n";
            screen +=
                std::string(kThemeBorder) + "+" + make_repeated_string("-", width - 2) + "+\033[0m";
            return screen;
        } else {
            std::string screen =
                std::string(kThemeBorder) + "║\033[0m" + footer_line1 + kThemeBorder + "║\033[0m\n";
            screen +=
                std::string(kThemeBorder) + "║\033[0m" + footer_line2 + kThemeBorder + "║\033[0m\n";
            screen +=
                std::string(kThemeBorder) + "╚" + make_repeated_string("═", width - 2) + "╝\033[0m";
            return screen;
        }
    }
    return "";
}

}  // namespace simrv::tui
