/**
 * @file Tui.hpp
 * @brief Interactive TUI console dashboard for RTOS execution mode.
 */
#pragma once

#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/isa/Base.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/tui/TuiKey.hpp"
#include "simrv/tui/TuiModal.hpp"
#include "simrv/tui/TuiTypes.hpp"
#include "simrv/tui/VirtualTerminal.hpp"
#include "simrv/tui/panels/StatusBar.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {

extern volatile std::sig_atomic_t g_resized;  // NOLINT(avoid-non-const-global-variables)

/**
 * @class Tui
 * @brief Handles ANSI-based split-screen rendering, scrolling, and status display for RTOS mode.
 */
enum class SelectionPane : uint8_t { None, LeftPane, RightPane };

struct SelectionState {
    SelectionPane pane = SelectionPane::None;
    int start_x = -1;
    int start_y = -1;
    int end_x = -1;
    int end_y = -1;
    bool is_selecting = false;
    bool is_active = false;
};

class LeftPane;
class RightPane;
class StatusBar;

class Tui {
   public:
    static constexpr size_t kTraceBufferSize = 200;
    explicit Tui(simrv::core::Machine& machine);
    ~Tui();

    void clear_selection();
    void copy_active_selection();
    void copy_to_clipboard(std::string_view text);
    [[nodiscard]] auto get_selection_state() const -> const SelectionState& { return selection_; }

    void initialize();
    void shutdown();
    void render(bool force = false);
    void handle_char_write(char ch);
    void print_log(const std::string& msg);

    void update();
    void pause_loop();
    void unpause_loop();
    [[nodiscard]] auto is_tui_paused() const -> bool {
        return paused_.load(std::memory_order_relaxed);
    }

    void set_paused(bool p);
    [[nodiscard]] auto is_paused() const -> bool { return paused_.load(std::memory_order_relaxed); }
    void set_sim_thread_sleeping(bool s) {
        sim_thread_is_sleeping_.store(s, std::memory_order_relaxed);
    }
    [[nodiscard]] auto is_sim_thread_sleeping() const -> bool {
        return sim_thread_is_sleeping_.load(std::memory_order_relaxed);
    }
    void update_cache();
    void on_cycle_completed_slow();
    void on_cycle_completed() {
        if (simrv::compiler::unlikely(step_delay_us_.load(std::memory_order_relaxed) > 0)) {
            on_cycle_completed_slow();
        }
    }
    void reset_speed_history();

    std::atomic<uint64_t> step_delay_us_{0};

    void open_modal(ModalType type) {
        if (!is_paused()) {
            pause_loop();
        }
        modal_.open(type, left_pane_.get(), step_delay_us_.load(std::memory_order_relaxed));
        set_paused(true);
        render(true);
    }
    void close_modal() {
        modal_.close();
        render(true);
    }
    void submit_modal() {
        modal_.submit(
            left_pane_.get(), step_delay_us_, [this](TuiRegPage page) { set_reg_page(page); },
            [this](const std::string& status) { set_status_override(status); },
            [this]() { reset_speed_history(); });
        render(true);
    }
    [[nodiscard]] auto is_modal_active() const -> bool { return modal_.is_active(); }
    [[nodiscard]] auto get_active_modal() const -> ModalType { return modal_.get_type(); }

    void set_status_override(const std::string& status) { status_override_ = status; }
    void clear_status_override() { status_override_.clear(); }

    void cycle_layout() {
        if (layout_ == TuiLayout::Split)
            layout_ = TuiLayout::FullRight;
        else if (layout_ == TuiLayout::FullRight)
            layout_ = TuiLayout::FullLeft;
        else
            layout_ = TuiLayout::Split;
        render(true);
    }

    void cycle_reg_page();
    void cycle_tool_page();
    void set_reg_page(TuiRegPage page);
    void toggle_explain();
    void toggle_high_contrast();
    void toggle_sakura_theme();
    void cycle_right_panel_mode();
    void record_instruction(Register pc, simrv::isa::Opcode opcode, simrv::isa::OperationId op_id,
                            uint8_t rd, Register rd_val, uint8_t rs1, Register rs1_val, uint8_t rs2,
                            Register rs2_val, int64_t imm);
    void toggle_trace_enabled();
    [[nodiscard]] auto is_trace_enabled() const -> bool {
        return trace_enabled_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] auto is_trace_active() const -> bool {
        return trace_or_livetrace_active_.load(std::memory_order_relaxed);
    }
    void scroll(int lines);
    void reset_scroll();
    void scroll_regs(int lines);
    void reset_scroll_regs();
    [[nodiscard]] auto get_scroll_offset() const -> int { return scroll_offset_; }
    [[nodiscard]] auto get_right_panel_mode() const -> TuiRightPanelMode {
        return right_panel_mode_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] auto get_pane_width() const -> int { return pane_width_cached_; }
    [[nodiscard]] auto get_layout() const -> TuiLayout { return layout_; }
    void adjust_left_pane_width(int delta);

