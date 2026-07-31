/**
 * @file StatusBar.cpp
 * @brief Implements StatusBar widget rendering.
 */
#include "simrv/tui/panels/StatusBar.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

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

    int x_start = (layout_ == TuiLayout::Split) ? (left_width_ + 2) : 2;
    std::string mode_label;
    switch (right_panel_mode_) {
        case TuiRightPanelMode::Terminal:
            mode_label = trace_enabled_ ? "Terminal [Trace ON]" : "Terminal";
            break;
        case TuiRightPanelMode::Display:
        default:
            mode_label = "Display";
            break;
    }
    int mode_len = 2 + static_cast<int>(mode_label.length());  // "[" + mode_label + "]"
    int x_end = x_start + mode_len - 1;

    return (x >= x_start && x <= x_end);
}

enum class FooterCategory : uint8_t {
    DebugExec,
    DebugInspect,
    PanelNav,
    SettingsConfig,
    HelpQuit,
    Separator,
    Spacer
};

struct FooterEntry {
    const char* text;
    std::optional<TuiFooterAction> action;
    FooterCategory category = FooterCategory::Spacer;
};

static const auto paused_row1_entries = std::to_array<FooterEntry>({
    // Debug Execution Group
    {.text = "[s] Step", .action = TuiFooterAction::Step, .category = FooterCategory::DebugExec},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[b] Back",
     .action = TuiFooterAction::StepBack,
     .category = FooterCategory::DebugExec},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[n] StepN", .action = TuiFooterAction::StepN, .category = FooterCategory::DebugExec},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[c] Run", .action = TuiFooterAction::RunPause, .category = FooterCategory::DebugExec},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[g] Size",
     .action = TuiFooterAction::SetStepSize,
     .category = FooterCategory::DebugExec},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[f] Speed",
     .action = TuiFooterAction::SetSpeed,
     .category = FooterCategory::DebugExec},
    {.text = "  │  ", .action = std::nullopt, .category = FooterCategory::Separator},
    // Panel Actions Group
    {.text = "[r] Regs",
     .action = TuiFooterAction::CycleRegs,
     .category = FooterCategory::PanelNav},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[l] Tools",
     .action = TuiFooterAction::CycleTools,
     .category = FooterCategory::PanelNav},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[Tab] Layout",
     .action = TuiFooterAction::CycleLayout,
     .category = FooterCategory::PanelNav},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[p] Panel",
     .action = TuiFooterAction::TogglePanel,
     .category = FooterCategory::PanelNav},
});

static const auto paused_row2_entries = std::to_array<FooterEntry>({
    // Debug Inspection Group
    {.text = "[i] Mem",
     .action = TuiFooterAction::InspectMem,
     .category = FooterCategory::DebugInspect},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[:] SetBP",
     .action = TuiFooterAction::SetBreakpoint,
     .category = FooterCategory::DebugInspect},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[w] SetWP",
     .action = TuiFooterAction::SetWatchpoint,
     .category = FooterCategory::DebugInspect},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[k] TogBP",
     .action = TuiFooterAction::TogglePcBreakpoint,
     .category = FooterCategory::DebugInspect},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[m] ManageBP",
     .action = TuiFooterAction::ManageBreakpoints,
     .category = FooterCategory::DebugInspect},
    {.text = "  │  ", .action = std::nullopt, .category = FooterCategory::Separator},
    // Settings, Config, Help & Quit Group
    {.text = "[o] Load",
     .action = TuiFooterAction::LoadBinary,
     .category = FooterCategory::SettingsConfig},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[,] Settings",
     .action = TuiFooterAction::OpenSettings,
     .category = FooterCategory::SettingsConfig},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[y] SysConfig",
     .action = TuiFooterAction::ConfigureSystem,
     .category = FooterCategory::SettingsConfig},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[Alt-m] MISA",
     .action = TuiFooterAction::ConfigureMisa,
     .category = FooterCategory::SettingsConfig},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[F1/?] Help",
     .action = TuiFooterAction::ToggleHelp,
     .category = FooterCategory::HelpQuit},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[q] Quit", .action = TuiFooterAction::Quit, .category = FooterCategory::HelpQuit},
});

