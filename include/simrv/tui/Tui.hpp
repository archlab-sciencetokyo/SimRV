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
#include "simrv/tui/TuiModal.hpp"
#include "simrv/tui/VirtualTerminal.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {

extern volatile std::sig_atomic_t g_resized;  // NOLINT(avoid-non-const-global-variables)

enum class TuiLayout : uint8_t {
    Split,
    FullRight,
    FullLeft
};

enum class TuiRegPage : uint8_t {
    GPR,
    FPR,
    VEC,
    PIPELINE,
    CACHE,
    TLB,
    BPRED,
    HAZARD,
    BUS,
    TRACE,
    EXPLAIN,
    STACK
};

enum class TuiRightPanelMode : uint8_t {
    Terminal,
    Display
};

/**
 * @class Tui
 * @brief Handles ANSI-based split-screen rendering, scrolling, and status display for RTOS mode.
 */
class LeftPane;
class RightPane;
class StatusBar;

class Tui {
   public:
    static constexpr size_t kTraceBufferSize = 200;
    explicit Tui(simrv::core::Machine& machine);
    ~Tui();

    void initialize();
    void shutdown();
    void render(bool force = false);
    void handle_char_write(char ch);
    void print_log(const std::string& msg);

    void update();
    void pause_loop();
    [[nodiscard]] auto is_tui_paused() const -> bool { return tui_loop_paused_.load(std::memory_order_relaxed); }

    void set_paused(bool p);
    [[nodiscard]] auto is_paused() const -> bool { return paused_; }
    void set_sim_thread_sleeping(bool s) { sim_thread_is_sleeping_.store(s, std::memory_order_relaxed); }
    [[nodiscard]] auto is_sim_thread_sleeping() const -> bool { return sim_thread_is_sleeping_.load(std::memory_order_relaxed); }
    void update_cache();
    void on_cycle_completed_slow();
    void on_cycle_completed() {
        if (simrv::compiler::unlikely(step_budget_.load(std::memory_order_relaxed) > 0 ||
                                      step_delay_us_.load(std::memory_order_relaxed) > 0)) {
            on_cycle_completed_slow();
        }
    }
    void reset_speed_history();

    std::atomic<uint64_t> step_budget_{0};
    std::atomic<uint64_t> step_delay_us_{0};
    std::atomic<uint64_t> step_granularity_{50};

    void cycle_step_granularity(bool increase = true);

    void open_modal(ModalType type) {
        if (!tui_loop_paused_) {
            pause_loop();
        }
        modal_.open(type, left_pane_.get(), step_granularity_.load(std::memory_order_relaxed), step_delay_us_.load(std::memory_order_relaxed));
        set_paused(true);
        render(true);
    }
    void close_modal() { modal_.close(); render(true); }
    void submit_modal() {
        modal_.submit(
            left_pane_.get(), step_granularity_, step_delay_us_,
            [this](TuiRegPage page) { set_reg_page(page); },
            [this](const std::string& status) { set_status_override(status); },
            [this]() { reset_speed_history(); });
        render(true);
    }
    [[nodiscard]] auto is_modal_active() const -> bool { return modal_.is_active(); }
    [[nodiscard]] auto get_active_modal() const -> ModalType { return modal_.get_type(); }

    void set_status_override(const std::string& status) { status_override_ = status; }
    void clear_status_override() { status_override_.clear(); }

    void cycle_layout() {
        if (layout_ == TuiLayout::Split) layout_ = TuiLayout::FullRight;
        else if (layout_ == TuiLayout::FullRight) layout_ = TuiLayout::FullLeft;
        else layout_ = TuiLayout::Split;
        render(true);
    }

    void cycle_reg_page();
    void cycle_tool_page();
    void set_reg_page(TuiRegPage page);
    void remove_breakpoint_or_watchpoint_by_index(size_t target_idx);
    void toggle_explain();
    void toggle_high_contrast();
    void toggle_sakura_theme();
    void toggle_cycle_accurate();
    void toggle_debug_mode();
    void cycle_right_panel_mode();
    void record_instruction(Register pc, simrv::isa::Opcode opcode, simrv::isa::OperationId op_id, uint8_t rd, Register rd_val, uint8_t rs1, Register rs1_val, uint8_t rs2, Register rs2_val, int64_t imm);
    void toggle_trace_enabled();
    [[nodiscard]] auto is_trace_enabled() const -> bool { return trace_enabled_.load(std::memory_order_relaxed); }
    [[nodiscard]] auto is_trace_active() const -> bool { return trace_or_livetrace_active_.load(std::memory_order_relaxed); }
    void scroll(int lines);
    void reset_scroll();
    void scroll_regs(int lines);
    void reset_scroll_regs();
    [[nodiscard]] auto get_scroll_offset() const -> int { return scroll_offset_; }
    [[nodiscard]] auto get_right_panel_mode() const -> TuiRightPanelMode { return right_panel_mode_.load(std::memory_order_relaxed); }
    [[nodiscard]] auto get_pane_width() const -> int { return pane_width_cached_; }
    [[nodiscard]] auto get_layout() const -> TuiLayout { return layout_; }
    void adjust_left_pane_width(int delta);

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
    int user_left_pane_width_{-1};
    int cell_width_px_ = 8;
    int cell_height_px_ = 16;
    VirtualTerminal vt_;
    VirtualTerminal vt_log_;
    std::vector<std::string> trace_buffer_;
    std::vector<std::string> lines_to_draw_;
    std::vector<std::string> last_screen_lines_;
    bool paused_ = true;
    std::atomic<bool> trace_enabled_{false};
    TuiLayout layout_ = TuiLayout::Split;
    std::atomic<TuiRightPanelMode> right_panel_mode_{TuiRightPanelMode::Terminal};
    std::atomic<bool> trace_or_livetrace_active_{false};
    std::string status_override_;
    int scroll_offset_{0};

    // Performance tracking
    std::chrono::steady_clock::time_point last_speed_update_;
    uint64_t last_icount_ = 0;
    uint64_t speed_ips_ = 0;
    uint64_t kips_ = 0;
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
    std::atomic<bool> tui_loop_paused_{false};
    std::atomic<bool> sim_thread_is_sleeping_{false};
    std::thread::id main_thread_id_;

    auto consume_control_sequence(uint8_t first_byte) -> bool;
    auto parse_sgr_mouse(const std::string& seq, int& b, int& x, int& y) -> bool;
    auto poll_keyboard(uint8_t& byte_out) -> bool;
    auto format_trace_record(const TraceRecord& rec) -> std::string;
    auto update_trace_active_cache() -> void;
};

} // namespace simrv::tui
