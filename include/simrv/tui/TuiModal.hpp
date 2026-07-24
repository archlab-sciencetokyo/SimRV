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
    SetWatchpoint,
    SetStepSize,
    SetSpeed,
    InspectAddress,
    LoadBinary,
    LoadDiskImage,
    Help,
    Settings,
    ConfigureMisa
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

struct MisaDraft {
    uint8_t xlen_bits = 64;
    bool ext_a = true;
    bool ext_b = true;
    bool ext_c = true;
    bool ext_d = true;
    bool ext_f = true;
    bool ext_i = true;
    bool ext_m = true;
    bool ext_s = true;
    bool ext_u = true;
    bool ext_v = false;

    [[nodiscard]] auto to_misa_val() const -> uint64_t {
        uint64_t val = 0;
        if (xlen_bits == 32) {
            val |= (1ULL << 30);
        } else {
            val |= (2ULL << 62);
        }
        if (ext_a) val |= (1ULL << ('a' - 'a'));
        if (ext_b) val |= (1ULL << ('b' - 'a'));
        if (ext_c) val |= (1ULL << ('c' - 'a'));
        if (ext_d) val |= (1ULL << ('d' - 'a'));
        if (ext_f) val |= (1ULL << ('f' - 'a'));
        if (ext_i) val |= (1ULL << ('i' - 'a'));
        if (ext_m) val |= (1ULL << ('m' - 'a'));
        if (ext_s) val |= (1ULL << ('s' - 'a'));
        if (ext_u) val |= (1ULL << ('u' - 'a'));
        if (ext_v) val |= (1ULL << ('v' - 'a'));
        return val;
    }

    [[nodiscard]] auto to_misa_string() const -> std::string {
        std::string s = (xlen_bits == 32) ? "rv32" : "rv64";
        if (ext_i) s += 'i';
        if (ext_m) s += 'm';
        if (ext_a) s += 'a';
        if (ext_f) s += 'f';
        if (ext_d) s += 'd';
        if (ext_c) s += 'c';
        if (ext_b) s += 'b';
        if (ext_v) s += 'v';
        if (ext_s) s += 's';
        if (ext_u) s += 'u';
        return s;
    }
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

    void move_misa_cursor(int delta);
    void toggle_misa_at_cursor();
    void toggle_misa_by_index(int index);
    void apply_misa_profile(int profile_idx);

    void render_overlay(std::vector<std::string>& lines, int term_width, int term_height) const;

   private:
    simrv::core::Machine& machine_;
    ModalType active_modal_ = ModalType::None;
    std::string input_;
    int settings_cursor_ = 0;
    SettingsDraft settings_draft_;
    int misa_cursor_ = 0;
    MisaDraft misa_draft_;
    bool load_appmode_ =
        true;  // Toggle for App (baremetal) vs OS (Linux) mode in LoadBinary modal
    std::string staged_binary_path_;
    bool staged_mode_change_ = false;
    bool staged_target_appmode_ = false;
};

}  // namespace simrv::tui