static const auto running_row1_entries = std::to_array<FooterEntry>({
    // Debug Execution Group
    {.text = "[Ctrl-P] Pause",
     .action = TuiFooterAction::RunPause,
     .category = FooterCategory::DebugExec},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[f] Speed",
     .action = TuiFooterAction::SetSpeed,
     .category = FooterCategory::DebugExec},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[v] Trace",
     .action = TuiFooterAction::ToggleTrace,
     .category = FooterCategory::DebugExec},
    {.text = "  │  ", .action = std::nullopt, .category = FooterCategory::Separator},
    // Panel Actions Group
    {.text = "[r] Regs",
     .action = TuiFooterAction::CycleRegs,
     .category = FooterCategory::PanelNav},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[l] Tools",
     .action = TuiFooterAction::CycleTools,
     .category = FooterCategory::PanelNav},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[Tab] Layout",
     .action = TuiFooterAction::CycleLayout,
     .category = FooterCategory::PanelNav},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[p] Panel",
     .action = TuiFooterAction::TogglePanel,
     .category = FooterCategory::PanelNav},
});

static const auto running_row2_entries = std::to_array<FooterEntry>({
    // Debug Inspection Group
    {.text = "[m] Mem",
     .action = TuiFooterAction::InspectMem,
     .category = FooterCategory::DebugInspect},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[:] SetBP",
     .action = TuiFooterAction::SetBreakpoint,
     .category = FooterCategory::DebugInspect},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[w] SetWP",
     .action = TuiFooterAction::SetWatchpoint,
     .category = FooterCategory::DebugInspect},
    {.text = "  │  ", .action = std::nullopt, .category = FooterCategory::Separator},
    // Settings, Config, Help & Quit Group
    {.text = "[o] Load",
     .action = TuiFooterAction::LoadBinary,
     .category = FooterCategory::SettingsConfig},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[,] Settings",
     .action = TuiFooterAction::OpenSettings,
     .category = FooterCategory::SettingsConfig},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[y] SysConfig",
     .action = TuiFooterAction::ConfigureSystem,
     .category = FooterCategory::SettingsConfig},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[Alt-m] MISA",
     .action = TuiFooterAction::ConfigureMisa,
     .category = FooterCategory::SettingsConfig},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[F1/?] Help",
     .action = TuiFooterAction::ToggleHelp,
     .category = FooterCategory::HelpQuit},
    {.text = "  ", .action = std::nullopt, .category = FooterCategory::Spacer},
    {.text = "[Ctrl-Q] Quit",
     .action = TuiFooterAction::Quit,
     .category = FooterCategory::HelpQuit},
});

auto process_footer_row(std::span<const FooterEntry> entries, int inner_w, bool is_debug_mode,
                        std::optional<int> hit_col = std::nullopt)
    -> std::pair<std::string, std::optional<TuiFooterAction>> {
    std::vector<FooterEntry> active_entries;
    active_entries.reserve(entries.size());

    for (const auto& e : entries) {
        if (!is_debug_mode) {
            if (e.category == FooterCategory::DebugInspect) {
                continue;
            }
            if (e.category == FooterCategory::DebugExec && e.action != TuiFooterAction::RunPause &&
                e.action != TuiFooterAction::SetSpeed && e.action != TuiFooterAction::Step) {
                continue;
            }
        }
        active_entries.push_back(e);
    }

    // Trim leading spacers and separators
    while (!active_entries.empty() &&
           (active_entries.front().category == FooterCategory::Spacer ||
            active_entries.front().category == FooterCategory::Separator)) {
        active_entries.erase(active_entries.begin());
    }

    // Trim trailing spacers and separators
    while (!active_entries.empty() &&
           (active_entries.back().category == FooterCategory::Spacer ||
            active_entries.back().category == FooterCategory::Separator)) {
        active_entries.pop_back();
    }

    int content_len = 0;
    for (const auto& e : active_entries) {
        content_len += static_cast<int>(get_display_width(e.text));
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
        int item_len = get_display_width(e.text);
        if (hit_col.has_value() && e.action.has_value()) {
            if (*hit_col >= current_col && *hit_col < current_col + item_len) {
                hit_action = e.action;
            }
        }

        const char* color_tag = nullptr;
        switch (e.category) {
            case FooterCategory::DebugExec:
                color_tag = kThemePeach;
                break;
            case FooterCategory::DebugInspect:
                color_tag = kThemeMint;
                break;
            case FooterCategory::PanelNav:
                color_tag = kThemeSky;
                break;
            case FooterCategory::SettingsConfig:
                color_tag = kThemePink;
                break;
            case FooterCategory::HelpQuit:
                color_tag = kThemeCoral;
                break;
            case FooterCategory::Separator:
                color_tag = kThemeBorder;
                break;
            case FooterCategory::Spacer:
                color_tag = nullptr;
                break;
        }

        if (color_tag != nullptr) {
            std::string_view text_sv{e.text};
            std::size_t close_bracket = text_sv.find(']');
            if (text_sv.starts_with('[') && close_bracket != std::string_view::npos) {
                row_str += "\033[1m";
                row_str += color_tag;
                row_str += text_sv.substr(0, close_bracket + 1);
                row_str += "\033[22m";
                row_str += text_sv.substr(close_bracket + 1);
            } else {
                row_str += color_tag;
                row_str += e.text;
            }
            row_str += "\033[0m";
        } else {
            row_str += e.text;
        }
        current_col += item_len;
    }

    if (pad > 0) {
        row_str.append(static_cast<std::size_t>(right_pad), ' ');
    }

    if (get_display_width(row_str) > inner_w) {
        row_str = format_to_width(row_str, inner_w);
    }
    return {row_str, hit_action};
}

