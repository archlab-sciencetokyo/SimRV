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

namespace simrv::tui {

namespace {

[[nodiscard]] auto use_basic_ansi_badges() -> bool {
    return get_tui_theme() == TuiTheme::Adaptive ||
           get_tui_theme() == TuiTheme::HighContrast ||
           get_tui_theme() == TuiTheme::ClassicAnsi ||
           get_active_theme_style() == TuiThemeStyle::ClassicAnsi;
}

[[nodiscard]] auto header_badge(std::string_view text, const char* ansi_color,
                                const char* indexed_color) -> std::string {
    return std::format("{} {} \033[0m", use_basic_ansi_badges() ? ansi_color : indexed_color, text);
}

[[nodiscard]] auto panel_mode_label(TuiRightPanelMode mode, int width, bool trace_enabled)
    -> std::string {
    if (mode == TuiRightPanelMode::Display) return width < 45 ? "Disp" : "Display";
    if (width < 45) return "Term";
    return trace_enabled ? "Terminal Trace" : "Terminal";
}

[[nodiscard]] auto speed_control_label(const simrv::core::Machine& machine, uint64_t kips,
                                       bool paused, int width) -> std::string {
    uint64_t const delay =
        machine.tui ? machine.tui->step_delay_us_.load(std::memory_order_relaxed) : 0;
    if (delay > 0) {
        double const hz = 1000000.0 / static_cast<double>(delay);
        if (width < 60) return hz >= 1000.0 ? std::format("{:.0f}kHz", hz / 1000.0)
                                             : std::format("{:.0f}Hz", hz);
        return hz >= 1000.0 ? std::format("SPEED {:.0f}kHz", hz / 1000.0)
                            : std::format("SPEED {:.1f}Hz", hz);
    }
    if (paused) return width < 60 ? "MAX" : "SPEED MAX";
    double const mips = static_cast<double>(kips) / 1000.0;
    return mips >= 1.0 ? std::format("{:.2f} MIPS", mips)
                       : std::format("{:.1f} KIPS", static_cast<double>(kips));
}

[[nodiscard]] auto format_metric_count(uint64_t value) -> std::string {
    if (value >= 1000000000000ULL) {
        return std::format("{:.2f}T", static_cast<double>(value) / 1000000000000.0);
    }
    if (value >= 1000000000ULL) {
        return std::format("{:.2f}G", static_cast<double>(value) / 1000000000.0);
    }
    if (value >= 1000000ULL) {
        return std::format("{:.2f}M", static_cast<double>(value) / 1000000.0);
    }
    if (value >= 1000ULL) {
        return std::format("{:.2f}K", static_cast<double>(value) / 1000.0);
    }
    return std::to_string(value);
}

[[nodiscard]] auto footer_display_width(std::string_view text) -> int {
    const std::size_t close = text.find(']');
    if (text.starts_with('[') && close != std::string_view::npos) {
        return get_display_width(text) - 2;
    }
    return get_display_width(text);
}

struct LeftHeaderLayout {
    std::string identity;
    std::string mode;
};

[[nodiscard]] auto make_left_header_layout(const simrv::core::Machine& machine, int target_width,
                                           size_t selected_hart) -> LeftHeaderLayout {
    std::string binary_name = machine.s_fn_memimg;
    auto const last_slash = binary_name.find_last_of("/\\");
    if (last_slash != std::string::npos) binary_name = binary_name.substr(last_slash + 1);
    if (binary_name.empty()) binary_name = "application";

    LeftHeaderLayout result{.identity = " SimRV", .mode = {}};
    if (target_width >= 45) result.identity += "  " + binary_name;
    if (target_width < 35) return result;

    const bool cycle_mode = machine.runtime_profile.is_cycle_mode();
    if (target_width < 55) {
        result.mode = cycle_mode ? "CA" : "IA";
        return result;
    }

    std::string const environment = machine.s_appmode ? "App" : "OS";
    if (!cycle_mode) {
        result.mode = environment + " · IA";
        return result;
    }
    auto const type = machine.hart(selected_hart).pipeline_sim.config.pipeline_type;
    result.mode = std::format("{} · CA {}-stage", environment,
                              type == simrv::pipeline::PipelineType::ThreeStage ? 3 : 5);
    return result;
}

}  // namespace

StatusBar::StatusBar(simrv::core::Machine& machine) : machine_(machine) {}

void StatusBar::update_kips(uint64_t current_kips) { kips_ = current_kips; }

auto StatusBar::is_pos_on_status_badge(int x, int width) const -> bool {
    int target_width = layout_ == TuiLayout::Split ? left_width_ : width - 2;
    size_t const selected = machine_.tui ? machine_.tui->selected_hart() : 0;
    auto const header = make_left_header_layout(machine_, target_width, selected);
    std::string status_text = paused_ ? " PAUSED " : " RUNNING ";
    if (!status_override_.empty()) {
        status_text = status_override_;
    }
    int const badge_len = get_display_width(status_text);

    int x_start = 2 + get_display_width(header.identity) + 3;
    if (!header.mode.empty()) x_start += get_display_width(header.mode) + 3;
    int x_end = x_start + badge_len - 1;

    return (x >= x_start && x <= x_end);
}

auto StatusBar::is_pos_on_right_panel_mode(int x) const -> bool {
    if (layout_ != TuiLayout::Split && layout_ != TuiLayout::FullRight) {
        return false;
    }

    int const right_x0 = (layout_ == TuiLayout::Split) ? (left_width_ + 3) : 2;
    int const target_right_w = right_width_ > 0 ? right_width_ : 100;
    std::string const mode_label =
        panel_mode_label(right_panel_mode_, target_right_w, trace_enabled_);
    int const x_start = right_x0 + 1;
    int const x_end = x_start + static_cast<int>(mode_label.length()) + 1;

    return x >= x_start && x <= x_end;
}

auto StatusBar::is_pos_on_right_panel_attached(int x) const -> bool {
    if (right_panel_mode_ != TuiRightPanelMode::Terminal) {
        return false;
    }
    if (layout_ != TuiLayout::Split && layout_ != TuiLayout::FullRight) {
        return false;
    }

    int const right_x0 = (layout_ == TuiLayout::Split) ? (left_width_ + 3) : 2;
    int const target_right_w = right_width_ > 0 ? right_width_ : 100;
    std::string const term_title =
        panel_mode_label(right_panel_mode_, target_right_w, trace_enabled_);
    int const mode_len = static_cast<int>(term_title.length()) + 2;

    int const badge_start = right_x0 + 1 + mode_len + 1;
    const bool attached = machine_.tui && machine_.tui->is_terminal_attached();
    std::string const focus_label =
        target_right_w < 45 ? (attached ? "ON" : "OFF") : (attached ? "ATTACHED" : "DETACHED");
    int const badge_len = static_cast<int>(focus_label.length()) + 2;
    int const badge_end = badge_start + badge_len - 1;

    return (x >= badge_start && x <= badge_end);
}

auto StatusBar::get_header_action_at_col(int col, int terminal_width) const -> HeaderHitResult {
    if (col < 1 || terminal_width < 2) return {};

    // Check Left Pane header region
    if (layout_ == TuiLayout::Split || layout_ == TuiLayout::FullLeft) {
        int const left_edge = layout_ == TuiLayout::Split ? left_width_ + 1 : terminal_width - 1;
        if (col <= left_edge) {
            if (is_pos_on_status_badge(col, terminal_width)) {
                return {.action = HeaderAction::RunPause};
            }
            return {};
        }
    }

    // Right Pane header region
    int const right_x0 = (layout_ == TuiLayout::Split) ? (left_width_ + 3) : 2;
    int const target_right_w = right_width_ > 0 ? right_width_ : terminal_width - 2;
    int cursor = right_x0 + 1;
    auto hit_badge = [col, &cursor](std::string_view label) {
        int const start = cursor;
        int const end = start + static_cast<int>(label.length()) + 1;
        cursor = end + 2;
        return col >= start && col <= end;
    };

    std::string const mode_label =
        panel_mode_label(right_panel_mode_, target_right_w, trace_enabled_);
    if (hit_badge(mode_label)) return {.action = HeaderAction::TogglePanelMode};

    if (right_panel_mode_ == TuiRightPanelMode::Terminal) {
        const bool attached = machine_.tui && machine_.tui->is_terminal_attached();
        std::string const focus_label = target_right_w < 45
                                            ? (attached ? "ON" : "OFF")
                                            : (attached ? "ATTACHED" : "DETACHED");
        if (hit_badge(focus_label)) return {.action = HeaderAction::ToggleAttached};
    }

    if (machine_.num_harts() > 1) {
        size_t const selected = machine_.tui ? machine_.tui->selected_hart() : 0;
        std::string const hart_label =
            std::format("HART {}/{}", selected, machine_.num_harts());
        if (hit_badge(hart_label)) {
            return {.action = HeaderAction::SelectHart,
                    .hart_index = (selected + 1) % machine_.num_harts()};
        }
    }

    std::string const speed_label = speed_control_label(machine_, kips_, paused_, target_right_w);
    if (hit_badge(speed_label)) return {.action = HeaderAction::SetSpeed};

    return {};
}

enum class FooterCategory : uint8_t { Exec, Debug, Config, Inspect, Sys, Separator, Spacer };

enum class FooterPriority : uint8_t { Core, Extended };

struct FooterEntry {
    const char* text;
    std::optional<TuiFooterAction> action;
    FooterCategory category = FooterCategory::Spacer;
    FooterPriority priority = FooterPriority::Core;
};

static const auto paused_row1_entries = std::to_array<FooterEntry>({
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
    {.text = "[:] Breakpoint",
     .action = TuiFooterAction::SetBreakpoint,
     .category = FooterCategory::Debug,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Extended},
    {.text = "[w] Watchpoint",
     .action = TuiFooterAction::SetWatchpoint,
     .category = FooterCategory::Debug,
     .priority = FooterPriority::Extended},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Extended},
    {.text = "[k] Toggle BP",
     .action = TuiFooterAction::TogglePcBreakpoint,
     .category = FooterCategory::Debug,
     .priority = FooterPriority::Extended},
});

