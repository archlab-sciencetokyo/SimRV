#include "simrv/util/FormatUtil.hpp"
/**
 * @file Tui.cpp
 * @brief Interactive TUI console dashboard implementation with premium double-line borders and
 * resolved registers.
 */
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <print>
#include <string>

#include "simrv/core/Machine.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/tui/TuiComponents.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::tui {

static struct termios g_saved_termios; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_termios_saved = false;   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_tui_active = false;      // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

extern "C" void emergency_terminal_restore() {
    if (g_tui_active) {
        const char* shutdown_seq = "\033[?1006l\033[?1000l\033[?25h\033[?1049l\n";
        (void)(::write(STDOUT_FILENO, shutdown_seq, std::strlen(shutdown_seq)) == 0);
        g_tui_active = false;
    }
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
    }
}

static void handle_termination_signal(int sig) {
    emergency_terminal_restore();
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

volatile std::sig_atomic_t g_resized = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

namespace {

void handle_sigwinch(int sig) {
    (void)sig;
    g_resized = 1;
}

auto make_repeated_string(const std::string& pattern, int count) -> std::string {
    std::string s;
    for (int i = 0; i < count; ++i) {
        s += pattern;
    }
    return s;
}



}  // namespace

Tui::Tui(simrv::core::Machine& machine) : machine_(machine) {
    last_speed_update_ = std::chrono::steady_clock::now();
    vt_.set_scroll_offset_callback([this](int lines) -> void {
        if (scroll_offset_ > 0) {
            scroll_offset_ += lines;
        }
    });
    initialize();
}

Tui::~Tui() { shutdown(); }

void Tui::set_paused(bool p) {
    if (paused_ != p) {
        paused_ = p;
        if (!p) {
            status_override_.clear();
            last_runtime_tick_ = std::chrono::steady_clock::now();
        } else {
            if (last_runtime_tick_ != std::chrono::steady_clock::time_point{}) {
                runtime_duration_ += std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - last_runtime_tick_);
                last_runtime_tick_ = std::chrono::steady_clock::time_point{};
            }
        }
    }
}

void Tui::initialize() {
    reg_pane_ = std::make_unique<RegisterPane>(machine_);
    console_pane_ = std::make_unique<ConsolePane>();
    status_bar_ = std::make_unique<StatusBar>(machine_);

    reg_pane_->update_cache();

    if (!g_termios_saved) {
        tcgetattr(STDIN_FILENO, &g_saved_termios);
        g_termios_saved = true;
        std::atexit(emergency_terminal_restore);

        std::signal(SIGINT, handle_termination_signal);
        std::signal(SIGTERM, handle_termination_signal);
        std::signal(SIGSEGV, handle_termination_signal);
        std::signal(SIGABRT, handle_termination_signal);
        std::signal(SIGILL, handle_termination_signal);
        std::signal(SIGFPE, handle_termination_signal);
        std::signal(SIGHUP, handle_termination_signal);
        std::signal(SIGQUIT, handle_termination_signal);
    }

    struct termios term = g_saved_termios;
    term.c_lflag &= ~ICANON;
    term.c_lflag &= ~ECHO;
    term.c_cc[VMIN] = 1;
    term.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &term);

    g_tui_active = true;

    const char* init_seq = "\033[?1049h\033[2J\033[H\033[?1000h\033[?1006h\033[?25l";
    (void)(::write(STDOUT_FILENO, init_seq, strlen(init_seq)) == 0);

    struct sigaction sa{};
    sa.sa_handler = handle_sigwinch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);

    print_log("SimRV Simulator Debug Dashboard Init...\n");
    print_log("Please press [Ctrl+P] to start/pause, [Space/s/S] to step, [Ctrl+Q] to quit.\n");
    simrv::log::set_tui_callback([this](const std::string& msg) -> void {
        print_log(msg);
    });
}

void Tui::shutdown() {
    simrv::log::set_tui_callback(nullptr);
    emergency_terminal_restore();

    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);
}

void Tui::handle_char_write(char ch) {
    vt_.write_char(ch);
}

