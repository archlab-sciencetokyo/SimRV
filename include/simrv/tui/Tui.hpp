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
    PIPELINE
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

    void set_paused(bool p) {
        paused_ = p;
                if (!p) status_override_.clear();
    }
    [[nodiscard]] auto is_paused() const -> bool { return paused_; }

    void set_status_override(const std::string& status) { status_override_ = status; }
    void clear_status_override() { status_override_.clear(); }

    void cycle_layout() {
        if (layout_ == TuiLayout::Split) layout_ = TuiLayout::FullConsole;
        else if (layout_ == TuiLayout::FullConsole) layout_ = TuiLayout::FullRegister;
        else layout_ = TuiLayout::Split;
        render();
    }

    void cycle_reg_page();
    void scroll(int lines);
    void reset_scroll();
    [[nodiscard]] auto get_scroll_offset() const -> int { return scroll_offset_; }


    void handle_mouse(int x, int y, int b);

   private:
    enum class AnsiState {
        Normal,
        Esc,
        Csi
    };
    AnsiState ansi_state_ = AnsiState::Normal;

    simrv::core::Machine& machine_;
    
    std::unique_ptr<RegisterPane> reg_pane_;
    std::unique_ptr<ConsolePane> console_pane_;
    std::unique_ptr<StatusBar> status_bar_;

    int pane_width_cached_ = 62;
    std::vector<std::string> raw_lines_;
    std::string raw_current_line_;
    std::vector<std::string> lines_to_draw_;
    bool paused_ = true;
    TuiLayout layout_ = TuiLayout::Split;
    std::string status_override_;
    int scroll_offset_{0};

    // Performance tracking
    std::chrono::steady_clock::time_point last_speed_update_;
    uint64_t last_icount_ = 0;
    uint64_t speed_ips_ = 0;
    uint64_t kips_ = 0;
    std::vector<uint64_t> kips_history_;
};

} // namespace simrv::tui