static const auto paused_row2_entries = std::to_array<FooterEntry>({
    {.text = "[,] Settings",
     .action = TuiFooterAction::OpenSettings,
     .category = FooterCategory::Config,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[?] Help",
     .action = TuiFooterAction::ToggleHelp,
     .category = FooterCategory::Config,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[t] Theme",
     .action = TuiFooterAction::ToggleTheme,
     .category = FooterCategory::Config,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[d] Debug",
     .action = TuiFooterAction::ToggleDebug,
     .category = FooterCategory::Config,
     .priority = FooterPriority::Core},
    {.text = "  │  ",
     .action = std::nullopt,
     .category = FooterCategory::Separator,
     .priority = FooterPriority::Core},
    {.text = "[i] Inspect",
     .action = TuiFooterAction::InspectMem,
     .category = FooterCategory::Inspect,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[g] Learn",
     .action = TuiFooterAction::ToggleLearn,
     .category = FooterCategory::Inspect,
     .priority = FooterPriority::Core},
    {.text = "  │  ",
     .action = std::nullopt,
     .category = FooterCategory::Separator,
     .priority = FooterPriority::Core},
    {.text = "[Tab] Layout",
     .action = TuiFooterAction::CycleLayout,
     .category = FooterCategory::Sys,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[o] Load",
     .action = TuiFooterAction::LoadBinary,
     .category = FooterCategory::Sys,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[Ctrl-R] Reboot",
     .action = TuiFooterAction::Reboot,
     .category = FooterCategory::Sys,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Core},
    {.text = "[q] Quit",
     .action = TuiFooterAction::Quit,
     .category = FooterCategory::Sys,
     .priority = FooterPriority::Core},
});

