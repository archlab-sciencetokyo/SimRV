/**
 * @file StatusBar.cpp
 * @brief Implements StatusBar widget rendering.
 */
#include "simrv/tui/StatusBar.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include <format>

namespace simrv::tui {

StatusBar::StatusBar(simrv::core::Machine& machine) : machine_(machine) {}

void StatusBar::update_kips(uint64_t current_kips) {
    kips_ = current_kips;
}

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
    std::string sim_mode = machine_.s_cycle_accurate ? "CA" : "IA";
    std::string tui_mode = machine_.s_debug_mode ? "Debug" : "Normal";
    int target_width = layout_ == TuiLayout::Split ? left_width_ : width - 2;
    std::string prefix;
    if (target_width < 35) {
        prefix = " SimRV | ";
    } else if (target_width < 50) {
        prefix = std::format(" SimRV [{}] | ", binary_name);
    } else {
        prefix = std::format(" SimRV [{}] ({}, {}, {}) | ", binary_name, mode_str, sim_mode, tui_mode);
    }

    int prefix_len = get_display_width(prefix);
    std::string status_text = paused_ ? " PAUSED " : " RUNNING ";
    if (!status_override_.empty()) {
        status_text = " TRAPPED ";
    }
    int badge_len = static_cast<int>(status_text.length());

    int x_start = 2 + prefix_len; // 1-indexed column + 1 border char ║
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
    int mode_len = 2 + static_cast<int>(mode_label.length()); // "[" + mode_label + "]"
    int x_end = x_start + mode_len - 1;

    return (x >= x_start && x <= x_end);
}

