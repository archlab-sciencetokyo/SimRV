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

#include "simrv/xlen/Helpers.hpp"
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
    SetSpeed,
    InspectAddress,
    LoadBinary,
    LoadDiskImage,
    Help,
    Settings,
    ConfigureMisa,
    ConfigureSystem,
    ManageBreakpoints,
    Notice
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
    bool smp_multithreaded = false;
};

struct SysConfigDraft {
    uint8_t preset = 0;  // 0: Rocket, 1: Embedded, 2: Fast
    uint32_t icache_miss_penalty = 10;
    uint32_t dcache_miss_penalty = 15;
    uint32_t tlb_miss_penalty = 25;
    uint32_t mul_latency = 3;
    uint32_t div_latency = 18;
    uint32_t fp_alu_latency = 4;
    uint32_t fp_div_latency = 16;
    uint32_t csr_flush_penalty = 3;
    uint32_t fence_flush_penalty = 4;
    uint32_t branch_mispredict_penalty = 2;
    bool enable_forwarding = true;
    bool enable_ex_forwarding = true;
    bool enable_mem_forwarding = true;
    uint8_t bp_type = 3;  // 0: Static-NT, 1: Static-T, 2: 1-Bit, 3: 2-Bit Bimodal, 4: Gshare
    uint32_t btb_entries = 128;
    uint32_t num_harts = 1;
    uint32_t smp_quantum = 1000;
    bool smp_multithreaded = false;
    bool cycle_accurate = false;
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
    unsigned vlen = 256;  // VLEN for V extension (32–1024, power of 2)
    [[nodiscard]] auto to_misa_val() const -> uint64_t {
        uint64_t val = 0;
        if (xlen_bits == 32) {
            val |= (1ULL << 30);
        } else {
            val |= (2ULL << 62);
        }
        val |= (1ULL << ('i' - 'a'));  // Extension I is mandatory
        if (ext_m) val |= (1ULL << ('m' - 'a'));
        if (ext_a) val |= (1ULL << ('a' - 'a'));
        if (ext_f) val |= (1ULL << ('f' - 'a'));
        if (ext_d) val |= (1ULL << ('d' - 'a'));
        if (ext_c) val |= (1ULL << ('c' - 'a'));
        if (ext_b) val |= (1ULL << ('b' - 'a'));
        if (ext_v) val |= (1ULL << ('v' - 'a'));
        if (ext_s) val |= (1ULL << ('s' - 'a'));
        if (ext_u) val |= (1ULL << ('u' - 'a'));
        return val;
    }

    [[nodiscard]] auto to_misa_string() const -> std::string {
        return simrv::xlen::resolve_misa_string(to_misa_val());
    }
};

class TuiModal {
   public:
    explicit TuiModal(simrv::core::Machine& machine);

    [[nodiscard]] auto is_active() const -> bool { return active_modal_ != ModalType::None; }
    [[nodiscard]] auto get_type() const -> ModalType { return active_modal_; }
    [[nodiscard]] auto get_input() const -> const std::string& { return input_; }

    void open(ModalType type, LeftPane* left_pane, uint64_t step_delay_us);
    void close();
    auto submit(LeftPane* left_pane, std::atomic<uint64_t>& step_delay_us,
                const std::function<void(TuiRegPage)>& set_reg_page_cb,
                const std::function<void(const std::string&)>& set_status_override_cb,
                const std::function<void()>& on_speed_changed_cb) -> bool;

    void push_char(char c) { input_.push_back(c); }
    void pop_char() {
        if (!input_.empty()) input_.pop_back();
    }
    void toggle_load_mode() { load_appmode_ = !load_appmode_; }
    [[nodiscard]] auto get_load_appmode() const -> bool { return load_appmode_; }

    void move_settings_cursor(int delta);
    void toggle_setting_at_cursor();
    void toggle_setting_by_index(int index);

    void move_misa_cursor(int delta);
    void toggle_misa_at_cursor();
    void toggle_misa_by_index(int index);
    void apply_misa_profile(int profile_idx);

    void move_sysconfig_cursor(int delta);
    void adjust_sysconfig_at_cursor(int dir);
    void toggle_sysconfig_at_cursor();
    void toggle_sysconfig_by_index(int index);
    void push_sysconfig_digit(char c);
    void pop_sysconfig_digit();

    void move_bp_cursor(int delta);
    auto remove_bp_at_cursor(const std::function<void(const std::string&)>& set_status_override_cb)
        -> bool;

    void open_notice(const std::string& title, const std::string& message, bool is_error = false);

    void render_overlay(std::vector<std::string>& lines, int term_width, int term_height) const;

   private:
    simrv::core::Machine& machine_;
    ModalType active_modal_ = ModalType::None;
    std::string input_;
    std::string notice_title_;
    std::string notice_message_;
    bool notice_is_error_ = false;
    int bp_cursor_ = 0;
    int settings_cursor_ = 0;
    SettingsDraft settings_draft_;
    int misa_cursor_ = 0;
    MisaDraft misa_draft_;
    int sysconfig_cursor_ = 0;
    SysConfigDraft sysconfig_draft_;
    bool load_appmode_ = true;  // Toggle for App (baremetal) vs OS (Linux) mode in LoadBinary modal
    std::string staged_binary_path_;
    bool staged_mode_change_ = false;
    bool staged_target_appmode_ = false;
};

}  // namespace simrv::tui
