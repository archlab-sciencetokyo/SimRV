/**
 * @file TuiModal.cpp
 * @brief Implementation of TUI Modal dialog manager and overlay renderer.
 */

#include "simrv/tui/TuiModal.hpp"

#include <algorithm>
#include <format>

#include "simrv/core/Machine.hpp"
#include "simrv/tui/TuiLayoutPolicy.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/modals/AddressModal.hpp"
#include "simrv/tui/modals/BreakpointModal.hpp"
#include "simrv/tui/modals/GlossaryModal.hpp"
#include "simrv/tui/modals/HelpModal.hpp"
#include "simrv/tui/modals/LoadModal.hpp"
#include "simrv/tui/modals/MisaModal.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"
#include "simrv/tui/modals/SettingsModal.hpp"
#include "simrv/tui/modals/StepModal.hpp"
#include "simrv/tui/modals/SystemConfigModal.hpp"
#include "simrv/tui/panels/LeftPane.hpp"

namespace simrv::tui {

namespace {

constexpr std::array<int, 18> kGeneralSettingRows = {4,  5,  6,  7,  8,  9,  12, 13, 14,
                                                     17, 18, 19, 22, 23, 26, 27, 28, 29};
constexpr int kGeneralSettingsContentRows = 30;

[[nodiscard]] auto general_setting_row(int item) -> int {
    return item >= 0 && item < static_cast<int>(kGeneralSettingRows.size())
               ? kGeneralSettingRows[static_cast<size_t>(item)]
               : 0;
}

[[nodiscard]] auto general_setting_at_row(int row) -> int {
    const auto found = std::ranges::find(kGeneralSettingRows, row);
    return found == kGeneralSettingRows.end()
               ? -1
               : static_cast<int>(std::distance(kGeneralSettingRows.begin(), found));
}

}  // namespace

TuiModal::TuiModal(simrv::core::Machine& machine) : machine_(machine) {}

void TuiModal::open(ModalType type, LeftPane* left_pane, uint64_t step_delay_us) {
    active_modal_ = type;
    input_.clear();
    bp_cursor_ = 0;
    switch (type) {
        case ModalType::SetBreakpoint:
        case ModalType::SetWatchpoint:
            modals::BreakpointModal::open(type, input_, machine_, left_pane);
            break;
        case ModalType::SetSpeed:
            modals::StepModal::open(type, input_, step_delay_us);
            break;
        case ModalType::InspectAddress:
            modals::AddressModal::open(input_, left_pane);
            break;
        case ModalType::LoadBinary:
        case ModalType::LoadDiskImage:
            modals::LoadModal::open(type, input_, load_appmode_, machine_);
            break;
        case ModalType::Glossary:
            modals::GlossaryModal::open(glossary_topic_, glossary_scroll_);
            break;
        case ModalType::Settings:
            modals::SettingsModal::open(settings_draft_, machine_);
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

void TuiModal::cycle_settings_tab(int delta) {
    input_.clear();
    modals::SettingsModal::cycle_tab(settings_draft_, delta);
}

void TuiModal::set_settings_tab(uint8_t tab) {
    input_.clear();
    modals::SettingsModal::set_tab(settings_draft_, tab);
}

void TuiModal::move_settings_cursor(int delta) {
    input_.clear();
    modals::SettingsModal::move_cursor(settings_draft_, delta);
}

void TuiModal::adjust_setting_at_cursor(int dir) {
    input_.clear();
    modals::SettingsModal::adjust_setting(settings_draft_, dir, &machine_);
}

void TuiModal::toggle_setting_at_cursor() {
    input_.clear();
    modals::SettingsModal::toggle_setting(settings_draft_, &machine_);
}

void TuiModal::toggle_setting_by_index(int index) {
    settings_draft_.tab_cursor[settings_draft_.active_tab] = index;
    toggle_setting_at_cursor();
}

void TuiModal::push_settings_digit(char c) {
    modals::SettingsModal::push_digit(settings_draft_, input_, c);
}

void TuiModal::pop_settings_digit() { modals::SettingsModal::pop_digit(settings_draft_, input_); }

void TuiModal::apply_settings_misa_profile(int profile_idx) {
    modals::SettingsModal::apply_misa_profile(settings_draft_, profile_idx);
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
    modals::SystemConfigModal::move_cursor(sysconfig_draft_, sysconfig_cursor_, delta);
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

auto TuiModal::remove_bp_at_cursor(
    const std::function<void(const std::string&)>& set_status_override_cb) -> bool {
    std::string removed_msg;
    bool removed = modals::BreakpointModal::remove_at_cursor(bp_cursor_, machine_,
                                                             [&](const std::string& msg) {
                                                                 removed_msg = msg;
                                                                 if (set_status_override_cb)
                                                                     set_status_override_cb(msg);
                                                             });
    if (removed && !removed_msg.empty()) {
        std::string title = (removed_msg.find("watchpoint") != std::string::npos ||
                             removed_msg.find("Watchpoint") != std::string::npos)
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

auto TuiModal::submit(LeftPane* left_pane, std::atomic<uint64_t>& step_delay_us,
                      const std::function<void(TuiRegPage)>& set_reg_page_cb,
                      const std::function<void(const std::string&)>& set_status_override_cb,
                      const std::function<void()>& on_speed_changed_cb) -> bool {
    bool result = false;
    ModalType current_modal = active_modal_;
    auto notice_cb = [this](const std::string& msg) { open_notice("MODAL NOTICE", msg, false); };
    switch (current_modal) {
        case ModalType::SetBreakpoint:
        case ModalType::SetWatchpoint: {
            std::string err_msg;
            auto err_cb = [&](const std::string& msg) { err_msg = msg; };
            result = modals::BreakpointModal::submit(current_modal, input_, machine_, err_cb);
            if (result) {
                std::string target_type =
                    (current_modal == ModalType::SetBreakpoint) ? "PC Breakpoint" : "Watchpoint";
                open_notice("ENTRY CREATED", std::format("Created {} for {}", target_type, input_),
                            false);
            } else if (!err_msg.empty()) {
                std::string msg_text = std::format(
                    "{}\n\nPlease enter a valid hex PC (0x...) or symbol name.", err_msg);
                open_notice("INVALID TARGET", msg_text, true);
                return false;
            }
        } break;
        case ModalType::SetSpeed:
            result = modals::StepModal::submit(current_modal, input_, step_delay_us,
                                               on_speed_changed_cb, notice_cb);
            break;
        case ModalType::InspectAddress: {
            std::string addr_msg;
            auto addr_cb = [&](const std::string& msg) { addr_msg = msg; };
            result = modals::AddressModal::submit(input_, machine_, left_pane, addr_cb);
            if (result && set_reg_page_cb) {
                set_reg_page_cb(TuiRegPage::STACK);
            }
            if (result) {
                open_notice("INSPECT MEMORY",
                            addr_msg.empty() ? "Inspecting memory address." : addr_msg, false);
                return true;
            } else if (!addr_msg.empty()) {
                open_notice("INVALID ADDRESS", addr_msg, true);
                return false;
            }
        } break;
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
                open_notice("PROGRAM LOADED", "Loaded program image. Resetting simulator system...",
                            false);
                return true;
            }
            break;
        case ModalType::Settings: {
            if (settings_draft_.platform_profile !=
                static_cast<uint8_t>(machine_.s_platform_profile)) {
                open_platform_confirm(settings_draft_);
                return false;
            }
            result = modals::SettingsModal::submit(settings_draft_, machine_, set_reg_page_cb);
            if (result) {
                open_notice("SETTINGS SAVED",
                            "Simulator & SMP settings saved and applied successfully.", false);
                return true;
            }
        } break;
        case ModalType::ConfigureMisa:
            result = modals::MisaModal::submit(misa_draft_, machine_, set_status_override_cb);
            if (result) {
                open_notice("MISA CONFIGURATION SAVED",
                            "CPU MISA extensions updated. Resetting simulator system...", false);
                return true;
            }
            break;
        case ModalType::ConfigureSystem:
            result = modals::SystemConfigModal::submit(sysconfig_draft_, machine_);
            if (result) {
                open_notice(
                    "MICROARCHITECTURE SAVED",
                    "Pipeline & microarchitecture configuration saved and applied successfully.",
                    false);
                return true;
            }
            break;
        default:
            break;
    }

    if (active_modal_ == current_modal) {
        active_modal_ = ModalType::None;
        input_.clear();
    }
    return result;
}

void TuiModal::move_glossary_topic(int delta) {
    modals::GlossaryModal::move_topic(glossary_topic_, glossary_scroll_, delta);
}

void TuiModal::set_glossary_topic(int topic) {
    glossary_topic_ = std::clamp(topic, 0, 5);
    glossary_scroll_ = 0;
}

void TuiModal::scroll_glossary_content(int delta) {
    modals::GlossaryModal::scroll_content(glossary_scroll_, delta, 30);
}

void TuiModal::open_notice(const std::string& title, const std::string& message, bool is_error) {
    active_modal_ = ModalType::Notice;
    input_.clear();
    notice_title_ = title;
    notice_message_ = message;
    notice_is_error_ = is_error;
}

void TuiModal::open_platform_confirm(const SettingsDraft& draft) {
    active_modal_ = ModalType::PlatformChangeConfirm;
    input_.clear();
    pending_platform_draft_ = draft;
}

auto TuiModal::handle_click(int x, int y, int term_width, int term_height) -> ModalClickResult {
    if (active_modal_ == ModalType::None) return ModalClickResult::Ignored;

    bool is_help = (active_modal_ == ModalType::Help);
    bool is_glossary = (active_modal_ == ModalType::Glossary);
    bool is_wide_modal =
        (is_help || is_glossary || active_modal_ == ModalType::Settings ||
         active_modal_ == ModalType::ConfigureMisa || active_modal_ == ModalType::ConfigureSystem ||
         active_modal_ == ModalType::Notice || active_modal_ == ModalType::PlatformChangeConfirm);
    const int maximum_width = is_wide_modal ? 78 : 58;
    const int provisional_width = std::min(maximum_width, term_width - 4);
    if (provisional_width < 35) return ModalClickResult::Ignored;

    int est_rows = (active_modal_ == ModalType::Settings && settings_draft_.active_tab == 0)
                       ? kGeneralSettingsContentRows
                   : (active_modal_ == ModalType::ConfigureMisa)   ? 16
                   : (active_modal_ == ModalType::ConfigureSystem) ? 22
                   : (active_modal_ == ModalType::Glossary)        ? 26
                   : (active_modal_ == ModalType::Help)            ? 24
                                                                   : 10;

    const OverlayGeometry overlay =
        calculate_overlay_geometry(term_width, term_height, maximum_width, est_rows);
    if (!overlay.renderable) return ModalClickResult::Ignored;

    int box_w = overlay.width;
    int box_h = overlay.height;
    int start_y = overlay.start_y + 1;  // 1-indexed terminal row
    int start_x = overlay.start_x + 1;  // 1-indexed terminal col

    if (x < start_x || x >= start_x + box_w || y < start_y || y >= start_y + box_h) {
        if (active_modal_ != ModalType::LoadBinary || !machine_.s_fn_memimg.empty()) {
            close();
            return ModalClickResult::Closed;
        }
        return ModalClickResult::Ignored;
    }

    int rel_y = y - start_y;
    int rel_x = x - start_x;

    if (rel_y == 0 || rel_y == box_h - 1) return ModalClickResult::Handled;
    int content_row = rel_y - 1;

    switch (active_modal_) {
        case ModalType::Notice:
            close();
            return ModalClickResult::Closed;

        case ModalType::PlatformChangeConfirm:
            if (content_row >= 5 && content_row <= 6) return ModalClickResult::ReloadRequested;
            if (content_row == 7) return ModalClickResult::DiscardRequested;
            if (content_row >= 8) {
                close();
                return ModalClickResult::Closed;
            }
            return ModalClickResult::Handled;

        case ModalType::Glossary: {
            if (content_row == 0) {
                // Topic buttons: 1.Regs (8) | 2.Pipe (8) | 3.Cache (9) | 4.VM/TLB (10) | 5.BPred
                // (9) | 6.Priv/Trap (13)
                constexpr std::array<int, 6> kTabWidths = {8, 8, 9, 10, 9, 13};
                int current_x = 1;
                for (size_t i = 0; i < kTabWidths.size(); ++i) {
                    if (rel_x >= current_x && rel_x < current_x + kTabWidths.at(i)) {
                        set_glossary_topic(static_cast<int>(i));
                        return ModalClickResult::Handled;
                    }
                    current_x += kTabWidths.at(i) + 1;
                }
            } else if (content_row >= 2 && content_row < box_h - 4) {
                if (rel_x >= box_w / 2) {
                    move_glossary_topic(1);
                } else {
                    move_glossary_topic(-1);
                }
                return ModalClickResult::Handled;
            } else if (rel_y >= box_h - 3) {
                if (rel_x >= box_w - 18) {
                    close();
                    return ModalClickResult::Closed;
                }
                move_glossary_topic(1);
                return ModalClickResult::Handled;
            }
            return ModalClickResult::Handled;
        }

        case ModalType::Settings: {
            if (settings_draft_.active_tab != 0) return ModalClickResult::Handled;
            const int visible_rows = overlay.visible_content_rows;
            const int cursor_row = general_setting_row(settings_draft_.tab_cursor[0]);
            const int scroll_start = kGeneralSettingsContentRows > visible_rows
                                         ? std::clamp(cursor_row - visible_rows / 2, 0,
                                                      kGeneralSettingsContentRows - visible_rows)
                                         : 0;
            const int item_idx = general_setting_at_row(content_row + scroll_start);
            if (item_idx >= 0) {
                settings_draft_.tab_cursor[0] = item_idx;
                if (rel_x > box_w / 2 + 10) {
                    adjust_setting_at_cursor(1);
                } else if (rel_x > box_w / 2 && rel_x <= box_w / 2 + 10) {
                    adjust_setting_at_cursor(-1);
                } else {
                    toggle_setting_at_cursor();
                }
                return ModalClickResult::Handled;
            }
            return ModalClickResult::Handled;
        }

        case ModalType::ConfigureSystem: {
            int sys_idx = std::clamp(content_row - 1, 0, 15);
            sysconfig_cursor_ = sys_idx;
            if (rel_x > box_w / 2 + 10)
                adjust_sysconfig_at_cursor(1);
            else if (rel_x > box_w / 2)
                adjust_sysconfig_at_cursor(-1);
            else
                toggle_sysconfig_at_cursor();
            return ModalClickResult::Handled;
        }

        case ModalType::ConfigureMisa: {
            int misa_idx = std::clamp(content_row - 1, 0, 10);
            misa_cursor_ = misa_idx;
            toggle_misa_at_cursor();
            return ModalClickResult::Handled;
        }

        case ModalType::Help:
            close();
            return ModalClickResult::Closed;

        default:
            break;
    }
    return ModalClickResult::Handled;
}

void TuiModal::render_overlay(std::vector<std::string>& lines, int term_width,
                              int term_height) const {
    if (active_modal_ == ModalType::None || lines.empty()) return;

    const auto meta = modals::get_modal_metadata(active_modal_, notice_is_error_, notice_title_);
    const int maximum_width = meta.is_wide ? 78 : 58;
    const int provisional_width = std::min(maximum_width, term_width - 4);
    if (provisional_width < 35) return;

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

    const std::string& title = meta.title;
    switch (active_modal_) {
        case ModalType::SetBreakpoint:
            modals::BreakpointModal::render(active_modal_, content_rows, input_, &machine_);
            break;
        case ModalType::SetWatchpoint:
            modals::BreakpointModal::render(active_modal_, content_rows, input_, &machine_);
            break;
        case ModalType::ManageBreakpoints:
            modals::BreakpointModal::render(active_modal_, content_rows, input_, &machine_,
                                            bp_cursor_);
            break;
        case ModalType::SetSpeed:
            modals::StepModal::render(active_modal_, content_rows, input_);
            break;
        case ModalType::InspectAddress:
            modals::AddressModal::render(content_rows, input_);
            break;
        case ModalType::LoadBinary:
            modals::LoadModal::render(active_modal_, content_rows, input_, load_appmode_,
                                      staged_binary_path_);
            break;
        case ModalType::LoadDiskImage:
            modals::LoadModal::render(active_modal_, content_rows, input_, load_appmode_,
                                      staged_binary_path_);
            break;
        case ModalType::Glossary:
            modals::GlossaryModal::render(content_rows, add_row, glossary_topic_, glossary_scroll_,
                                          term_height, provisional_width);
            break;
        case ModalType::Settings:
            modals::SettingsModal::render(content_rows, add_row, settings_draft_, machine_);
            break;
        case ModalType::ConfigureMisa:
            modals::MisaModal::render(content_rows, add_row, misa_draft_, misa_cursor_, machine_);
            break;
        case ModalType::ConfigureSystem:
            modals::SystemConfigModal::render(content_rows, add_row, sysconfig_draft_,
                                              sysconfig_cursor_, input_);
            add_row("");
            add_row("  " + modals::build_modal_footer(
                               {{"[Enter]", "apply configuration"}, {"[Esc / q]", "cancel"}}));
            break;
        case ModalType::Help:
            modals::HelpModal::render(content_rows, add_row, term_height, provisional_width);
            break;
        case ModalType::Notice:
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
            add_row("  " + modals::build_modal_footer({{"[Enter / Space / Esc]", "dismiss"}}));
            break;
        case ModalType::PlatformChangeConfirm: {
            add_row("");
            const char* cur_prof =
                (machine_.s_platform_profile == simrv::core::PlatformProfile::Pcie)
                    ? "PCIe (PCIe 1.2 + ECAM)"
                    : ((machine_.s_platform_profile == simrv::core::PlatformProfile::Mmio)
                           ? "MMIO (VirtIO-MMIO v2)"
                           : "Hybrid (PCIe + MMIO)");
            const char* new_prof =
                (pending_platform_draft_.platform_profile == 0)
                    ? "PCIe (PCIe 1.2 + ECAM)"
                    : ((pending_platform_draft_.platform_profile == 1) ? "MMIO (VirtIO-MMIO v2)"
                                                                       : "Hybrid (PCIe + MMIO)");
            add_row(std::format("  {}Platform profile changed:\033[0m", kThemeText));
            add_row(std::format("    {}Current:\033[0m \033[1;36m{}\033[0m", kThemeText, cur_prof));
            add_row(std::format("    {}Target :\033[0m \033[1;32m{}\033[0m", kThemeText, new_prof));
            add_row("");
            add_row("  Changing the platform bus topology requires regenerating the");
            add_row("  Device Tree (FDT) and restarting CPU execution state.");
            add_row("");
            add_row(
                std::format("  {}[R / Enter]\033[0m \033[1;32mReload Simulator Now\033[0m (Apply & "
                            "Reset System)",
                            kThemeMint));
            add_row(
                std::format("  {}[D / Space]\033[0m \033[1;33mDiscard Platform Change\033[0m (Keep "
                            "Current Profile)",
                            kThemePeach));
            add_row("  " +
                    modals::build_modal_footer({{"[Esc / q]", "Cancel & Return to Settings"}}));
        } break;
        default:
            break;
    }

    if (content_rows.empty()) return;

    int cursor_row = 0;
    switch (active_modal_) {
        case ModalType::Settings:
            cursor_row = settings_draft_.active_tab == 0
                             ? general_setting_row(settings_draft_.tab_cursor[0])
                             : 3 + settings_draft_.tab_cursor[settings_draft_.active_tab];
            break;
        case ModalType::ConfigureMisa:
            cursor_row = 2 + misa_cursor_;
            break;
        case ModalType::ConfigureSystem:
            cursor_row = 3 + sysconfig_cursor_;
            break;
        case ModalType::ManageBreakpoints:
            cursor_row = 2 + bp_cursor_;
            break;
        default:
            cursor_row = 0;
            break;
    }

    const int available_height = std::min(term_height, static_cast<int>(lines.size()));
    const OverlayGeometry overlay = calculate_overlay_geometry(
        term_width, available_height, maximum_width, static_cast<int>(content_rows.size()));
    if (!overlay.renderable) return;
    const int box_w = overlay.width;
    const int box_h = overlay.height;
    const int start_y = overlay.start_y;
    const int start_x = overlay.start_x;
    int inner_w = box_w - 2;

    const int total_rows = static_cast<int>(content_rows.size());
    const int visible_rows = overlay.visible_content_rows;
    int scroll_start = 0;
    if (total_rows > visible_rows) {
        scroll_start = std::clamp(cursor_row - visible_rows / 2, 0, total_rows - visible_rows);
    }

    // Render Box Top Border
    if (start_y < static_cast<int>(lines.size())) {
        std::string title_fmt =
            std::format("\033[1m{}{}\033[0m{}{}", kThemeMint, title, kThemeBorder, m_bg);
        int title_len = get_display_width(title);
        int dash_len = inner_w - title_len;
        if (dash_len < 0) dash_len = 0;
        int left_dash = dash_len / 2;
        int right_dash = dash_len - left_dash;
        const auto style = get_active_theme_style();
        auto const& glyphs = get_theme_glyphs(style);

        std::string top_border =
            std::format("{}{}{}{}{}{}{}\033[0m", kThemeBorder, m_bg,
                        make_repeated_string(glyphs.horiz, left_dash + 1), title_fmt, kThemeBorder,
                        make_repeated_string(glyphs.horiz, right_dash + 1), "\033[0m");

        lines.at(static_cast<std::size_t>(start_y)) =
            overlay_string(lines.at(static_cast<std::size_t>(start_y)), top_border, start_x, box_w);
    }

    // Render Box Content Rows with background color persistence & viewport scrolling
    const auto style = get_active_theme_style();
    auto const& glyphs = get_theme_glyphs(style);
    for (int i = 0; i < visible_rows; ++i) {
        int target_y = start_y + 1 + i;
        int content_idx = scroll_start + i;

        std::string content;
        if (content_idx < total_rows) {
            content = content_rows.at(static_cast<std::size_t>(content_idx));
        }

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
            std::format("{}{}\033[0m{}{}{}{}\033[0m", kThemeBorder, glyphs.vert, m_bg,
                        format_to_width(content, inner_w), kThemeBorder, glyphs.vert);

        lines.at(static_cast<std::size_t>(target_y)) =
            overlay_string(lines.at(static_cast<std::size_t>(target_y)), row_str, start_x, box_w);
    }

    // Render Box Bottom Border
    int bot_y = start_y + box_h - 1;
    if (bot_y < static_cast<int>(lines.size())) {
        std::string bot_border = std::format("{}{}{}{}\033[0m", kThemeBorder, m_bg,
                                             make_repeated_string(glyphs.horiz, box_w), "\033[0m");
        lines.at(static_cast<std::size_t>(bot_y)) =
            overlay_string(lines.at(static_cast<std::size_t>(bot_y)), bot_border, start_x, box_w);
    }
}

}  // namespace simrv::tui