void Tui::print_log(const std::string& msg) {
    vt_.write_string(msg);
}

void Tui::render() {
    if (!reg_pane_ || !console_pane_ || !status_bar_) {
        return;
    }

    if (g_resized) {
        g_resized = 0;
    }

    struct winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w); // NOLINT(cppcoreguidelines-pro-type-vararg)
    int term_width = w.ws_col;
    int term_height = w.ws_row;

    if (term_width < 40 || term_height < 10) return;

    // Track KIPS and notify RegisterPane
    auto now = std::chrono::steady_clock::now();

    // Update active runtime duration if running
    if (!paused_) {
        if (last_runtime_tick_ != std::chrono::steady_clock::time_point{}) {
            runtime_duration_ += std::chrono::duration_cast<std::chrono::microseconds>(now - last_runtime_tick_);
        }
        last_runtime_tick_ = now;
    } else {
        last_runtime_tick_ = std::chrono::steady_clock::time_point{};
    }

    auto diff =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_speed_update_).count();
    if (diff >= 500) {
        uint64_t current_icount = machine_.cpu.e_icount;
        uint64_t insns_since_last = current_icount - last_icount_;
        speed_ips_ = (insns_since_last * 1000ULL) / static_cast<uint64_t>(diff);
        kips_ = speed_ips_ / 1000;
        kips_history_.push_back(kips_);
        if (kips_history_.size() > 60) {
            kips_history_.erase(kips_history_.begin());
        }
        last_icount_ = current_icount;
        last_speed_update_ = now;
    }

    int left_pane_width = (term_width > 120) ? 75 : 62;
    pane_width_cached_ = left_pane_width;
    int right_pane_width = term_width - left_pane_width - 3;
    if (layout_ == TuiLayout::FullConsole) {
        left_pane_width = 0;
        right_pane_width = term_width - 2;
    } else if (layout_ == TuiLayout::FullRegister) {
        left_pane_width = term_width - 2;
        right_pane_width = 0;
    }

    // StatusBar renders 3 header lines + 2 footer lines, and we add 1 separator before footer.
    int num_rows = term_height - 6;

    // Rebuild visible console rows from virtual terminal state.
    lines_to_draw_.clear();
    if (right_pane_width > 0 && num_rows > 0) {
        vt_.resize(right_pane_width, num_rows);

        int total = vt_.get_lines_count();
        int end_exclusive = total - scroll_offset_;
        if (end_exclusive < 0) {
            end_exclusive = 0;
        }
        int start = end_exclusive - num_rows;
        if (start < 0) {
            start = 0;
        }

        int cursor_abs_line = vt_.get_scrollback_size() + vt_.get_cursor_y();
        bool is_live = (scroll_offset_ == 0);

        for (int i = start; i < end_exclusive; ++i) {
            bool draw_cursor = is_live && (i == cursor_abs_line) && vt_.is_cursor_visible();
            lines_to_draw_.push_back(vt_.get_line_as_string(i, right_pane_width, draw_cursor));
        }

        while (lines_to_draw_.size() < static_cast<std::size_t>(num_rows)) {
            lines_to_draw_.emplace_back(static_cast<std::size_t>(right_pane_width), ' ');
        }
    }

    // Update panes state
    reg_pane_->set_kips(kips_);
    reg_pane_->set_kips_history(kips_history_);
    reg_pane_->set_paused(paused_);
    reg_pane_->set_visible_rows(num_rows);
    reg_pane_->set_active_runtime(static_cast<double>(runtime_duration_.count()) / 1000000.0);
    console_pane_->set_lines(lines_to_draw_);
    console_pane_->set_scroll_offset(scroll_offset_);

    status_bar_->set_paused(paused_);
    status_bar_->set_status_override(status_override_);
    status_bar_->update_kips(kips_);
    status_bar_->set_layout(layout_);
    status_bar_->set_active_page(reg_pane_->get_page());
    status_bar_->set_scroll_offset(scroll_offset_);
    status_bar_->set_pane_widths(left_pane_width, right_pane_width);

    // Render loop
    std::string screen = "\033[?25l\033[H";
    screen += status_bar_->render_row(0, term_width);

    for (int i = 0; i < num_rows; ++i) {
        if (layout_ == TuiLayout::Split) {
            std::string left = reg_pane_->render_row(i, left_pane_width);
            std::string right = console_pane_->render_row(i, right_pane_width);
            screen += std::format("{}║\033[0m{}{}│\033[0m{}{}║\033[0m\n", kSakuraBorder, left, kSakuraBorder, right, kSakuraBorder);
        } else if (layout_ == TuiLayout::FullConsole) {
            std::string right = console_pane_->render_row(i, right_pane_width);
            screen += std::format("{}║\033[0m{}{}║\033[0m\n", kSakuraBorder, right, kSakuraBorder);
        } else {
            std::string left = reg_pane_->render_row(i, left_pane_width);
            screen += std::format("{}║\033[0m{}{}║\033[0m\n", kSakuraBorder, left, kSakuraBorder);
        }
    }

    if (layout_ == TuiLayout::Split) {
        screen += std::format("{}╠{}╧{}╣\033[0m\n", kSakuraBorder, make_repeated_string("═", left_pane_width), make_repeated_string("═", right_pane_width));
    } else {
        screen += std::format("{}╠{}╣\033[0m\n", kSakuraBorder, make_repeated_string("═", term_width - 2));
    }

    screen += status_bar_->render_row(1, term_width);

    (void)(::write(STDOUT_FILENO, screen.data(), screen.size()) == 0);
    ::fflush(stdout);
}