auto StatusBar::get_footer_action_at_col(int col) const -> std::optional<TuiFooterAction> {
    if (col < 0) return std::nullopt;

    struct FooterItem {
        const char* text;
        TuiFooterAction action;
    };

    static constexpr std::array<FooterItem, 15> paused_items = {{
        {" [s] Step",      TuiFooterAction::Step},
        {"[b] Back",       TuiFooterAction::StepBack},
        {"[n] StepN",      TuiFooterAction::StepN},
        {"[g] N-Size",     TuiFooterAction::SetStepSize},
        {"[r] Regs",       TuiFooterAction::CycleRegs},
        {"[l] Tools",      TuiFooterAction::CycleTools},
        {"[o] Load",       TuiFooterAction::LoadBinary},
        {"[,] Settings",   TuiFooterAction::OpenSettings},
        {"[:] Set-BP",     TuiFooterAction::SetBreakpoint},
        {"[k] Toggle-BP",  TuiFooterAction::TogglePcBreakpoint},
        {"[f] Speed",      TuiFooterAction::SetSpeed},
        {"[m] Mem",        TuiFooterAction::InspectMem},
        {"[F1/?] Help",    TuiFooterAction::ToggleHelp},
        {"[c] Run",        TuiFooterAction::RunPause},
        {"[q] Quit",       TuiFooterAction::Quit},
    }};

    static constexpr std::array<FooterItem, 11> running_items = {{
        {" [Ctrl-P] Pause", TuiFooterAction::RunPause},
        {"[r] Regs",        TuiFooterAction::CycleRegs},
        {"[l] Tools",       TuiFooterAction::CycleTools},
        {"[Tab] Layout",    TuiFooterAction::CycleLayout},
        {"[p] Panel",       TuiFooterAction::TogglePanel},
        {"[v] Trace",       TuiFooterAction::ToggleTrace},
        {"[,] Settings",    TuiFooterAction::OpenSettings},
        {"[f] Speed",       TuiFooterAction::SetSpeed},
        {"[:] Set-BP",      TuiFooterAction::SetBreakpoint},
        {"[F1/?] Help",     TuiFooterAction::ToggleHelp},
        {"[Ctrl-Q] Quit",    TuiFooterAction::Quit},
    }};

    int current_x = 0;
    if (paused_) {
        for (std::size_t i = 0; i < paused_items.size(); ++i) {
            int len = static_cast<int>(std::strlen(paused_items[i].text));
            if (col >= current_x && col < current_x + len) {
                return paused_items[i].action;
            }
            current_x += len;
            if (i + 1 < paused_items.size()) {
                current_x += 3; // +3 for " | "
            }
        }
    } else {
        for (std::size_t i = 0; i < running_items.size(); ++i) {
            int len = static_cast<int>(std::strlen(running_items[i].text));
            if (col >= current_x && col < current_x + len) {
                return running_items[i].action;
            }
            current_x += len;
            if (i + 1 < running_items.size()) {
                current_x += 3; // +3 for " | "
            }
        }
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
            bool use_ansi = (get_tui_theme() == TuiTheme::Adaptive || get_tui_theme() == TuiTheme::HighContrast);
            status_badge = paused_
                ? (use_ansi ? "\033[43;30m PAUSED \033[0m" : "\033[48;5;223m\033[38;5;232m PAUSED \033[0m")
                : (use_ansi ? "\033[42;30m RUNNING \033[0m" : "\033[48;5;121m\033[38;5;232m RUNNING \033[0m");
        } else if (status_badge == "\033[1;38;5;234;48;5;210m TRAPPED \033[0m" && (get_tui_theme() == TuiTheme::HighContrast || get_tui_theme() == TuiTheme::Adaptive)) {
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
            const double cpi = icount == 0 ? 0.0 : static_cast<double>(cycles) / static_cast<double>(icount);
            
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
                        right_text = std::format("{}Cycles: {} | Insns: {} | CPI: {:.2f}",
                                                 dbg_info, simrv::util::format_with_commas(cycles), simrv::util::format_with_commas(icount), cpi);
                    } else {
                        right_text = std::format("{}Insns: {}",
                                                 dbg_info, simrv::util::format_with_commas(icount));
                    }
                } else {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("{}C: {} | I: {}",
                                                 dbg_info, simrv::util::format_with_commas(cycles), simrv::util::format_with_commas(icount));
                    } else {
                        right_text = std::format("{}I: {}",
                                                 dbg_info, simrv::util::format_with_commas(icount));
                    }
                }
            } else {
                if (target_width_right >= 75) {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("{}Cycles: {} | Insns: {} | CPI: {:.2f} | Speed: {}",
                                                 dbg_info, simrv::util::format_scaled(cycles), simrv::util::format_scaled(icount),
                                                 cpi, speed_str);
                    } else {
                        right_text = std::format("{}Insns: {} | Speed: {}",
                                                 dbg_info, simrv::util::format_scaled(icount), speed_str);
                    }
                } else if (target_width_right >= 55) {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("{}Cycles: {} | Insns: {} | Speed: {}",
                                                 dbg_info, simrv::util::format_scaled(cycles), simrv::util::format_scaled(icount), speed_str);
                    } else {
                        right_text = std::format("{}Insns: {} | Speed: {}",
                                                 dbg_info, simrv::util::format_scaled(icount), speed_str);
                    }
                } else if (target_width_right >= 40) {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("{}C: {} | I: {} | Speed: {}",
                                                 dbg_info, simrv::util::format_scaled(cycles), simrv::util::format_scaled(icount), speed_str);
                    } else {
                        right_text = std::format("{}I: {} | Speed: {}",
                                                 dbg_info, simrv::util::format_scaled(icount), speed_str);
                    }
                } else {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("{}C: {} | I: {}",
                                                 dbg_info, simrv::util::format_scaled(cycles), simrv::util::format_scaled(icount));
                    } else {
                        right_text = std::format("{}I: {}",
                                                 dbg_info, simrv::util::format_scaled(icount));
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
                right_render = mode_prefix + std::string(static_cast<std::size_t>(left_pad), ' ') + text + std::string(static_cast<std::size_t>(right_pad), ' ');
            } else {
                right_render = format_to_width(mode_prefix + text, target_right_width);
            }
            right_render = color_prefix + right_render + color_suffix;
        } else {
            int stats_len = get_display_width(right_text);
            int pad = target_right_width - (mode_len + stats_len);
            if (pad > 0) {
                right_render = mode_prefix + std::string(static_cast<std::size_t>(pad), ' ') + right_text;
            } else {
                right_render = format_to_width(mode_prefix + right_text, target_right_width);
            }
        }

        std::string screen;
        switch (layout_) {
            case TuiLayout::Split:
                screen += std::string(kThemeBorder) + "╔" + make_repeated_string("═", left_width_) + "╤" +
                          make_repeated_string("═", right_width_) + "╗\033[0m\n";
                screen += std::string(kThemeBorder) + "║\033[0m" + left_render + kThemeBorder + "│\033[0m" + right_render +
                          kThemeBorder + "║\033[0m\n";
                screen += std::string(kThemeBorder) + "╠" + make_repeated_string("═", left_width_) + "╪" +
                          make_repeated_string("═", right_width_) + "╣\033[0m\n";
                break;
            case TuiLayout::FullRight:
                screen += std::string(kThemeBorder) + "╔" + make_repeated_string("═", width - 2) + "╗\033[0m\n";
                screen += std::string(kThemeBorder) + "║\033[0m" + right_render + kThemeBorder + "║\033[0m\n";
                screen += std::string(kThemeBorder) + "╠" + make_repeated_string("═", width - 2) + "╣\033[0m\n";
                break;
            default:
                screen += std::string(kThemeBorder) + "╔" + make_repeated_string("═", width - 2) + "╗\033[0m\n";
                screen += std::string(kThemeBorder) + "║\033[0m" + left_render + kThemeBorder + "║\033[0m\n";
                screen += std::string(kThemeBorder) + "╠" + make_repeated_string("═", width - 2) + "╣\033[0m\n";
                break;
        }
        return screen;
    } else if (row_idx == 1) {
        // Footer
        std::string footer_text;
        if (paused_) {
            footer_text =
                " [s] Step | [b] Back | [n] StepN | [g] N-Size | [r] Regs | [l] Tools | [o] Load | [,] Settings | [:] Set-BP | [k] Toggle-BP | [f] Speed | [m] Mem | [F1/?] Help | [c] Run | [q] Quit ";
        } else {
            footer_text =
                " [Ctrl-P] Pause | [r] Regs | [l] Tools | [Tab] Layout | [p] Panel | [v] Trace | [,] Settings | [f] Speed | [:] Set-BP | [F1/?] Help | [Ctrl-Q] Quit ";
        }
        int footer_len = get_display_width(footer_text);
        int pad_foot = (width - 2) - footer_len;
        std::string footer_render = footer_text;
        if (pad_foot > 0) {
            footer_render += std::string(static_cast<std::size_t>(pad_foot), ' ');
        } else {
            footer_render = format_to_width(footer_render, width - 2);
        }
        std::string screen = std::string(kThemeBorder) + "║\033[0m" + footer_render + kThemeBorder + "║\033[0m\n";
        screen += std::string(kThemeBorder) + "╚" + make_repeated_string("═", width - 2) + "╝\033[0m";
        return screen;
    }
    return "";
}

}  // namespace simrv::tui
