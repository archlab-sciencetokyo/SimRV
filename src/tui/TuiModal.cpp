/**
 * @file TuiModal.cpp
 * @brief Implementation of TUI Modal dialog manager and overlay renderer.
 */

#include "simrv/tui/TuiModal.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <utility>

#include "simrv/core/Machine.hpp"
#include "simrv/tui/panels/LeftPane.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/modals/AddressModal.hpp"
#include "simrv/tui/modals/BreakpointModal.hpp"
#include "simrv/tui/modals/HelpModal.hpp"
#include "simrv/tui/modals/LoadModal.hpp"
#include "simrv/tui/modals/MisaModal.hpp"
#include "simrv/tui/modals/SettingsModal.hpp"
#include "simrv/tui/modals/StepModal.hpp"
#include "simrv/tui/modals/SystemConfigModal.hpp"
#include "simrv/xlen/Helpers.hpp"

namespace simrv::tui {

TuiModal::TuiModal(simrv::core::Machine& machine) : machine_(machine) {}

void TuiModal::open(ModalType type, LeftPane* left_pane, uint64_t step_granularity,
                    uint64_t step_delay_us) {
    active_modal_ = type;
    input_.clear();
    bp_cursor_ = 0;
    switch (type) {
        case ModalType::SetBreakpoint:
        case ModalType::SetWatchpoint:
            modals::BreakpointModal::open(type, input_, machine_, left_pane);
            break;
        case ModalType::SetStepSize:
        case ModalType::SetSpeed:
            modals::StepModal::open(type, input_, step_granularity, step_delay_us);
            break;
        case ModalType::InspectAddress:
            modals::AddressModal::open(input_, left_pane);
            break;
        case ModalType::LoadBinary:
        case ModalType::LoadDiskImage:
            modals::LoadModal::open(type, input_, load_appmode_, machine_);
            break;
        case ModalType::Settings:
            modals::SettingsModal::open(settings_draft_, settings_cursor_, machine_);
            break;
        case ModalType::ConfigureMisa:
            modals::MisaModal::open(misa_draft_, misa_cursor_, machine_);
            break;
        case ModalType::ConfigureSystem:
            modals::SystemConfigModal::open(sysconfig_draft_, sysconfig_cursor_, machine_);
            break;
        default:
            break;
    }
}

void TuiModal::move_settings_cursor(int delta) {
    input_.clear();
    modals::SettingsModal::move_cursor(settings_cursor_, delta);
}

void TuiModal::toggle_setting_at_cursor() { toggle_setting_by_index(settings_cursor_); }

void TuiModal::toggle_setting_by_index(int index) {
    modals::SettingsModal::toggle_setting(settings_draft_, index, machine_);
}

void TuiModal::move_misa_cursor(int delta) {
    input_.clear();
    modals::MisaModal::move_cursor(misa_cursor_, delta);
}

void TuiModal::toggle_misa_at_cursor() { toggle_misa_by_index(misa_cursor_); }

void TuiModal::toggle_misa_by_index(int index) {
    modals::MisaModal::toggle_item(misa_draft_, index);
}

void TuiModal::apply_misa_profile(int profile_idx) {
    modals::MisaModal::apply_profile(misa_draft_, profile_idx);
}

void TuiModal::move_sysconfig_cursor(int delta) {
    input_.clear();
    modals::SystemConfigModal::move_cursor(sysconfig_cursor_, delta);
}

void TuiModal::adjust_sysconfig_at_cursor(int dir) {
    input_.clear();
    modals::SystemConfigModal::adjust_setting(sysconfig_draft_, sysconfig_cursor_, dir);
}

void TuiModal::toggle_sysconfig_at_cursor() {
    input_.clear();
    modals::SystemConfigModal::toggle_setting(sysconfig_draft_, sysconfig_cursor_);
}

void TuiModal::toggle_sysconfig_by_index(int index) {
    modals::SystemConfigModal::toggle_setting(sysconfig_draft_, index);
}

void TuiModal::push_sysconfig_digit(char c) {
    modals::SystemConfigModal::push_digit(sysconfig_draft_, sysconfig_cursor_, input_, c);
}

void TuiModal::pop_sysconfig_digit() {
    modals::SystemConfigModal::pop_digit(sysconfig_draft_, sysconfig_cursor_, input_);
}

void TuiModal::move_bp_cursor(int delta) {
    modals::BreakpointModal::move_cursor(bp_cursor_, delta, machine_);
}

auto TuiModal::remove_bp_at_cursor(const std::function<void(const std::string&)>& set_status_override_cb) -> bool {
    std::string removed_msg;
    bool removed = modals::BreakpointModal::remove_at_cursor(bp_cursor_, machine_, [&](const std::string& msg) {
        removed_msg = msg;
        if (set_status_override_cb) set_status_override_cb(msg);
    });
    if (removed && !removed_msg.empty()) {
        std::string title = (removed_msg.find("watchpoint") != std::string::npos || removed_msg.find("Watchpoint") != std::string::npos)
                                ? "WATCHPOINT REMOVED"
                                : "BREAKPOINT REMOVED";
        open_notice(title, removed_msg, false);
    }
    return removed;
}

void TuiModal::close() {
    active_modal_ = ModalType::None;
    input_.clear();
}

auto TuiModal::submit(LeftPane* left_pane, std::atomic<uint64_t>& step_granularity,
                      std::atomic<uint64_t>& step_delay_us,
                      const std::function<void(TuiRegPage)>& set_reg_page_cb,
                      const std::function<void(const std::string&)>& set_status_override_cb,
                      const std::function<void()>& on_speed_changed_cb) -> bool {
    if (active_modal_ == ModalType::None) return false;

    bool result = false;
    ModalType current_modal = active_modal_;
    std::string err_msg;

    auto notice_cb = [&](const std::string& msg) {
        if (msg.find("not found") != std::string::npos || msg.find("invalid") != std::string::npos || msg.find("Error") != std::string::npos) {
            err_msg = msg;
        } else if (set_status_override_cb) {
            set_status_override_cb(msg);
        }
    };

    switch (current_modal) {
        case ModalType::SetBreakpoint:
        case ModalType::SetWatchpoint:
            {
                std::string msg_text;
                auto bp_cb = [&](const std::string& msg) {
                    msg_text = msg;
                };
                result = modals::BreakpointModal::submit(current_modal, input_, machine_, bp_cb);
                if (result) {
                    std::string title = (current_modal == ModalType::SetBreakpoint) ? "BREAKPOINT CREATED" : "WATCHPOINT CREATED";
                    open_notice(title, msg_text.empty() ? "Watchpoint set successfully." : msg_text, false);
                    return true;
                } else if (!msg_text.empty()) {
                    open_notice("INVALID TARGET", msg_text, true);
                    return false;
                }
            }
            break;
        case ModalType::SetStepSize:
        case ModalType::SetSpeed:
            result = modals::StepModal::submit(current_modal, input_, step_granularity,
                                               step_delay_us, on_speed_changed_cb, notice_cb);
            break;
        case ModalType::InspectAddress:
            {
                std::string addr_msg;
                auto addr_cb = [&](const std::string& msg) {
                    addr_msg = msg;
                };
                result = modals::AddressModal::submit(input_, machine_, left_pane, addr_cb);
                if (result && set_reg_page_cb) {
                    set_reg_page_cb(TuiRegPage::STACK);
                }
                if (result) {
                    open_notice("INSPECT MEMORY", addr_msg.empty() ? "Inspecting memory address." : addr_msg, false);
                    return true;
                } else if (!addr_msg.empty()) {
                    open_notice("INVALID ADDRESS", addr_msg, true);
                    return false;
                }
            }
            break;
        case ModalType::LoadBinary:
        case ModalType::LoadDiskImage:
            result = modals::LoadModal::submit(current_modal, input_, load_appmode_, machine_,
                                               staged_binary_path_, staged_mode_change_,
                                               staged_target_appmode_, notice_cb);
            if (current_modal == ModalType::LoadBinary && staged_mode_change_ && !load_appmode_) {
                active_modal_ = ModalType::LoadDiskImage;
                input_ = machine_.s_fn_dskimg;
                return false;
            }
            if (result) {
                open_notice("PROGRAM LOADED", "Loaded program image. Resetting simulator system...", false);
                return true;
            }
            break;
        case ModalType::Settings:
            result = modals::SettingsModal::submit(settings_draft_, machine_, set_reg_page_cb);
            if (result) {
                open_notice("SETTINGS SAVED", "Simulator settings saved and applied successfully.", false);
                return true;
            }
            break;
        case ModalType::ConfigureMisa:
            result = modals::MisaModal::submit(misa_draft_, machine_, set_status_override_cb);
            if (result) {
                open_notice("MISA CONFIGURATION SAVED", "CPU MISA extensions updated. Resetting simulator system...", false);
                return true;
            }
            break;
        case ModalType::ConfigureSystem:
            result = modals::SystemConfigModal::submit(sysconfig_draft_, machine_);
            if (result) {
                open_notice("CA CONFIGURATION SAVED", "Cycle-Accurate system configuration saved and applied successfully.", false);
                return true;
            }
            break;
        default:
            break;
    }

    if (!result && !err_msg.empty()) {
        open_notice("ERROR", err_msg, true);
        return false;
    }

    if (active_modal_ == current_modal) {
        active_modal_ = ModalType::None;
        input_.clear();
    }
    return result;
}

void TuiModal::open_notice(const std::string& title, const std::string& message, bool is_error) {
    active_modal_ = ModalType::Notice;
    input_.clear();
    notice_title_ = title;
    notice_message_ = message;
    notice_is_error_ = is_error;
}

void TuiModal::render_overlay(std::vector<std::string>& lines, int term_width,
                              int term_height) const {
    if (active_modal_ == ModalType::None || lines.empty()) return;

    bool is_help = (active_modal_ == ModalType::Help);
    bool is_wide_modal = (is_help || active_modal_ == ModalType::Settings ||
                          active_modal_ == ModalType::ConfigureMisa ||
                          active_modal_ == ModalType::ConfigureSystem ||
                          active_modal_ == ModalType::Notice);
    int box_w = is_wide_modal ? std::min(78, term_width - 4) : std::min(58, term_width - 4);
    if (box_w < 35) return;

    std::string m_bg = kThemeModalBg;
    std::string m_rst = std::string("\033[0m") + m_bg;

    std::vector<std::string> content_rows;
    auto add_row = [&](const std::string& formatted_str) {
        std::string s = formatted_str;
        std::string target = "\033[0m";
        size_t pos = 0;
        while ((pos = s.find(target, pos)) != std::string::npos) {
            s.replace(pos, target.length(), m_rst);
            pos += m_rst.length();
        }
        content_rows.push_back(s);
    };

    std::string title;
    switch (active_modal_) {
        case ModalType::SetBreakpoint:
            title = " SET BREAKPOINT ";
            modals::BreakpointModal::render(active_modal_, content_rows, input_, &machine_);
            break;
        case ModalType::SetWatchpoint:
            title = " SET WATCHPOINT ";
            modals::BreakpointModal::render(active_modal_, content_rows, input_, &machine_);
            break;
        case ModalType::ManageBreakpoints:
            title = " MANAGE BREAKPOINTS & WATCHPOINTS ";
            modals::BreakpointModal::render(active_modal_, content_rows, input_, &machine_,
                                            bp_cursor_);
            break;
        case ModalType::SetStepSize:
            title = " SET STEP SIZE (N) ";
            modals::StepModal::render(active_modal_, content_rows, input_);
            break;
        case ModalType::SetSpeed:
            title = " SET SIMULATION FREQUENCY ";
            modals::StepModal::render(active_modal_, content_rows, input_);
            break;
        case ModalType::InspectAddress:
            title = " INSPECT MEMORY ADDRESS ";
            modals::AddressModal::render(content_rows, input_);
            break;
        case ModalType::LoadBinary:
            title = " LOAD PROGRAM BINARY ";
            modals::LoadModal::render(active_modal_, content_rows, input_, load_appmode_,
                                      staged_binary_path_);
            break;
        case ModalType::LoadDiskImage:
            title = " LOAD DISK IMAGE (Optional) ";
            modals::LoadModal::render(active_modal_, content_rows, input_, load_appmode_,
                                      staged_binary_path_);
            break;
        case ModalType::Settings:
            title = " SIMULATOR SETTINGS ";
            modals::SettingsModal::render(content_rows, add_row, settings_draft_,
                                          settings_cursor_, machine_);
            break;
        case ModalType::ConfigureMisa:
            title = " CONFIGURE CPU MISA & EXTENSIONS ";
            modals::MisaModal::render(content_rows, add_row, misa_draft_, misa_cursor_, machine_);
            break;
        case ModalType::ConfigureSystem:
            title = " CA SYSTEM CONFIGURATION ";
            modals::SystemConfigModal::render(content_rows, add_row, sysconfig_draft_,
                                              sysconfig_cursor_, input_);
            break;
        case ModalType::Help:
            title = " SIMULATOR KEYBOARD SHORTCUTS ";
            modals::HelpModal::render(content_rows, add_row, term_height, box_w);
            break;
        case ModalType::Notice:
            title = notice_is_error_ ? std::format(" ❌ {} ", notice_title_)
                                     : std::format(" ⚠️  {} ", notice_title_);
            add_row("");
            {
                std::size_t start = 0;
                std::size_t pos = 0;
                while ((pos = notice_message_.find('\n', start)) != std::string::npos) {
                    add_row(std::format(" {}", notice_message_.substr(start, pos - start)));
                    start = pos + 1;
                }
                if (start < notice_message_.size()) {
                    add_row(std::format(" {}", notice_message_.substr(start)));
                }
            }
            add_row("");
            add_row(std::format("{}Press \033[1m[Enter]\033[0m, \033[1m[Space]\033[0m or \033[1m[Esc]\033[0m to dismiss\033[0m", kThemeMuted));
            break;
        default:
            break;
    }

    if (content_rows.empty()) return;

    int box_h = static_cast<int>(content_rows.size()) + 2;  // top/bottom borders
    int start_y = (term_height - box_h) / 2;
    int start_x = (term_width - box_w) / 2;

    if (start_y < 0) start_y = 0;
    if (start_x < 0) start_x = 0;

    int max_y = std::min(term_height, start_y + box_h);
    int inner_w = box_w - 2;

    // Render Box Top Border
    if (start_y < static_cast<int>(lines.size())) {
        std::string title_fmt = std::format("\033[1m{}{}\033[0m{}", kThemeMint, title, kThemeBorder);
        int title_len = get_display_width(title);
        int dash_len = inner_w - title_len;
        if (dash_len < 0) dash_len = 0;
        int left_dash = dash_len / 2;
        int right_dash = dash_len - left_dash;

        std::string top_border =
            std::format("{}{}\033[0m{}{}{}{}\033[0m", kThemeBorder,
                        make_repeated_string("─", left_dash + 1), title_fmt, kThemeBorder,
                        make_repeated_string("─", right_dash + 1), "\033[0m");

        lines.at(static_cast<std::size_t>(start_y)) =
            overlay_string(lines.at(static_cast<std::size_t>(start_y)), top_border, start_x, box_w);
    }

    // Render Box Content Rows with background color persistence
    for (int i = 0; i < static_cast<int>(content_rows.size()); ++i) {
        int target_y = start_y + 1 + i;
        if (target_y >= max_y || target_y >= static_cast<int>(lines.size())) break;

        std::string content = content_rows.at(static_cast<std::size_t>(i));
        if (!m_bg.empty() && m_bg != "\033[49m") {
            std::string target = "\033[0m";
            std::string replacement = "\033[0m" + std::string(m_bg);
            std::size_t pos = 0;
            while ((pos = content.find(target, pos)) != std::string::npos) {
                content.replace(pos, target.length(), replacement);
                pos += replacement.length();
            }
        }

        std::string row_str =
            std::format("{}{}\033[0m{}{}{}{}\033[0m", kThemeBorder, "│", m_bg,
                        format_to_width(content, inner_w), kThemeBorder, "│");

        lines.at(static_cast<std::size_t>(target_y)) =
            overlay_string(lines.at(static_cast<std::size_t>(target_y)), row_str, start_x, box_w);
    }

    // Render Box Bottom Border
    int bot_y = start_y + box_h - 1;
    if (bot_y < max_y && bot_y < static_cast<int>(lines.size())) {
        std::string bot_border = std::format("{}{}{}\033[0m", kThemeBorder,
                                            make_repeated_string("─", box_w), "\033[0m");
        lines.at(static_cast<std::size_t>(bot_y)) =
            overlay_string(lines.at(static_cast<std::size_t>(bot_y)), bot_border, start_x, box_w);
    }
}

}  // namespace simrv::tui
