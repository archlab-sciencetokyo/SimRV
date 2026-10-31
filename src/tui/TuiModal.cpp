/**
 * @file TuiModal.cpp
 * @brief Implementation of TUI Modal dialog manager and overlay renderer.
 */

#include "simrv/tui/TuiModal.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <numeric>
#include <sstream>
#include <string_view>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/debug/BreakpointManager.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/tui/TuiLayoutPolicy.hpp"
#include "simrv/tui/TuiMission.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/framework/Modal.hpp"
#include "simrv/tui/framework/Text.hpp"
#include "simrv/tui/modals/AddressModal.hpp"
#include "simrv/tui/modals/BreakpointModal.hpp"
#include "simrv/tui/modals/ConfirmModal.hpp"
#include "simrv/tui/modals/FilePickerModal.hpp"
#include "simrv/tui/modals/GlossaryModal.hpp"
#include "simrv/tui/modals/HartModal.hpp"
#include "simrv/tui/modals/HelpModal.hpp"
#include "simrv/tui/modals/LayoutPresetsModal.hpp"
#include "simrv/tui/modals/LoadModal.hpp"
#include "simrv/tui/modals/LogModal.hpp"
#include "simrv/tui/modals/MemoryModal.hpp"
#include "simrv/tui/modals/MisaModal.hpp"
#include "simrv/tui/modals/MissionObjectiveModal.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"
#include "simrv/tui/modals/NoticeModal.hpp"
#include "simrv/tui/modals/SettingsModal.hpp"
#include "simrv/tui/modals/StepModal.hpp"
#include "simrv/tui/modals/SystemConfigModal.hpp"
#include "simrv/tui/modals/ToolPickerModal.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"