void Tui::handle_mouse(int x, int y, int b) {
    if (!console_pane_ || !reg_pane_) {
        return;
    }

    (void)y;
    if (x < pane_width_cached_) {
        if (b == 64) {
            reg_pane_->scroll(-2);
            render();
        } else if (b == 65) {
            reg_pane_->scroll(2);
            render();
        }
    } else {
        if (b == 64) {
            scroll_offset_ += 5;
            console_pane_->set_scroll_offset(scroll_offset_);
            render();
        } else if (b == 65) {
            if (scroll_offset_ > 0) {
                scroll_offset_ -= 5;
                if (scroll_offset_ < 0) scroll_offset_ = 0;
                console_pane_->set_scroll_offset(scroll_offset_);
                render();
            }
        }
    }
}

void Tui::cycle_reg_page() {
    bool has_f = (machine_.cpu.state().misa & (1ULL << ('f' - 'a'))) != 0;
    bool has_d = (machine_.cpu.state().misa & (1ULL << ('d' - 'a'))) != 0;
    bool has_v = (machine_.cpu.state().misa & (1ULL << ('v' - 'a'))) != 0;
    TuiRegPage rp = reg_pane_->get_page();
    if (rp == TuiRegPage::GPR) {
        if (has_f || has_d)
            rp = TuiRegPage::FPR;
        else if (has_v)
            rp = TuiRegPage::VEC;
        else
            rp = TuiRegPage::PIPELINE;
    } else if (rp == TuiRegPage::FPR) {
        if (has_v)
            rp = TuiRegPage::VEC;
        else
            rp = TuiRegPage::PIPELINE;
    } else if (rp == TuiRegPage::VEC) {
        rp = TuiRegPage::PIPELINE;
    } else {
        rp = TuiRegPage::GPR;
    }
    reg_pane_->set_page(rp);
    render();
}

void Tui::scroll(int lines) {
    scroll_offset_ += lines;
    if (scroll_offset_ < 0) {
        scroll_offset_ = 0;
    }
    render();
}

void Tui::reset_scroll() {
    scroll_offset_ = 0;
    render();
}

void Tui::scroll_regs(int lines) {
    if (reg_pane_) {
        reg_pane_->scroll(lines);
        render();
    }
}

void Tui::reset_scroll_regs() {
    if (reg_pane_) {
        reg_pane_->reset_scroll();
        render();
    }
}

void Tui::update_cache() {
    if (reg_pane_) {
        reg_pane_->update_cache();
    }
}

}  // namespace simrv::tui