    /// Toggle keyboard input routing between guest VirtIO console and TUI navigation.
    void toggle_terminal_focus();
    [[nodiscard]] auto is_terminal_focused() const -> bool { return is_terminal_focused_; }

    void handle_mouse(int x, int y, int b);

   private:
    struct TraceRecord {
        Register pc;
        simrv::isa::Opcode opcode;
        simrv::isa::OperationId op_id;
        uint8_t rd;
        Register rd_val;
        uint8_t rs1;
        Register rs1_val;
        uint8_t rs2;
        Register rs2_val;
        int64_t imm;
    };

    simrv::core::Machine& machine_;
    TuiModal modal_;

    std::unique_ptr<LeftPane> left_pane_;
    std::unique_ptr<RightPane> right_pane_;
    std::unique_ptr<StatusBar> status_bar_;

    int pane_width_cached_ = 62;
    int cached_num_rows_ = 20;
    [[nodiscard]] auto get_right_pane_start_line(int num_rows) const -> int;
    [[nodiscard]] auto is_sixel_supported() const -> bool { return sixel_supported_; }

    int user_left_pane_width_{-1};
    int cell_width_px_ = 8;
    int cell_height_px_ = 16;
    bool sixel_supported_{false};
    VirtualTerminal vt_;
    VirtualTerminal vt_log_;
    std::vector<std::string> trace_buffer_;
    std::vector<std::string> lines_to_draw_;
    std::vector<std::string> last_screen_lines_;
    std::atomic<bool> paused_{true};
    std::atomic<bool> trace_enabled_{false};
    TuiLayout layout_ = TuiLayout::Split;
    std::atomic<TuiRightPanelMode> right_panel_mode_{TuiRightPanelMode::Terminal};
    std::atomic<bool> trace_or_livetrace_active_{false};
    std::string status_override_;
    int scroll_offset_{0};
    SelectionState selection_;

    // Performance tracking
    std::chrono::steady_clock::time_point last_speed_update_;
    uint64_t last_icount_ = 0;
    uint64_t speed_ips_ = 0;
    uint64_t kips_ = 0;
    uint64_t max_kips_ = 0;
    std::vector<uint64_t> kips_history_;

    // Active runtime tracking
    std::chrono::microseconds runtime_duration_{0};
    std::chrono::steady_clock::time_point last_runtime_tick_{};

    // Circular trace buffer
    std::vector<TraceRecord> trace_record_buffer_;
    size_t trace_buffer_head_{0};
    size_t trace_buffer_tail_{0};
    size_t trace_buffer_size_{0};
    mutable std::mutex trace_mutex_;

    // Thread-safe queues for decoupling writes from simulation
    std::queue<char> tx_fifo_;
    std::queue<std::string> log_fifo_;
    mutable std::mutex tui_mutex_;
    mutable std::mutex io_mutex_;

    std::string esc_buf_;
    std::atomic<bool> sim_thread_is_sleeping_{false};
    std::thread::id main_thread_id_;
    std::atomic<bool> is_terminal_focused_{false};  // True when Ctrl-A routes keys to guest console

    auto consume_control_sequence(uint8_t first_byte) -> bool;
    auto parse_sgr_mouse(const std::string& seq, int& b, int& x, int& y) -> bool;
    auto poll_keyboard(uint8_t& byte_out) -> bool;
    auto format_trace_record(const TraceRecord& rec) -> std::string;
    auto update_trace_active_cache() -> void;

    void execute_footer_action(TuiFooterAction action);
    auto handle_alt_key(char key, uint8_t byte) -> bool;
    auto handle_arrow_key_sequence() -> bool;
    auto handle_modal_keyboard_input(uint8_t byte, TuiKey key) -> bool;
    bool handle_modal_settings_misa(ModalType mtype, uint8_t byte, TuiKey key);
    bool handle_modal_sysconfig_bp(ModalType mtype, uint8_t byte, TuiKey key);
    auto handle_normal_keyboard_input(uint8_t byte, TuiKey key) -> void;
    auto handle_debug_keyboard_input(TuiKey key) -> bool;
    bool handle_speed_keyboard_input(TuiKey key);
    bool handle_navigation_keyboard_input(uint8_t byte, TuiKey key);
    auto handle_mouse_left_pane(int x, int y, int b) -> void;
    void format_trace_inst(const TraceRecord& rec, const std::string& op_name, bool rd_fp,
                           bool rs1_fp, bool rs2_fp, std::string& inst_str,
                           std::string& side_effect);
    void render_update_speed(std::chrono::steady_clock::time_point now);
    void render_build_lines(int left_pane_width, int right_pane_width, int num_rows,
                            TuiRightPanelMode panel_mode);
    void render_draw_sixel(int left_pane_width, int right_pane_width, int num_rows,
                           std::string& update_cmds);
    void init_terminal_raw_mode();
    void detect_terminal_sixel_support(const std::string& resp);
};

}  // namespace simrv::tui