namespace simrv::tui {

namespace {}  // namespace

TuiModal::TuiModal(simrv::core::Machine& machine) : machine_(machine) {}

void TuiModal::open(ModalType type, InspectorPane* inspector_pane, uint64_t step_delay_us) {
    active_modal_ = type;
    rendered_box_width_ = 0;
    input_.clear();
    path_completion_preview_.clear();
    bp_cursor_ = 0;
    switch (type) {
        case ModalType::SetBreakpoint:
        case ModalType::SetWatchpoint:
            modals::BreakpointModal::open(type, input_, machine_, inspector_pane);
            active_modal_impl_ = std::make_unique<modals::BreakpointModal>(this, &machine_, type,
                                                                           input_, bp_cursor_);
            break;
        case ModalType::ManageBreakpoints:
            active_modal_impl_ = std::make_unique<modals::BreakpointModal>(this, &machine_, type,
                                                                           input_, bp_cursor_);
            break;
        case ModalType::SetSpeed:
            modals::StepModal::open(type, input_, step_delay_us);
            active_modal_impl_ = std::make_unique<modals::StepModal>(input_);
            break;
        case ModalType::InspectAddress:
            modals::AddressModal::open(input_, inspector_pane);
            active_modal_impl_ = std::make_unique<modals::AddressModal>(input_);
            break;
        case ModalType::LoadBinary:
            load_buildroot_mode_ = false;
            modals::LoadModal::open(type, input_, load_appmode_, machine_);
            active_modal_impl_ = std::make_unique<modals::LoadModal>(
                this, &machine_, type, input_, load_appmode_, load_buildroot_mode_,
                staged_binary_path_, path_completion_preview_);
            break;
        case ModalType::LoadDiskImage:
            modals::LoadModal::open(type, input_, load_appmode_, machine_);
            active_modal_impl_ = std::make_unique<modals::LoadModal>(
                this, &machine_, type, input_, load_appmode_, load_buildroot_mode_,
                staged_binary_path_, path_completion_preview_);
            break;
        case ModalType::Glossary:
            modals::GlossaryModal::open(glossary_topic_, glossary_scroll_);
            active_modal_impl_ =
                std::make_unique<modals::GlossaryModal>(glossary_topic_, glossary_scroll_);
            break;
        case ModalType::Settings:
            modals::SettingsModal::open(settings_draft_, machine_, density_mode_,
                                        get_active_theme_style());
            active_modal_impl_ =
                std::make_unique<modals::SettingsModal>(this, &machine_, settings_draft_);
            break;
        case ModalType::Help:
            reset_help_scroll();
            active_modal_impl_ = std::make_unique<modals::HelpModal>(help_scroll_);
            break;
        case ModalType::MemoryDump:
            open_memory_dump(0, inspector_pane);
            break;
        case ModalType::SwitchHart:
            open_switch_hart(0);
            break;
        case ModalType::LogViewer:
            open_log_viewer();
            break;
        case ModalType::LayoutPresets:
            active_modal_impl_ = std::make_unique<modals::LayoutPresetsModal>(this, preset_cursor_);
            break;
        case ModalType::MissionObjective:
            open_mission_objective();
            break;
        default:
            active_modal_impl_.reset();
            break;
    }
}

void TuiModal::open_save_cpu_config() {
    active_modal_ = ModalType::SaveCpuConfig;
    rendered_box_width_ = 0;
    path_completion_preview_.clear();
    const auto preset =
        static_cast<simrv::pipeline::CpuModelPreset>(settings_draft_.sys_config.preset);
    const auto name = simrv::pipeline::cpu_model_preset_name(preset);
    input_ = std::format("configs/models/{}.cfg", name);
    active_modal_impl_ = std::make_unique<modals::LoadModal>(
        this, &machine_, active_modal_, input_, load_appmode_, load_buildroot_mode_,
        staged_binary_path_, path_completion_preview_);
}

void TuiModal::open_load_cpu_config() {
    active_modal_ = ModalType::LoadCpuConfig;
    rendered_box_width_ = 0;
    path_completion_preview_.clear();
    const auto& name = machine_.primary_hart().cpu_model_config.name;
    input_ = name.empty() ? "configs/models/rvcomp.cfg" : name;
    active_modal_impl_ = std::make_unique<modals::LoadModal>(
        this, &machine_, active_modal_, input_, load_appmode_, load_buildroot_mode_,
        staged_binary_path_, path_completion_preview_);
}

void TuiModal::open_file_picker() {
    if (active_modal_ != ModalType::LoadBinary && active_modal_ != ModalType::LoadDiskImage &&
        active_modal_ != ModalType::LoadCpuConfig)
        return;
    file_picker_return_modal_ = active_modal_;
    file_picker_return_input_ = input_;
    path_completion_preview_.clear();
    const std::filesystem::path typed(input_);
    std::error_code directory_error;
    const bool typed_is_directory =
        !typed.empty() && std::filesystem::is_directory(typed, directory_error);
    const bool has_trailing_separator =
        !input_.empty() && input_.back() == std::filesystem::path::preferred_separator;
    const bool navigate_into_typed_path = typed_is_directory || has_trailing_separator;
    const auto typed_parent =
        navigate_into_typed_path
            ? typed
            : (typed.has_parent_path() ? typed.parent_path() : std::filesystem::path("."));
    std::error_code path_error;
    file_picker_directory_ = std::filesystem::absolute(typed_parent, path_error);
    if (path_error) file_picker_directory_ = std::filesystem::current_path();
    file_picker_directory_ = file_picker_directory_.lexically_normal();
    if (file_picker_directory_ != file_picker_directory_.root_path() &&
        file_picker_directory_.filename().empty()) {
        file_picker_directory_ = file_picker_directory_.parent_path();
    }
    file_picker_filter_ = navigate_into_typed_path ? std::string{} : typed.filename().string();
    file_picker_select_directory_ = load_buildroot_mode_;
    active_modal_ = ModalType::LoadFilePicker;
    active_modal_impl_ = std::make_unique<modals::FilePickerModal>(*this);
    refresh_file_picker();
}

void TuiModal::refresh_file_picker() {
    namespace fs = std::filesystem;
    file_picker_entries_.clear();
    std::error_code error;
    auto case_insensitive_contains = [](std::string_view h, std::string_view n) {
        if (n.empty()) return true;
        auto it = std::search(h.begin(), h.end(), n.begin(), n.end(), [](char c1, char c2) {
            return std::tolower(static_cast<unsigned char>(c1)) ==
                   std::tolower(static_cast<unsigned char>(c2));
        });
        return it != h.end();
    };
    for (fs::directory_iterator it(file_picker_directory_, error), end; !error && it != end;
         it.increment(error)) {
        const auto filename = it->path().filename().string();
        if (!case_insensitive_contains(filename, file_picker_filter_)) continue;
        file_picker_entries_.push_back(it->path());
    }
    std::ranges::sort(file_picker_entries_, [](const fs::path& left, const fs::path& right) {
        std::error_code le, re;
        const bool ld = fs::is_directory(left, le), rd = fs::is_directory(right, re);
        if (ld != rd) return ld;
        return left.filename().string() < right.filename().string();
    });
    if (file_picker_filter_.empty() &&
        file_picker_directory_ != file_picker_directory_.root_path()) {
        file_picker_entries_.insert(file_picker_entries_.begin(),
                                    (file_picker_directory_ / "..").lexically_normal());
    }
    if (file_picker_select_directory_)
        file_picker_entries_.insert(file_picker_entries_.begin(), ".");
    file_picker_cursor_ = std::clamp(
        file_picker_cursor_, 0, std::max(0, static_cast<int>(file_picker_entries_.size()) - 1));
}

void TuiModal::move_file_picker_cursor(int delta) {
    if (file_picker_entries_.empty()) return;
    const int count = static_cast<int>(file_picker_entries_.size());
    file_picker_cursor_ = (file_picker_cursor_ + delta + count) % count;
}

void TuiModal::accept_file_picker_entry() {
    namespace fs = std::filesystem;
    if (file_picker_entries_.empty()) return;
    const auto selected = file_picker_entries_[static_cast<size_t>(file_picker_cursor_)];
    const auto parent_entry = (file_picker_directory_ / "..").lexically_normal();
    if (selected == parent_entry) {
        file_picker_directory_ = selected;
        file_picker_filter_.clear();
        file_picker_cursor_ = 0;
        refresh_file_picker();
        return;
    }
    if (file_picker_select_directory_ && selected == fs::path(".")) {
        input_ = file_picker_directory_.lexically_normal().string();
        active_modal_ = file_picker_return_modal_;
        active_modal_impl_ = std::make_unique<modals::LoadModal>(
            this, &machine_, active_modal_, input_, load_appmode_, load_buildroot_mode_,
            staged_binary_path_, path_completion_preview_);
        file_picker_entries_.clear();
        return;
    }
    if (file_picker_select_directory_ && selected == file_picker_directory_) {
        input_ = selected.lexically_normal().string();
        active_modal_ = file_picker_return_modal_;
        active_modal_impl_ = std::make_unique<modals::LoadModal>(
            this, &machine_, active_modal_, input_, load_appmode_, load_buildroot_mode_,
            staged_binary_path_, path_completion_preview_);
        file_picker_entries_.clear();
        return;
    }
    std::error_code error;
    if (fs::is_directory(selected, error) && !error) {
        file_picker_directory_ = selected;
        file_picker_filter_.clear();
        file_picker_cursor_ = 0;
        refresh_file_picker();
        return;
    }
    std::error_code absolute_error;
    auto absolute_selected = fs::absolute(selected, absolute_error);
    input_ = (absolute_error ? selected : absolute_selected).lexically_normal().string();
    active_modal_ = file_picker_return_modal_;
    active_modal_impl_ = std::make_unique<modals::LoadModal>(
        this, &machine_, active_modal_, input_, load_appmode_, load_buildroot_mode_,
        staged_binary_path_, path_completion_preview_);
    file_picker_entries_.clear();
}

void TuiModal::file_picker_input(uint8_t byte, TuiKey key) {
    if (key == TuiKey::Esc || byte == 27 || byte == 'q' || byte == 'Q') {
        input_ = file_picker_return_input_;
        active_modal_ = file_picker_return_modal_;
        active_modal_impl_ = std::make_unique<modals::LoadModal>(
            this, &machine_, active_modal_, input_, load_appmode_, load_buildroot_mode_,
            staged_binary_path_, path_completion_preview_);
        file_picker_entries_.clear();
        return;
    }
    if (key == TuiKey::Enter || key == TuiKey::Newline) {
        accept_file_picker_entry();
    } else if (key == TuiKey::BackTab || key == TuiKey::Tab) {
        move_file_picker_cursor(key == TuiKey::Tab ? 1 : -1);
    } else if (key == TuiKey::Backspace || byte == 8 || byte == 127) {
        if (!file_picker_filter_.empty()) {
            file_picker_filter_.pop_back();
        } else if (file_picker_directory_.has_parent_path()) {
            file_picker_directory_ = file_picker_directory_.parent_path();
        }
        file_picker_cursor_ = 0;
        refresh_file_picker();
    } else if (key == TuiKey::CtrlP || byte == 16) {
        file_picker_cursor_ = std::max(0, file_picker_cursor_ - 1);
    } else if (key == TuiKey::CtrlN || byte == 14) {
        move_file_picker_cursor(1);
    } else if (byte >= 32 && byte <= 126) {
        file_picker_filter_.push_back(static_cast<char>(byte));
        file_picker_cursor_ = 0;
        refresh_file_picker();
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

void TuiModal::apply_settings_isa_preset(int preset_idx) {
    modals::SettingsModal::apply_isa_preset(settings_draft_, preset_idx);
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
    if (removed && !removed_msg.empty() &&
        removed_msg.find("managed by the mission") == std::string::npos) {
        std::string title = (removed_msg.find("watchpoint") != std::string::npos ||
                             removed_msg.find("Watchpoint") != std::string::npos)
                                ? "WATCHPOINT REMOVED"
                                : "BREAKPOINT REMOVED";
        open_notice(title, removed_msg, false);
    }
    return removed;
}

auto TuiModal::tool_filter() const -> modals::ToolFilter {
    return modals::ToolPickerModal::make_filter(machine_, selected_hart_,
                                                display_graphics_supported_);
}

void TuiModal::open_tool_picker(int slot_idx, TuiRegPage current_page, int num_slots) {
    active_modal_ = ModalType::ToolPicker;
    tool_picker_num_slots_ = std::max(1, num_slots);
    tool_picker_slot_ = std::clamp(slot_idx, 0, tool_picker_num_slots_ - 1);
    tool_picker_current_page_ = current_page;
    tool_picker_cursor_ = 0;
    const auto list = modals::ToolPickerModal::all_tools(tool_filter());
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].page == current_page) {
            tool_picker_cursor_ = static_cast<int>(i);
            break;
        }
    }
    active_modal_impl_ = std::make_unique<modals::ToolPickerModal>(
        tool_picker_slot_, tool_picker_cursor_, tool_picker_current_page_, tool_picker_num_slots_,
        tool_filter());
}

