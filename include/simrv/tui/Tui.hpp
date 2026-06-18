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

#include "simrv/xlen/Types.hpp"
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
    EXPLAIN
};

enum class TuiRightPanelMode {
    Terminal,
    Log,
    LiveTrace
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
    explicit Tui(simrv::core::Machine& machine);
    ~Tui();

    void initialize();
    void shutdown();
    void render();
    void handle_char_write(char ch);
    void print_log(const std::string& msg);

    void set_paused(bool p);
    [[nodiscard]] auto is_paused() const -> bool { return paused_; }
    void update_cache();

    void set_status_override(const std::string& status) { status_override_ = status; }
    void clear_status_override() { status_override_.clear(); }

    void cycle_layout() {
        if (layout_ == TuiLayout::Split) layout_ = TuiLayout::FullConsole;
        else if (layout_ == TuiLayout::FullConsole) layout_ = TuiLayout::FullRegister;
        else layout_ = TuiLayout::Split;
        render();
    }

    void cycle_reg_page();
    void set_reg_page(TuiRegPage page);
    void toggle_explain();
    void toggle_high_contrast();
    void toggle_sakura_theme();
    void cycle_right_panel_mode();
    void record_instruction(Register pc, uint8_t op_id, uint8_t rd, Register rd_val, uint8_t rs1, Register rs1_val, uint8_t rs2, Register rs2_val, int64_t imm);
    void toggle_trace_enabled();
    [[nodiscard]] auto is_trace_enabled() const -> bool { return trace_enabled_; }
    void scroll(int lines);
    void reset_scroll();
    void scroll_regs(int lines);
    void reset_scroll_regs();
    [[nodiscard]] auto get_scroll_offset() const -> int { return scroll_offset_; }
    [[nodiscard]] auto get_right_panel_mode() const -> TuiRightPanelMode { return right_panel_mode_; }
    [[nodiscard]] auto get_pane_width() const -> int { return pane_width_cached_; }
    [[nodiscard]] auto get_layout() const -> TuiLayout { return layout_; }
    void adjust_left_pane_width(int delta);


    void handle_mouse(int x, int y, int b);

   private:
    simrv::core::Machine& machine_;
    
    std::unique_ptr<RegisterPane> reg_pane_;
    std::unique_ptr<ConsolePane> console_pane_;
    std::unique_ptr<StatusBar> status_bar_;

    int pane_width_cached_ = 62;
    int user_left_pane_width_{-1};
    VirtualTerminal vt_;
    VirtualTerminal vt_log_;
    std::vector<std::string> trace_buffer_;
    std::vector<std::string> lines_to_draw_;
    bool paused_ = true;
    bool trace_enabled_ = false;
    TuiLayout layout_ = TuiLayout::Split;
    TuiRightPanelMode right_panel_mode_ = TuiRightPanelMode::Terminal;
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
};

} // namespace simrv::tui