auto StatusBar::get_footer_action_at_col(int col, int row_idx) const
    -> std::optional<TuiFooterAction> {
    if (col < 0) return std::nullopt;

    struct winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);  // NOLINT(cppcoreguidelines-pro-type-vararg)
    int term_w = w.ws_col > 0 ? w.ws_col : 80;

    bool is_dbg = machine_.s_debug_mode;
    if (paused_) {
        if (row_idx == 0)
            return process_footer_row(paused_row1_entries, term_w - 2, is_dbg, col).second;
        if (row_idx == 1)
            return process_footer_row(paused_row2_entries, term_w - 2, is_dbg, col).second;
    } else {
        if (row_idx == 0)
            return process_footer_row(running_row1_entries, term_w - 2, is_dbg, col).second;
        if (row_idx == 1)
            return process_footer_row(running_row2_entries, term_w - 2, is_dbg, col).second;
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
        std::string mode_str = machine_.s_appmode ? "Application" : "OS/RTOS";
        std::string status_badge = status_override_;
        if (status_badge.empty()) {
            bool use_ansi = (get_tui_theme() == TuiTheme::Adaptive ||
                             get_tui_theme() == TuiTheme::HighContrast);
            status_badge = paused_ ? (use_ansi ? "\033[43;30m PAUSED \033[0m"
                                               : "\033[48;5;223m\033[38;5;232m PAUSED \033[0m")
                                   : (use_ansi ? "\033[42;30m RUNNING \033[0m"
                                               : "\033[48;5;121m\033[38;5;232m RUNNING \033[0m");
        } else if (status_badge == "\033[1;38;5;234;48;5;210m TRAPPED \033[0m" &&
                   (get_tui_theme() == TuiTheme::HighContrast ||
                    get_tui_theme() == TuiTheme::Adaptive)) {
            status_badge = "\033[1;41;37m TRAPPED \033[0m";
        }
        int target_width = layout_ == TuiLayout::Split ? left_width_ : width - 2;
        std::string left_render;
        if (target_width < 35) {
            left_render = std::format(" SimRV | {}", status_badge);
        } else if (target_width < 50) {
            left_render = std::format(" SimRV [{}] | {}", binary_name, status_badge);
        } else {
            left_render = std::format(" SimRV [{}] ({}) | {}", binary_name, mode_str, status_badge);
        }

        int left_printed_len = get_display_width(left_render);
        int pad_left = target_width - left_printed_len;
        if (pad_left > 0) {
            left_render += std::string(static_cast<std::size_t>(pad_left), ' ');
        } else {
            left_render = format_to_width(left_render, target_width);
        }

        std::string right_text;
        std::string color_prefix = "";
        std::string color_suffix = "";
        if (scroll_offset_ > 0) {
            color_prefix = "\033[1;5;30;43m";
            color_suffix = "\033[0m";
            right_text = std::format(
                " ═══ SCROLLBACK (Offset: -{}) [Press 'c'/'Enter' to Live] ═══ ", scroll_offset_);
        } else {
            const auto cycles = machine_.cpu.clint_mmio.mcycle;
            const auto icount = machine_.cpu.e_icount;
            const double cpi =
                icount == 0 ? 0.0 : static_cast<double>(cycles) / static_cast<double>(icount);

            // Calculate simulation speed in MIPS/KIPS
            const double mips = static_cast<double>(kips_) / 1000.0;

            std::string speed_str;
            if (mips >= 1.0) {
                speed_str = std::format("{:.2f} MIPS", mips);
            } else {
                speed_str = std::format("{:.1f} KIPS", static_cast<double>(kips_));
            }
            uint64_t budget = 0;
            uint64_t delay = 0;
            uint64_t gran = 50;
            if (machine_.tui) {
                budget = machine_.tui->step_budget_.load(std::memory_order_relaxed);
                delay = machine_.tui->step_delay_us_.load(std::memory_order_relaxed);
                gran = machine_.tui->step_granularity_.load(std::memory_order_relaxed);
            }
            std::string dbg_info;
            const auto num_bp = machine_.breakpoints.get_pc_breakpoints().size();
            const auto num_wp = machine_.breakpoints.get_watchpoints().size();
            if (num_bp > 0 || num_wp > 0) {
                dbg_info += std::format("BP:{} WP:{} | ", num_bp, num_wp);
            }
            if (machine_.s_rollback_enabled) {
                dbg_info += "Rollback: ON | ";
            }
            dbg_info += std::format("StepN: {} | ", gran);
            if (budget > 0) {
                dbg_info += std::format("Steps: {} | ", budget);
            }
            if (delay > 0) {
                double hz = 1000000.0 / static_cast<double>(delay);
                if (hz >= 1000.0) {
                    dbg_info += std::format("Speed: {:.0f}kHz | ", hz / 1000.0);
                } else {
                    dbg_info += std::format("Speed: {:.1f}Hz | ", hz);
                }
            } else if (paused_) {
                dbg_info += "Speed: Max | ";
            }

            int target_width_right = layout_ == TuiLayout::Split ? right_width_ : width - 2;

            if (paused_) {
                if (target_width_right >= 55) {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("{}Cycles: {} | Insns: {} | CPI: {:.2f}", dbg_info,
                                                 simrv::util::format_with_commas(cycles),
                                                 simrv::util::format_with_commas(icount), cpi);
                    } else {
                        right_text = std::format("{}Insns: {}", dbg_info,
                                                 simrv::util::format_with_commas(icount));
                    }
                } else {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("{}C: {} | I: {}", dbg_info,
                                                 simrv::util::format_with_commas(cycles),
                                                 simrv::util::format_with_commas(icount));
                    } else {
                        right_text = std::format("{}I: {}", dbg_info,
                                                 simrv::util::format_with_commas(icount));
                    }
                }
            } else {
                if (target_width_right >= 75) {
                    if (machine_.s_cycle_accurate) {
                        right_text =
                            std::format("{}Cycles: {} | Insns: {} | CPI: {:.2f} | Speed: {}",
                                        dbg_info, simrv::util::format_scaled(cycles),
                                        simrv::util::format_scaled(icount), cpi, speed_str);
                    } else {
                        right_text = std::format("{}Insns: {} | Speed: {}", dbg_info,
                                                 simrv::util::format_scaled(icount), speed_str);
                    }
                } else if (target_width_right >= 55) {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("{}Cycles: {} | Insns: {} | Speed: {}", dbg_info,
                                                 simrv::util::format_scaled(cycles),
                                                 simrv::util::format_scaled(icount), speed_str);
                    } else {
                        right_text = std::format("{}Insns: {} | Speed: {}", dbg_info,
                                                 simrv::util::format_scaled(icount), speed_str);
                    }
                } else if (target_width_right >= 40) {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("{}C: {} | I: {} | Speed: {}", dbg_info,
                                                 simrv::util::format_scaled(cycles),
                                                 simrv::util::format_scaled(icount), speed_str);
                    } else {
                        right_text = std::format("{}I: {} | Speed: {}", dbg_info,
                                                 simrv::util::format_scaled(icount), speed_str);
                    }
                } else {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("{}C: {} | I: {}", dbg_info,
                                                 simrv::util::format_scaled(cycles),
                                                 simrv::util::format_scaled(icount));
                    } else {
                        right_text =
                            std::format("{}I: {}", dbg_info, simrv::util::format_scaled(icount));
                    }
                }
            }
        }

        int target_right_width = layout_ == TuiLayout::Split ? right_width_ : width - 2;
        std::string right_render;

        std::string mode_label;
        switch (right_panel_mode_) {
            case TuiRightPanelMode::Terminal:
                mode_label = trace_enabled_ ? "Terminal [Trace ON]" : "Terminal";
                break;
            case TuiRightPanelMode::Display:
            default:
                mode_label = "Display";
                break;
        }
        std::string mode_prefix = std::format("{}[{}]\033[0m", kThemeSky, mode_label);
        int mode_len = get_display_width(mode_prefix);

        if (scroll_offset_ > 0) {
            std::string text = " " + right_text;
            int text_len = get_display_width(text);
            int pad = target_right_width - (mode_len + text_len);
            if (pad > 0) {
                int left_pad = pad / 2;
                int right_pad = pad - left_pad;
                right_render = mode_prefix + std::string(static_cast<std::size_t>(left_pad), ' ') +
                               text + std::string(static_cast<std::size_t>(right_pad), ' ');
            } else {
                right_render = format_to_width(mode_prefix + text, target_right_width);
            }
            right_render = color_prefix + right_render + color_suffix;
        } else {
            int stats_len = get_display_width(right_text);
            int pad = target_right_width - (mode_len + stats_len);
            if (pad > 0) {
                right_render =
                    mode_prefix + std::string(static_cast<std::size_t>(pad), ' ') + right_text;
            } else {
                right_render = format_to_width(mode_prefix + right_text, target_right_width);
            }
        }

        std::string screen;
        switch (layout_) {
            case TuiLayout::Split:
                screen += std::string(kThemeBorder) + "╔" + make_repeated_string("═", left_width_) +
                          "╤" + make_repeated_string("═", right_width_) + "╗\033[0m\n";
                screen += std::string(kThemeBorder) + "║\033[0m" + left_render + kThemeBorder +
                          "│\033[0m" + right_render + kThemeBorder + "║\033[0m\n";
                screen += std::string(kThemeBorder) + "╠" + make_repeated_string("═", left_width_) +
                          "╪" + make_repeated_string("═", right_width_) + "╣\033[0m\n";
                break;
            case TuiLayout::FullRight:
                screen += std::string(kThemeBorder) + "╔" + make_repeated_string("═", width - 2) +
                          "╗\033[0m\n";
                screen += std::string(kThemeBorder) + "║\033[0m" + right_render + kThemeBorder +
                          "║\033[0m\n";
                screen += std::string(kThemeBorder) + "╠" + make_repeated_string("═", width - 2) +
                          "╣\033[0m\n";
                break;
            default:
                screen += std::string(kThemeBorder) + "╔" + make_repeated_string("═", width - 2) +
                          "╗\033[0m\n";
                screen += std::string(kThemeBorder) + "║\033[0m" + left_render + kThemeBorder +
                          "║\033[0m\n";
                screen += std::string(kThemeBorder) + "╠" + make_repeated_string("═", width - 2) +
                          "╣\033[0m\n";
                break;
        }
        return screen;
    } else if (row_idx == 1) {
        // 2-Row Footer (Centered & Grouped)
        std::string footer_line1;
        std::string footer_line2;
        bool is_dbg = machine_.s_debug_mode;
        if (paused_) {
            footer_line1 = process_footer_row(paused_row1_entries, width - 2, is_dbg).first;
            footer_line2 = process_footer_row(paused_row2_entries, width - 2, is_dbg).first;
        } else {
            footer_line1 = process_footer_row(running_row1_entries, width - 2, is_dbg).first;
            footer_line2 = process_footer_row(running_row2_entries, width - 2, is_dbg).first;
        }

        std::string screen =
            std::string(kThemeBorder) + "║\033[0m" + footer_line1 + kThemeBorder + "║\033[0m\n";
        screen +=
            std::string(kThemeBorder) + "║\033[0m" + footer_line2 + kThemeBorder + "║\033[0m\n";
        screen +=
            std::string(kThemeBorder) + "╚" + make_repeated_string("═", width - 2) + "╝\033[0m";
        return screen;
    }
    return "";
}

}  // namespace simrv::tui
