/**
 * @file TuiModal.hpp
 * @brief Modal dialog manager and overlay renderer for SimRV TUI.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {

class LeftPane;
enum class TuiRegPage : uint8_t;

enum class ModalType : uint8_t {
    None,
    SetBreakpoint,
    SetStepSize,
    SetSpeed,
    InspectAddress,
    LoadBinary,
    LoadDiskImage,
    Help,
    Settings
};

struct SettingsDraft {
    bool cycle_accurate = false;
    bool debug_mode = false;
    bool rollback_enabled = false;
    bool high_contrast = false;
    bool use_mix = false;
    bool bp_trace = false;
    bool traplog_mode = false;
    bool dlog_mode = false;
    bool high_performance = false;
    bool lockstep_mode = false;
    bool gdb_mode = false;
};

class TuiModal {
   public:
    explicit TuiModal(simrv::core::Machine& machine);

    [[nodiscard]] auto is_active() const -> bool { return active_modal_ != ModalType::None; }
    [[nodiscard]] auto get_type() const -> ModalType { return active_modal_; }
    [[nodiscard]] auto get_input() const -> const std::string& { return input_; }

    void open(ModalType type, LeftPane* left_pane, uint64_t step_granularity, uint64_t step_delay_us);
    void close();
    auto submit(LeftPane* left_pane, std::atomic<uint64_t>& step_granularity,
                std::atomic<uint64_t>& step_delay_us,
                const std::function<void(TuiRegPage)>& set_reg_page_cb,
                const std::function<void(const std::string&)>& set_status_override_cb,
                const std::function<void()>& on_speed_changed_cb) -> bool;

    void push_char(char c) { input_.push_back(c); }
    void pop_char() { if (!input_.empty()) input_.pop_back(); }
    void toggle_load_mode() { load_appmode_ = !load_appmode_; }
    [[nodiscard]] auto get_load_appmode() const -> bool { return load_appmode_; }

    void move_settings_cursor(int delta);
    void toggle_setting_at_cursor();
    void toggle_setting_by_index(int index);

    void render_overlay(std::vector<std::string>& lines, int term_width, int term_height) const;

   private:
    simrv::core::Machine& machine_;
    ModalType active_modal_ = ModalType::None;
    std::string input_;
    int settings_cursor_ = 0;
    SettingsDraft settings_draft_;
    bool load_appmode_ =
        false;  // Toggle for App (baremetal) vs OS (Linux) mode in LoadBinary modal
    std::string staged_binary_path_;
    bool staged_mode_change_ = false;
    bool staged_target_appmode_ = false;
};

}  // namespace simrv::tui