void TuiModal::move_tool_picker_cursor(int delta) {
    const int count = static_cast<int>(modals::ToolPickerModal::total_tool_count(tool_filter()));
    if (count <= 0) return;
    tool_picker_cursor_ = (tool_picker_cursor_ + delta) % count;
    if (tool_picker_cursor_ < 0) tool_picker_cursor_ += count;
}

void TuiModal::set_tool_picker_cursor(int cursor) {
    const int count = static_cast<int>(modals::ToolPickerModal::total_tool_count(tool_filter()));
    if (count <= 0) return;
    tool_picker_cursor_ = std::clamp(cursor, 0, count - 1);
}

void TuiModal::set_tool_picker_slot(int slot) {
    tool_picker_slot_ = std::clamp(slot, 0, std::max(0, tool_picker_num_slots_ - 1));
}

void TuiModal::cycle_tool_picker_slot(int num_slots, int direction) {
    int const total = (num_slots > 0) ? num_slots : tool_picker_num_slots_;
    if (total <= 0) return;
    int const step = (direction >= 0) ? 1 : (total - 1);
    tool_picker_slot_ = (tool_picker_slot_ + step) % total;
}

auto TuiModal::get_selected_tool_page() const -> TuiRegPage {
    auto page = modals::ToolPickerModal::tool_at_index(static_cast<size_t>(tool_picker_cursor_),
                                                       tool_filter());
    return page.value_or(TuiRegPage::GPR);
}

