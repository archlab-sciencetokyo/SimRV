/**
 * @file Tui.hpp
 * @brief Interactive TUI console dashboard for RTOS execution mode.
 */
#pragma once

#include <chrono>
#include <csignal>
#include <string>
#include <vector>
#include <memory>
#include <queue>
#include <mutex>
#include <atomic>
#include <thread>

#include "simrv/xlen/Types.hpp"
#include "simrv/isa/Base.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/tui/VirtualTerminal.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {
extern volatile std::sig_atomic_t g_resized;

enum class TuiLayout {
    Split,
    FullConsole,
    FullRegister
};

enum class TuiRegPage {
    GPR,
    FPR,
    VEC,
    PIPELINE,
    CACHE,
    EXPLAIN
};

enum class TuiRightPanelMode {
    Terminal,
    Log,
    LiveTrace,
    Display
};

/**
 * @class Tui
 * @brief Handles ANSI-based split-screen rendering, scrolling, and status display for RTOS mode.
 */
class RegisterPane;
class ConsolePane;
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

    void set_status_override(const std::string& status) { status_override_ = status; }
    void clear_status_override() { status_override_.clear(); }

    void cycle_layout() {
        if (layout_ == TuiLayout::Split) layout_ = TuiLayout::FullConsole;
        else if (layout_ == TuiLayout::FullConsole) layout_ = TuiLayout::FullRegister;
        else layout_ = TuiLayout::Split;
        render(true);
    }

    void cycle_reg_page();
    void set_reg_page(TuiRegPage page);
    void toggle_explain();
    void toggle_high_contrast();
    void toggle_sakura_theme();
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
    
    std::unique_ptr<RegisterPane> reg_pane_;
    std::unique_ptr<ConsolePane> console_pane_;
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