static const auto running_row1_entries = std::to_array<FooterEntry>({
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
    {.text = "  │  ",
     .action = std::nullopt,
     .category = FooterCategory::Separator,
     .priority = FooterPriority::Core},
    {.text = "[:] Breakpoint",
     .action = TuiFooterAction::SetBreakpoint,
     .category = FooterCategory::Debug,
     .priority = FooterPriority::Core},
    {.text = "  ",
     .action = std::nullopt,
     .category = FooterCategory::Spacer,
     .priority = FooterPriority::Extended},
    {.text = "[w] Watchpoint",
     .action = TuiFooterAction::SetWatchpoint,
     .category = FooterCategory::Debug,
     .priority = FooterPriority::Extended},
});

static const auto running_row2_entries = paused_row2_entries;

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
            len += footer_display_width(footer_entry_text(e));
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
                        bool is_smp, bool alternate_shade = false,
                        std::optional<int> hit_col = std::nullopt)
    -> std::pair<std::string, std::optional<TuiFooterAction>> {
    std::vector<FooterEntry> active_entries =
        filter_footer_entries(entries, inner_w, is_debug_mode, is_smp);

    int content_len = 0;
    for (const auto& e : active_entries) {
        std::string text = footer_entry_text(e);
        if (e.category == FooterCategory::Separator) {
            text = "  ·  ";
        }
        content_len += footer_display_width(text);
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
            text = "  ·  ";
        }
        int item_len = footer_display_width(text);
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
            case FooterCategory::Config:
                color_tag = kThemeSky;
                break;
            case FooterCategory::Inspect:
                color_tag = kThemePink;
                break;
            case FooterCategory::Sys:
                color_tag = kThemeCoral;
                break;
            case FooterCategory::Separator:
                color_tag = kThemeMuted;
                break;
            case FooterCategory::Spacer:
                color_tag = nullptr;
                break;
        }

        if (color_tag != nullptr) {
            std::string_view text_sv{text};
            std::size_t close_bracket = text_sv.find(']');
            if (text_sv.starts_with('[') && close_bracket != std::string_view::npos) {
                const std::string_view key = text_sv.substr(1, close_bracket - 1);
                const char* ansi_color = alternate_shade ? "\033[100;37m" : "\033[47;30m";
                const char* indexed_color =
                    alternate_shade ? "\033[48;5;238;38;5;255m"
                                    : "\033[48;5;245;38;5;232m";
                switch (e.category) {
                    case FooterCategory::Exec:
                        ansi_color = alternate_shade ? "\033[100;37m" : "\033[43;30m";
                        indexed_color = alternate_shade ? "\033[48;5;179;38;5;232m"
                                                        : "\033[48;5;223;38;5;232m";
                        break;
                    case FooterCategory::Debug:
                        ansi_color = alternate_shade ? "\033[100;37m" : "\033[42;30m";
                        indexed_color = alternate_shade ? "\033[48;5;71;38;5;232m"
                                                        : "\033[48;5;121;38;5;232m";
                        break;
                    case FooterCategory::Config:
                        ansi_color = alternate_shade ? "\033[100;37m" : "\033[46;30m";
                        indexed_color = alternate_shade ? "\033[48;5;73;38;5;232m"
                                                        : "\033[48;5;117;38;5;232m";
                        break;
                    case FooterCategory::Inspect:
                        ansi_color = alternate_shade ? "\033[100;37m" : "\033[45;37m";
                        indexed_color = alternate_shade ? "\033[48;5;132;38;5;255m"
                                                        : "\033[48;5;211;38;5;232m";
                        break;
                    case FooterCategory::Sys:
                        ansi_color = alternate_shade ? "\033[100;37m" : "\033[41;37m";
                        indexed_color = alternate_shade ? "\033[48;5;167;38;5;255m"
                                                        : "\033[48;5;210;38;5;232m";
                        break;
                    case FooterCategory::Separator:
                    case FooterCategory::Spacer:
                        break;
                }
                row_str += use_basic_ansi_badges() ? ansi_color : indexed_color;
                row_str += key;
                row_str += "\033[0m";
                row_str += color_tag;
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
            return process_footer_row(paused_row1_entries, terminal_width - 2, is_dbg, is_smp,
                                      false, col)
                .second;
        if (row_idx == 1)
            return process_footer_row(paused_row2_entries, terminal_width - 2, is_dbg, is_smp,
                                      true, col)
                .second;
    } else {
        if (row_idx == 0)
            return process_footer_row(running_row1_entries, terminal_width - 2, is_dbg, is_smp,
                                      false, col)
                .second;
        if (row_idx == 1)
            return process_footer_row(running_row2_entries, terminal_width - 2, is_dbg, is_smp,
                                      true, col)
                .second;
    }

    return std::nullopt;
}