void TuiModal::open_memory_dump(simrv::xlen::Address addr, InspectorPane* inspector_pane) {
    active_modal_ = ModalType::MemoryDump;
    rendered_box_width_ = 0;
    input_.clear();
    modals::MemoryModal::open(addr, machine_, inspector_pane);
    active_modal_impl_ = std::make_unique<modals::MemoryModal>(this, &machine_, input_);
}

void TuiModal::open_switch_hart(size_t current_hart) {
    active_modal_ = ModalType::SwitchHart;
    rendered_box_width_ = 0;
    input_.clear();
    modals::HartModal::open(current_hart, machine_);
    active_modal_impl_ = std::make_unique<modals::HartModal>(machine_);
}

void TuiModal::move_memory_cursor(int delta_byte, int delta_row) {
    modals::MemoryModal::move_cursor(delta_byte, delta_row, machine_);
}

void TuiModal::scroll_memory_page(int pages) { modals::MemoryModal::scroll_page(pages, machine_); }

void TuiModal::toggle_memory_mode() { modals::MemoryModal::toggle_addressing_mode(); }

void TuiModal::open_log_viewer() {
    active_modal_ = ModalType::LogViewer;
    rendered_box_width_ = 0;
    input_.clear();
    if (log_buffer_) modals::LogModal::open(*log_buffer_);
    active_modal_impl_ = std::make_unique<modals::LogModal>(this, log_buffer_);
}

void TuiModal::open_mission_objective() {
    if (mission_ == nullptr || !mission_->guidance()) return;
    active_modal_ = ModalType::MissionObjective;
    mission_hint_level_ = 0;
    rendered_box_width_ = 0;
    input_.clear();
    active_modal_impl_ = std::make_unique<modals::MissionObjectiveModal>(this, &machine_, mission_,
                                                                         mission_hint_level_);
}

void TuiModal::cycle_mission_hint() noexcept {
    const auto guidance = mission_ != nullptr ? mission_->guidance() : std::nullopt;
    if (guidance && !guidance->hint.empty()) {
        mission_hint_level_ = static_cast<uint8_t>((mission_hint_level_ + 1) % 3);
    } else {
        mission_hint_level_ = mission_hint_level_ == 2 ? 0 : 2;
    }
}

void TuiModal::scroll_log_viewer(int delta, int total_lines, int visible_lines) {
    modals::LogModal::scroll(delta, total_lines, visible_lines);
}

void TuiModal::scroll_log_viewer_page(int pages, int total_lines, int visible_lines) {
    modals::LogModal::scroll_page(pages, total_lines, visible_lines);
}

void TuiModal::toggle_log_follow() { modals::LogModal::toggle_follow(); }

void TuiModal::cycle_log_level() { modals::LogModal::cycle_filter_level(); }

void TuiModal::set_log_filter_level(std::optional<simrv::log::Level> level) {
    modals::LogModal::set_filter_level(level);
}

void TuiModal::move_hart_cursor(int delta) { modals::HartModal::move_cursor(delta, machine_); }

void TuiModal::close() {
    active_modal_ = ModalType::None;
    active_modal_impl_.reset();
    rendered_box_width_ = 0;
    input_.clear();
    path_completion_preview_.clear();
}

