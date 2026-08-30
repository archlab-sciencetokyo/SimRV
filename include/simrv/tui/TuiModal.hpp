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

#include "simrv/tui/framework/Types.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
enum class PlatformProfile : uint8_t;
}  // namespace simrv::core

namespace simrv::tui {

class InspectorPane;
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
    Glossary,
    Settings,
    ConfigureMisa,
    ConfigureSystem,
    ManageBreakpoints,
    Notice,
    PlatformChangeConfirm
};

struct SysConfigDraft {
    uint8_t profile = 1;        // tiny, balanced, performance, custom
    uint8_t pipeline_type = 0;  // 0: 5-stage, 1: 3-stage
    uint32_t mul_latency = 3;
    uint32_t div_latency = 18;
    uint32_t fp_alu_latency = 4;
    uint32_t fp_div_latency = 16;
    uint32_t csr_flush_penalty = 3;
    uint32_t fence_flush_penalty = 4;
    bool enable_forwarding = true;
    bool cycle_accurate = false;
    uint8_t bpred_type = 2;  // 0: Static, 1: Bimodal, 2: GShare, 3: Tournament
    uint32_t bht_entries = 1024;
    uint32_t btb_entries = 256;
    uint32_t ras_entries = 16;
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

struct SettingsDraft {
    uint8_t active_tab = 0;  // 0: General, 1: MISA Extensions, 2: Microarchitecture
    int tab_cursor[3] = {0, 0, 0};

    // Tab 0: General / UI
    bool cycle_accurate = false;
    bool debug_mode = false;
    bool high_contrast = false;
    bool class_mode = false;
    uint32_t tui_fps = 30;
    bool use_mix = false;
    bool bp_trace = false;
    bool traplog_mode = false;
    bool dlog_mode = false;
    bool lockstep_mode = false;
    bool gdb_mode = false;
    uint32_t num_harts = 1;
    uint32_t smp_quantum = 1000;
    bool smp_multithreaded = false;
    uint8_t platform_profile = 0;  // 0: PCIe, 1: MMIO
    uint64_t dram_size_mb = 256;
    std::string net_mode = "user";

    // Tab 1: MISA Extensions
    MisaDraft misa;

    // Tab 2: System / Microarchitecture
    SysConfigDraft sys_config;
};

class TuiModal {
   public:
    explicit TuiModal(simrv::core::Machine& machine);

    [[nodiscard]] auto is_active() const -> bool { return active_modal_ != ModalType::None; }
    [[nodiscard]] auto get_type() const -> ModalType { return active_modal_; }
    [[nodiscard]] auto get_input() const -> const std::string& { return input_; }

    void open(ModalType type, InspectorPane* left_pane, uint64_t step_delay_us);
    void close();
    auto submit(InspectorPane* left_pane, std::atomic<uint64_t>& step_delay_us,
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
    void cycle_settings_tab(int delta = 1);
    void set_settings_tab(uint8_t tab);
    void adjust_setting_at_cursor(int dir);
    void toggle_setting_at_cursor();
    void toggle_setting_by_index(int index);
    void push_settings_digit(char c);
    void pop_settings_digit();
    void apply_settings_misa_profile(int profile_idx);

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

    void move_glossary_topic(int delta);
    void set_glossary_topic(int topic);
    void scroll_glossary_content(int delta);

    void open_notice(const std::string& title, const std::string& message, bool is_error = false);
    void open_platform_confirm(const SettingsDraft& draft);
    [[nodiscard]] auto get_pending_platform_draft() const -> const SettingsDraft& {
        return pending_platform_draft_;
    }

    enum class ModalClickResult : uint8_t {
        Ignored,
        Handled,
        Closed,
        Submit,
        ReloadRequested,
        DiscardRequested
    };

    [[nodiscard]] auto handle_click(int x, int y, int term_width, int term_height)
        -> ModalClickResult;

    void render_overlay(std::vector<std::string>& lines, int term_width, int term_height) const;

   private:
    simrv::core::Machine& machine_;
    ModalType active_modal_ = ModalType::None;
    std::string input_;
    std::string notice_title_;
    std::string notice_message_;
    bool notice_is_error_ = false;
    int bp_cursor_ = 0;
    SettingsDraft settings_draft_;
    SettingsDraft pending_platform_draft_;
    int misa_cursor_ = 0;
    MisaDraft misa_draft_;
    int sysconfig_cursor_ = 0;
    SysConfigDraft sysconfig_draft_;
    int glossary_topic_ = 0;
    int glossary_scroll_ = 0;
    bool load_appmode_ = true;  // Toggle for App (baremetal) vs OS (Linux) mode in LoadBinary modal
    std::string staged_binary_path_;
    bool staged_mode_change_ = false;
    bool staged_target_appmode_ = false;
    // Rendering resolves content-dependent modal geometry; hit-testing reuses that exact box.
    mutable int rendered_term_width_ = 0;
    mutable int rendered_term_height_ = 0;
    mutable int rendered_box_width_ = 0;
    mutable int rendered_box_height_ = 0;
    mutable int rendered_start_x_ = 0;
    mutable int rendered_start_y_ = 0;
    struct RenderedControlRow {
        int relative_y = 0;
        int content_row = 0;
        std::vector<framework::ControlSpan> spans;
    };
    mutable std::vector<RenderedControlRow> rendered_control_rows_;
};

}  // namespace simrv::tui