auto StatusBar::render_row(int row_idx, int width) -> std::string {
    if (row_idx == 0) {
        // Header
        const size_t selected = (machine_.tui) ? machine_.tui->selected_hart() : 0;
        std::string status_badge = status_override_;
        if (status_badge.empty()) {
            bool use_ansi = use_basic_ansi_badges();
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
        auto const left_header = make_left_header_layout(machine_, target_width, selected);
        std::string left_render = left_header.identity;
        if (!left_header.mode.empty()) {
            std::string styled_mode = left_header.mode;
            constexpr std::string_view separator = " · ";
            auto const midpoint = styled_mode.find(separator);
            if (midpoint != std::string::npos) {
                styled_mode.replace(
                    midpoint, separator.size(),
                    std::format(" \033[0m{}·\033[0m \033[1m{}", kThemeMuted, kThemeSky));
            }
            left_render += std::format("   \033[1m{}{}\033[0m", kThemeSky, styled_mode);
        }
        left_render += "   " + status_badge;

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
                std::string const term_title =
                    panel_mode_label(right_panel_mode_, target_right_width, trace_enabled_);
                std::string const mode_badge =
                    header_badge(term_title, "\033[46;30m", "\033[48;5;117;38;5;232m");
                std::string const focus_badge = term_focused
                                                    ? header_badge(
                                                          target_right_width < 45 ? "ON" : "ATTACHED",
                                                          "\033[42;30m",
                                                          "\033[48;5;121;38;5;232m")
                                                    : header_badge(
                                                          target_right_width < 45 ? "OFF" : "DETACHED",
                                                          "\033[47;30m",
                                                          "\033[48;5;245;38;5;232m");
                mode_prefix = " " + mode_badge + " " + focus_badge;
                break;
            }
            case TuiRightPanelMode::Display:
            default:
                mode_prefix = " " + header_badge(
                                          panel_mode_label(right_panel_mode_, target_right_width,
                                                           trace_enabled_),
                                          "\033[46;30m", "\033[48;5;117;38;5;232m");
                break;
        }

        if (machine_.num_harts() > 1) {
            std::string const hart_label = std::format("HART {}/{}", selected, machine_.num_harts());
            mode_prefix += " " + header_badge(hart_label, "\033[45;37m",
                                                "\033[48;5;183;38;5;232m");
        }
        std::string const speed_label =
            speed_control_label(machine_, kips_, paused_, target_right_width);
        mode_prefix += " " +
                       header_badge(speed_label, "\033[43;30m",
                                    "\033[48;5;223;38;5;232m");

        int const prefix_w = get_display_width(mode_prefix);
        int const available_mid_w = target_right_width - prefix_w - 2;

        std::string mid_text;
        if (scroll_offset_ > 0) {
            mid_text = std::format("═══ SCROLLBACK (-{}) ['c'/'Enter' Live] ═══", scroll_offset_);
            if (get_display_width(mid_text) > available_mid_w) {
                mid_text = std::format("SCROLL (-{})", scroll_offset_);
            }
        } else if (available_mid_w >= 10) {
            const size_t selected = (machine_.tui) ? machine_.tui->selected_hart() : 0;
            const auto& current_cpu = machine_.hart(selected);
            const auto cycles = current_cpu.clint_mmio.mcycle;
            const auto icount = current_cpu.e_icount;
            const double cpi =
                icount == 0 ? 0.0 : static_cast<double>(cycles) / static_cast<double>(icount);
            std::string const metric_sep = std::format(" {}·\033[0m ", kThemeMuted);

            if (machine_.runtime_profile.is_cycle_mode()) {
                std::string full_stats =
                    std::format("Cycles {}{}Instruction Count {}{}CPI {:.2f}",
                                format_metric_count(cycles), metric_sep,
                                format_metric_count(icount), metric_sep, cpi);
                if (get_display_width(full_stats) <= available_mid_w) {
                    mid_text = full_stats;
                } else {
                    std::string med_stats =
                        std::format("Cycles {}{}Instructions {}{}CPI {:.2f}",
                                    format_metric_count(cycles), metric_sep,
                                    format_metric_count(icount), metric_sep, cpi);
                    if (get_display_width(med_stats) <= available_mid_w) {
                        mid_text = med_stats;
                    } else {
                        std::string short_stats =
                            std::format("C {}{}I {}", format_metric_count(cycles), metric_sep,
                                        format_metric_count(icount));
                        if (get_display_width(short_stats) <= available_mid_w) {
                            mid_text = short_stats;
                        }
                    }
                }
            } else {
                std::string full_stats =
                    std::format("Instruction Count {}", format_metric_count(icount));
                if (get_display_width(full_stats) <= available_mid_w) {
                    mid_text = full_stats;
                } else {
                    std::string med_stats =
                        std::format("Instructions {}", format_metric_count(icount));
                    if (get_display_width(med_stats) <= available_mid_w) {
                        mid_text = med_stats;
                    } else {
                        std::string short_stats = std::format("I {}", format_metric_count(icount));
                        if (get_display_width(short_stats) <= available_mid_w) {
                            mid_text = short_stats;
                        }
                    }
                }
            }
        }

        int const mid_w = get_display_width(mid_text);
        int const pad_total = std::max(0, target_right_width - (prefix_w + mid_w));

        std::string right_render =
            mode_prefix + std::string(static_cast<std::size_t>(pad_total), ' ') + mid_text;
        if (get_display_width(right_render) > target_right_width) {
            right_render = format_to_width(right_render, target_right_width);
        } else if (get_display_width(right_render) < target_right_width) {
            right_render += std::string(
                static_cast<std::size_t>(target_right_width - get_display_width(right_render)),
                ' ');
        }

        std::string screen;
        const auto style = get_active_theme_style();
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
            footer_line1 =
                process_footer_row(paused_row1_entries, width - 2, is_dbg, is_smp, false).first;
            footer_line2 =
                process_footer_row(paused_row2_entries, width - 2, is_dbg, is_smp, true).first;
        } else {
            footer_line1 =
                process_footer_row(running_row1_entries, width - 2, is_dbg, is_smp, false).first;
            footer_line2 =
                process_footer_row(running_row2_entries, width - 2, is_dbg, is_smp, true).first;
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