auto TuiModal::submit(
    InspectorPane* inspector_pane, std::atomic<uint64_t>& step_delay_us,
    const std::function<void(TuiRegPage)>& set_reg_page_cb,
    const std::function<void(const std::string&)>& set_status_override_cb,
    const std::function<void()>& on_speed_changed_cb,
    const std::function<void(framework::DensityMode, TuiThemeStyle)>& on_presentation_changed_cb)
    -> bool {
    (void)set_status_override_cb;
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
            result = modals::AddressModal::submit(input_, machine_, inspector_pane, addr_cb);
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
            result = modals::LoadModal::submit(
                current_modal, input_, load_appmode_, load_buildroot_mode_, machine_,
                staged_binary_path_, staged_mode_change_, staged_target_appmode_, notice_cb);
            if (current_modal == ModalType::LoadBinary && staged_mode_change_ && !load_appmode_) {
                active_modal_ = ModalType::LoadDiskImage;
                input_ = machine_.disk_path();
                active_modal_impl_ = std::make_unique<modals::LoadModal>(
                    this, &machine_, active_modal_, input_, load_appmode_, load_buildroot_mode_,
                    staged_binary_path_, path_completion_preview_);
                return false;
            }
            if (result) {
                open_notice(load_buildroot_mode_ ? "BUILDROOT IMAGE LOADED" : "PROGRAM LOADED",
                            load_buildroot_mode_
                                ? "Loaded Buildroot firmware, DTB, and root filesystem."
                                : "Loaded program image. Resetting simulator system...",
                            false);
                return true;
            }
            break;
        case ModalType::Settings: {
            result = modals::SettingsModal::submit(
                settings_draft_, machine_, set_reg_page_cb,
                [this](simrv::core::MachineConfig next, SysConfigDraft, bool cycle_accurate) {
                    open_reconfiguration_confirm(std::move(next), "Machine settings changed",
                                                 cycle_accurate);
                });
            if (on_presentation_changed_cb) {
                on_presentation_changed_cb(settings_draft_.density_mode,
                                           settings_draft_.theme_style);
            }
            if (result) {
                open_notice("SETTINGS SAVED",
                            "Simulator & SMP settings saved and applied successfully.", false);
                return true;
            }
        } break;
        case ModalType::LayoutPresets:
        case ModalType::ToolPicker:
            result = true;
            break;
        case ModalType::SaveCpuConfig: {
            if (input_.empty()) return false;
            std::string path = input_;
            if (!path.ends_with(".cfg") && !path.ends_with(".ini")) {
                path += ".cfg";
            }
            auto model = machine_.primary_hart().cpu_model_config;
            const auto selected_preset =
                static_cast<simrv::pipeline::CpuModelPreset>(settings_draft_.sys_config.preset);
            if (selected_preset != simrv::pipeline::CpuModelPreset::Custom) {
                model = simrv::pipeline::make_cpu_model_preset(selected_preset);
            }
            model.pipeline.pipeline_type = static_cast<simrv::pipeline::PipelineType>(
                settings_draft_.sys_config.pipeline_type);
            model.pipeline.mul_latency = settings_draft_.sys_config.mul_latency;
            model.pipeline.div_latency = settings_draft_.sys_config.div_latency;
            model.pipeline.fp_alu_latency = settings_draft_.sys_config.fp_alu_latency;
            model.pipeline.fp_div_latency = settings_draft_.sys_config.fp_div_latency;
            model.pipeline.csr_flush_penalty = settings_draft_.sys_config.csr_flush_penalty;
            model.pipeline.fence_flush_penalty = settings_draft_.sys_config.fence_flush_penalty;
            model.pipeline.enable_forwarding = settings_draft_.sys_config.enable_forwarding;
            model.pipeline.branch_predictor.type =
                static_cast<simrv::pipeline::BranchPredictorType>(
                    settings_draft_.sys_config.bpred_type);
            model.pipeline.branch_predictor.bht_entries = settings_draft_.sys_config.bht_entries;
            model.pipeline.branch_predictor.btb_entries = settings_draft_.sys_config.btb_entries;
            model.pipeline.branch_predictor.ras_entries = settings_draft_.sys_config.ras_entries;

            const std::string model_name = std::filesystem::path(path).stem().string();
            if (simrv::core::save_cpu_config(path, model, model_name)) {
                open_notice("CPU MODEL SAVED", std::format("Configuration saved to {}", path),
                            false);
                return true;
            } else {
                open_notice("SAVE FAILED", std::format("Cannot write configuration to {}", path),
                            true);
                return false;
            }
        }
        case ModalType::LoadCpuConfig: {
            if (input_.empty()) return false;
            const auto resolved = simrv::core::resolve_cpu_model_path(input_);
            const auto target_path = resolved.value_or(std::filesystem::path(input_));
            simrv::pipeline::CpuModelConfig loaded{};
            if (!simrv::core::load_cpu_config(target_path, loaded)) {
                open_notice(
                    "LOAD FAILED",
                    std::format("Failed to load CPU model '{}' (check architecture)", input_),
                    true);
                return false;
            }
            std::ostringstream current_text;
            std::ostringstream loaded_text;
            simrv::core::serialize_cpu_config(machine_.primary_hart().cpu_model_config,
                                              current_text);
            simrv::core::serialize_cpu_config(loaded, loaded_text);
            if (current_text.str() == loaded_text.str()) {
                open_notice("CPU MODEL UNCHANGED",
                            std::format("{} is already active; no restart is needed.", loaded.name),
                            false);
                return true;
            }
            auto next = machine_.configuration();
            next.cpu_model_config_override = std::move(loaded);
            next.files.config_path.clear();
            open_reconfiguration_confirm(std::move(next), std::format("CPU model: {}", input_));
            return false;
        }
        case ModalType::MemoryDump: {
            result = modals::MemoryModal::submit_input(input_, machine_, inspector_pane,
                                                       set_status_override_cb);
            if (result) {
                input_.clear();
            }
            return result;
        }
        case ModalType::SwitchHart: {
            size_t selected = 0;
            if (modals::HartModal::submit(machine_, selected)) {
                if (inspector_pane) {
                    inspector_pane->set_selected_hart(selected);
                    inspector_pane->update_cache();
                }
                set_status_override_cb(
                    std::format("Active telemetry switched to Hart {}", selected));
                active_modal_ = ModalType::None;
                return true;
            }
            return false;
        }
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

void TuiModal::scroll_help(int delta) { help_scroll_ = std::max(0, help_scroll_ + delta); }

void TuiModal::scroll_to_top() {
    if (active_modal_impl_) {
        active_modal_impl_->scroll_to_top();
    }
}

void TuiModal::scroll_to_bottom() {
    if (active_modal_impl_) {
        active_modal_impl_->scroll_to_bottom();
    }
}

void TuiModal::handle_wheel(int delta) {
    if (active_modal_impl_) {
        active_modal_impl_->handle_wheel(delta);
    }
}

auto TuiModal::handle_horizontal(int delta) -> bool {
    if (active_modal_impl_) {
        return active_modal_impl_->handle_horizontal(delta);
    }
    return false;
}

void TuiModal::open_notice(const std::string& title, const std::string& message, bool is_error) {
    active_modal_ = ModalType::Notice;
    input_.clear();
    notice_title_ = title;
    notice_message_ = message;
    notice_is_error_ = is_error;
    active_modal_impl_ = std::make_unique<modals::NoticeModal>(this, &machine_, notice_title_,
                                                               notice_message_, notice_is_error_);
}

void TuiModal::open_platform_confirm(const SettingsDraft& draft) {
    active_modal_ = ModalType::PlatformChangeConfirm;
    input_.clear();
    pending_platform_draft_ = draft;
    active_modal_impl_ = std::make_unique<modals::ConfirmModal>(this, &machine_, active_modal_,
                                                                &pending_platform_draft_, "");
}

void TuiModal::open_reconfiguration_confirm(simrv::core::MachineConfig config, std::string summary,
                                            std::optional<bool> cycle_accurate) {
    active_modal_ = ModalType::ConfigurationChangeConfirm;
    input_.clear();
    pending_reconfiguration_ = std::move(config);
    pending_cycle_accurate_ = cycle_accurate;
    pending_reconfiguration_summary_ = std::move(summary);
    active_modal_impl_ = std::make_unique<modals::ConfirmModal>(
        this, &machine_, active_modal_, nullptr, pending_reconfiguration_summary_);
}

auto TuiModal::apply_pending_reconfiguration() -> std::expected<void, std::string> {
    if (!pending_reconfiguration_) return std::unexpected("No simulator configuration is pending");
    if (const auto valid = pending_reconfiguration_->validate(); !valid)
        return std::unexpected(valid.error());
    if (pending_cycle_accurate_) {
        const auto engine = simrv::core::select_execution_engine(*pending_cycle_accurate_,
                                                                 simrv::core::InteractionMode::Tui);
        machine_.switch_execution_engine_sync(engine);
    }
    return machine_.stage_reconfiguration(*pending_reconfiguration_);
}

auto TuiModal::handle_click(int x, int y, int term_width, int term_height) -> ModalClickResult {
    if (active_modal_ == ModalType::None && !active_modal_impl_) return ModalClickResult::Ignored;

    bool is_help = (active_modal_ == ModalType::Help);
    bool is_glossary = (active_modal_ == ModalType::Glossary);
    bool is_wide_modal =
        (is_help || is_glossary || active_modal_ == ModalType::Settings ||
         active_modal_ == ModalType::Notice || active_modal_ == ModalType::PlatformChangeConfirm ||
         active_modal_ == ModalType::ConfigurationChangeConfirm ||
         active_modal_ == ModalType::LayoutPresets || active_modal_ == ModalType::ToolPicker ||
         active_modal_ == ModalType::LogViewer || active_modal_ == ModalType::MemoryDump);
    const int fallback_width = is_wide_modal ? 78 : 58;
    const bool has_rendered_geometry = rendered_box_width_ > 0 &&
                                       rendered_term_width_ == term_width &&
                                       rendered_term_height_ == term_height;
    const int maximum_width = has_rendered_geometry ? rendered_box_width_ : fallback_width;
    const int provisional_width = std::min(maximum_width, term_width - 4);
    if (provisional_width < 35) return ModalClickResult::Ignored;

    int est_rows = (active_modal_ == ModalType::Settings && settings_draft_.active_tab == 0)
                       ? modals::kGeneralSettingsContentRows
                   : (active_modal_ == ModalType::Glossary)   ? 26
                   : (active_modal_ == ModalType::Help)       ? 24
                   : (active_modal_ == ModalType::LogViewer)  ? 24
                   : (active_modal_ == ModalType::MemoryDump) ? 18
                   : (active_modal_ == ModalType::ToolPicker)
                       ? modals::ToolPickerModal::content_row_count(tool_filter())
                   : (active_modal_ == ModalType::LayoutPresets) ? 12
                                                                 : 10;

    OverlayGeometry overlay =
        calculate_overlay_geometry(term_width, term_height, maximum_width, est_rows);
    if (has_rendered_geometry) {
        overlay.width = rendered_box_width_;
        overlay.height = rendered_box_height_;
        overlay.start_x = rendered_start_x_;
        overlay.start_y = rendered_start_y_;
        overlay.visible_content_rows = std::max(0, overlay.height - 2);
        overlay.renderable = true;
    }
    if (!overlay.renderable) return ModalClickResult::Ignored;

    int box_w = overlay.width;
    int box_h = overlay.height;
    int start_y = overlay.start_y + 1;  // 1-indexed terminal row
    int start_x = overlay.start_x + 1;  // 1-indexed terminal col

    if (x < start_x || x >= start_x + box_w || y < start_y || y >= start_y + box_h) {
        if (active_modal_impl_ || active_modal_ != ModalType::LoadBinary ||
            !machine_.binary_path().empty()) {
            close();
            return ModalClickResult::Closed;
        }
        return ModalClickResult::Ignored;
    }

    int rel_y = y - start_y;
    int rel_x = x - start_x;

    if (rel_y == 0 || rel_y == box_h - 1) return ModalClickResult::Handled;

    for (auto const& control_row : rendered_control_rows_) {
        if (control_row.relative_y != rel_y) continue;
        int const control_column = rel_x - 1;
        for (std::size_t action = 0; action < control_row.spans.size(); ++action) {
            if (!control_row.spans[action].contains(control_column)) continue;
            if (active_modal_impl_) {
                auto act =
                    active_modal_impl_->handle_control_click(control_row.content_row, action);
                if (act == framework::ModalAction::Close) {
                    close();
                    return ModalClickResult::Closed;
                }
                if (act == framework::ModalAction::Submit) {
                    return ModalClickResult::Submit;
                }
                if (act == framework::ModalAction::ReloadRequested) {
                    return ModalClickResult::ReloadRequested;
                }
                if (act == framework::ModalAction::DiscardRequested) {
                    return ModalClickResult::DiscardRequested;
                }
                return ModalClickResult::Handled;
            }
        }
    }

    if (active_modal_impl_) {
        auto act = active_modal_impl_->handle_click(
            framework::ScreenCoord{.x = rel_x, .y = rel_y},
            framework::ScreenSize{.width = box_w, .height = box_h});
        if (act == framework::ModalAction::Close) {
            close();
            return ModalClickResult::Closed;
        }
        if (act == framework::ModalAction::Submit) {
            return ModalClickResult::Submit;
        }
        if (act == framework::ModalAction::ReloadRequested) {
            return ModalClickResult::ReloadRequested;
        }
        if (act == framework::ModalAction::DiscardRequested) {
            return ModalClickResult::DiscardRequested;
        }
        return ModalClickResult::Handled;
    }

    return ModalClickResult::Handled;
}

auto TuiModal::is_control_at(int x, int y) const -> bool {
    if ((active_modal_ == ModalType::None && !active_modal_impl_) || rendered_box_width_ == 0 ||
        rendered_box_height_ == 0)
        return false;
    const int relative_y = y - rendered_start_y_ - 1;
    const int control_column = x - rendered_start_x_ - 3;
    for (const auto& row : rendered_control_rows_) {
        if (row.relative_y != relative_y) continue;
        if (std::ranges::any_of(row.spans, [control_column](const auto& span) {
                return span.contains(control_column);
            })) {
            return true;
        }
    }
    if (active_modal_impl_) {
        const int rel_x = x - rendered_start_x_ - 1;
        return active_modal_impl_->is_custom_control_at(rel_x, relative_y);
    }
    return false;
}

void TuiModal::render_overlay(std::vector<std::string>& lines, int term_width,
                              int term_height) const {
    if ((active_modal_ == ModalType::None && !active_modal_impl_) || lines.empty()) return;
    rendered_control_rows_.clear();
    const int modal_available_width = std::max(0, term_width - 4);
    const int modal_available_height = std::max(0, term_height - 4);
    framework::ScopedRenderContext render_context(
        {.width = modal_available_width,
         .height = modal_available_height,
         .density = framework::resolve_density(density_mode_, modal_available_width,
                                               modal_available_height)});

    const auto meta = modals::get_modal_metadata(active_modal_, notice_is_error_, notice_title_);
    constexpr int kMissionObjectiveMaxWidth = 100;
    const int maximum_width = std::min(term_width - 4, active_modal_ == ModalType::MissionObjective
                                                           ? kMissionObjectiveMaxWidth
                                                           : term_width - 4);
    const int provisional_width = maximum_width;
    if (provisional_width < 35) return;

    std::string m_bg = kThemeModalBg;

    std::vector<std::string> content_rows;

    const std::string title = active_modal_impl_ ? active_modal_impl_->title() : meta.title;
    if (active_modal_impl_) {
        active_modal_impl_->render_content(content_rows, provisional_width, modal_available_height);
    }

    if (content_rows.empty()) return;

    int cursor_row = active_modal_impl_ ? active_modal_impl_->cursor_row() : 0;

    const int available_height = std::min(term_height, static_cast<int>(lines.size()));
    std::vector<framework::Row> measured_rows;
    measured_rows.reserve(content_rows.size());
    for (auto const& row : content_rows) measured_rows.push_back({.text = row});
    const OverlayGeometry overlay =
        framework::layout_modal(title, measured_rows, term_width, available_height).geometry;
    if (!overlay.renderable) return;
    const int box_w = overlay.width;
    const int box_h = overlay.height;
    const int start_y = overlay.start_y;
    const int start_x = overlay.start_x;
    int inner_w = box_w - 2;
    rendered_term_width_ = term_width;
    rendered_term_height_ = term_height;
    rendered_box_width_ = box_w;
    rendered_box_height_ = box_h;
    rendered_start_x_ = start_x;
    rendered_start_y_ = start_y;

    const int total_rows = static_cast<int>(content_rows.size());
    const int visible_rows = overlay.visible_content_rows;
    int scroll_start = 0;
    if (total_rows > visible_rows) {
        if (active_modal_ == ModalType::Help) {
            scroll_start = std::clamp(help_scroll_, 0, total_rows - visible_rows);
            help_scroll_ = scroll_start;
        } else {
            scroll_start = std::clamp(cursor_row - visible_rows / 2, 0, total_rows - visible_rows);
        }
    }

    // Render Box Top Border
    if (start_y < static_cast<int>(lines.size())) {
        std::string title_fmt =
            std::format("\033[1m{}{}\033[0m{}{}", kThemeMint, title, kThemeBorder, m_bg);
        int title_len = get_display_width(title);
        std::string up_badge;
        int up_badge_len = 0;
        if (scroll_start > 0) {
            up_badge = " \033[1;33m▲ more\033[0m ";
            up_badge_len = get_display_width(up_badge);
        }
        int dash_len = inner_w - title_len - up_badge_len;
        if (dash_len < 0) dash_len = 0;
        int left_dash = dash_len / 2;
        int right_dash = dash_len - left_dash;
        const auto style = get_active_theme_style();
        auto const& glyphs = get_theme_glyphs(style);

        bool const classic = std::string_view(glyphs.top_left) == "+";
        std::string_view const top_left = classic ? "+" : "┌";
        std::string_view const top_right = classic ? "+" : "┐";
        std::string top_border =
            std::format("{}{}{}{}{}{}{}{}{}{}\033[0m", kThemeBorder, m_bg, top_left,
                        make_repeated_string(glyphs.horiz, left_dash), title_fmt, up_badge, m_bg,
                        kThemeBorder, make_repeated_string(glyphs.horiz, right_dash), top_right);

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
            auto control = modals::layout_modal_control_row(
                content_rows.at(static_cast<std::size_t>(content_idx)), inner_w);
            content = std::move(control.text);
            if (!control.spans.empty()) {
                rendered_control_rows_.push_back({.relative_y = i + 1,
                                                  .content_row = content_idx,
                                                  .spans = std::move(control.spans)});
            }
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

        // Each border cell establishes its own foreground and surface background. Content fitting
        // deliberately emits a reset, so relying on inherited state made the left, right, and
        // horizontal edges render with different backgrounds.
        std::string row_str =
            std::format("{}{}{}\033[0m{}{}\033[0m{}{}{}\033[0m", m_bg, kThemeBorder, glyphs.vert,
                        m_bg, format_to_width(content, inner_w), m_bg, kThemeBorder, glyphs.vert);

        lines.at(static_cast<std::size_t>(target_y)) =
            overlay_string(lines.at(static_cast<std::size_t>(target_y)), row_str, start_x, box_w);
    }

    // Render Box Bottom Border
    int bot_y = start_y + box_h - 1;
    if (bot_y < static_cast<int>(lines.size())) {
        bool const classic = std::string_view(glyphs.bot_left) == "+";
        std::string_view const bot_left = classic ? "+" : "└";
        std::string_view const bot_right = classic ? "+" : "┘";
        std::string bot_border;
        if (total_rows > visible_rows && scroll_start + visible_rows < total_rows) {
            int more_below = total_rows - (scroll_start + visible_rows);
            std::string down_badge = std::format(" \033[1;33m▼ {} more below\033[0m ", more_below);
            int down_badge_len = get_display_width(down_badge);
            int rem_dash = inner_w - down_badge_len;
            if (rem_dash > 0) {
                int left_d = rem_dash / 2;
                int right_d = rem_dash - left_d;
                bot_border = std::format("{}{}{}{}{}{}{}{}{}\033[0m", kThemeBorder, m_bg, bot_left,
                                         make_repeated_string(glyphs.horiz, left_d), down_badge,
                                         m_bg, kThemeBorder,
                                         make_repeated_string(glyphs.horiz, right_d), bot_right);
            } else {
                bot_border =
                    std::format("{}{}{}{}{}{}\033[0m", kThemeBorder, m_bg, bot_left,
                                make_repeated_string(glyphs.horiz, inner_w), bot_right, "\033[0m");
            }
        } else {
            bot_border =
                std::format("{}{}{}{}{}{}\033[0m", kThemeBorder, m_bg, bot_left,
                            make_repeated_string(glyphs.horiz, inner_w), bot_right, "\033[0m");
        }
        lines.at(static_cast<std::size_t>(bot_y)) =
            overlay_string(lines.at(static_cast<std::size_t>(bot_y)), bot_border, start_x, box_w);
    }
}

}  // namespace simrv::tui
