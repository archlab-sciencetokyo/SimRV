/**
 * @file Tui.cpp
 * @brief Interactive TUI console dashboard implementation with premium double-line borders and
 * resolved registers.
 */
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <cctype>
#include <charconv>
#include <thread>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>

#include "simrv/core/Machine.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/tui/TuiKey.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/tui/LeftPane.hpp"
#include "simrv/tui/RightPane.hpp"
#include "simrv/device/Framebuffer.hpp"
#include "simrv/tui/StatusBar.hpp"
#include "simrv/xlen/Types.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/device/Framebuffer.hpp"

namespace simrv::tui {

static struct termios g_saved_termios; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_termios_saved = false;   // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_tui_active = false;      // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

extern "C" void emergency_terminal_restore() {
    std::fflush(stdout);
    if (g_tui_active) {
        const char* shutdown_seq = "\033[0m\033[?1006l\033[?1000l\033[?25h\033[2J\033[H\033[?1049l\n";
        (void)(::write(STDOUT_FILENO, shutdown_seq, std::strlen(shutdown_seq)) == 0);
        g_tui_active = false;
    }
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
    }
}

static void handle_termination_signal(int sig) {
    if (g_tui_active) {
        using namespace std::string_view_literals;
        auto constexpr shutdown_seq = "\033[0m\033[?1006l\033[?1000l\033[?25h\033[2J\033[H\033[?1049l\n"sv;
        (void)(::write(STDOUT_FILENO, shutdown_seq.data(), shutdown_seq.size()) == 0);
        g_tui_active = false;
    }
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
    }
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

volatile std::sig_atomic_t g_resized = 0; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

namespace {

void handle_sigwinch(int sig) {
    (void)sig;
    g_resized = 1;
}

}  // namespace

Tui::Tui(simrv::core::Machine& machine) : machine_(machine) {
    main_thread_id_ = std::this_thread::get_id();
    last_speed_update_ = std::chrono::steady_clock::now();
    trace_enabled_.store(false, std::memory_order_relaxed);
    right_panel_mode_.store(TuiRightPanelMode::Terminal, std::memory_order_relaxed);
    trace_record_buffer_.resize(Tui::kTraceBufferSize);
    update_trace_active_cache();
    vt_.set_scroll_offset_callback([this](int lines) -> void {
        if (scroll_offset_ > 0) {
            scroll_offset_ += lines;
        }
    });
    vt_log_.set_scroll_offset_callback([this](int lines) -> void {
        if (scroll_offset_ > 0) {
            scroll_offset_ += lines;
        }
    });
}

Tui::~Tui() { shutdown(); }

void Tui::set_paused(bool p) {
    if (paused_ != p) {
        paused_ = p;
        if (!p) {
            status_override_.clear();
            last_runtime_tick_ = std::chrono::steady_clock::now();
            last_speed_update_ = std::chrono::steady_clock::now();
            last_icount_ = machine_.cpu.e_icount;
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
    left_pane_ = std::make_unique<LeftPane>(machine_);
    right_pane_ = std::make_unique<RightPane>();
    status_bar_ = std::make_unique<StatusBar>(machine_);

    set_high_contrast(machine_.s_high_contrast);

    left_pane_->update_cache();

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
    term.c_lflag &= ~ISIG;
    term.c_cc[VMIN] = 1;
    term.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &term);

    // Query character cell size in pixels: ESC [ 16 t
    (void)(::write(STDOUT_FILENO, "\033[16t", 5) == 0);
    
    // Read the response from stdin in raw mode with 100ms timeout
    std::string resp;
    char query_ch = 0;
    auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count() < 100) {
        fd_set read_fds;
        struct timeval tv{.tv_sec = 0, .tv_usec = 10000}; // 10ms
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        if (select(STDIN_FILENO + 1, &read_fds, nullptr, nullptr, &tv) > 0) {
            if (::read(STDIN_FILENO, &query_ch, 1) == 1) {
                resp.push_back(query_ch);
                if (query_ch == 't') break;
            }
        }
    }

    if (resp.size() >= 8 && resp.starts_with("\033[6;") && resp.back() == 't') {
        std::string_view payload(resp.data() + 4, resp.size() - 5);
        size_t semi = payload.find(';');
        if (semi != std::string_view::npos) {
            int q_height = 0;
            int q_width = 0;
            std::string_view h_str = payload.substr(0, semi);
            std::string_view w_str = payload.substr(semi + 1);
            auto res_h = std::from_chars(h_str.data(), h_str.data() + h_str.size(), q_height);
            auto res_w = std::from_chars(w_str.data(), w_str.data() + w_str.size(), q_width);
            if (res_h.ec == std::errc{} && res_w.ec == std::errc{} && q_height > 0 && q_width > 0) {
                cell_height_px_ = q_height;
                cell_width_px_ = q_width;
            }
        }
    }

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
    std::scoped_lock lock(io_mutex_);
    tx_fifo_.push(ch);
}

void Tui::print_log(const std::string& msg) {
    std::scoped_lock lock(io_mutex_);
    log_fifo_.push(msg);
}

void Tui::render(bool force) {
    std::unique_lock<std::mutex> lock(tui_mutex_);

    std::queue<char> local_tx;
    std::queue<std::string> local_log;
    {
        std::scoped_lock io_lock(io_mutex_);
        std::swap(tx_fifo_, local_tx);
        std::swap(log_fifo_, local_log);
    }

    const bool has_tx = !local_tx.empty();
    const bool has_log = !local_log.empty();

    // Drain queues without holding io_mutex_
    while (!local_tx.empty()) {
        vt_.write_char(local_tx.front());
        local_tx.pop();
    }
    while (!local_log.empty()) {
        vt_log_.write_string(local_log.front());
        local_log.pop();
    }

    if (!left_pane_ || !right_pane_ || !status_bar_) {
        return;
    }

    TuiRightPanelMode const panel_mode = right_panel_mode_.load(std::memory_order_relaxed);

    static auto last_draw_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_draw_time).count();

    // Flood-proof dual-rate rendering throttle:
    // - Always respect force and g_resized events.
    // - During active simulation or I/O events, limit frame rate to ~60 FPS (16ms) to keep updates responsive.
    // - During paused idle state, render at most once every 200ms (5 FPS) to conserve CPU.
    if (!force && !g_resized) {
        bool const is_active = !paused_ || has_tx || has_log;
        if (is_active && elapsed_ms < 16) {
            return;
        }
        if (!is_active && elapsed_ms < 200) {
            return;
        }
    }
    last_draw_time = now;

    if (g_resized) {
        g_resized = 0;
    }

    // Asynchronously format trace records in TUI thread if trace is active
    if (trace_or_livetrace_active_.load(std::memory_order_relaxed)) {
        std::vector<TraceRecord> local_records;
        {
            std::scoped_lock tr_lock(trace_mutex_);
            local_records.reserve(trace_buffer_size_);
            size_t idx = trace_buffer_tail_;
            for (size_t i = 0; i < trace_buffer_size_; ++i) {
                local_records.push_back(trace_record_buffer_[idx]);
                idx = (idx + 1) % kTraceBufferSize;
            }
        }
        trace_buffer_.clear();
        for (const auto& rec : local_records) {
            trace_buffer_.push_back(format_trace_record(rec));
        }
    }

    struct winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w); // NOLINT(cppcoreguidelines-pro-type-vararg)
    int term_width = w.ws_col;
    int term_height = w.ws_row;

    if (term_width < 40 || term_height < 10) return;

    // Track KIPS and notify RegisterPane
    now = std::chrono::steady_clock::now();

    // Update active runtime duration if running
    if (!paused_) {
        if (last_runtime_tick_ != std::chrono::steady_clock::time_point{}) {
            runtime_duration_ += std::chrono::duration_cast<std::chrono::microseconds>(now - last_runtime_tick_);
        }
        last_runtime_tick_ = now;
    } else {
        last_runtime_tick_ = std::chrono::steady_clock::time_point{};
    }

    if (!paused_) {
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
    } else {
        kips_ = 0;
    }

    int left_pane_width = user_left_pane_width_ > 0 ? user_left_pane_width_ : ((term_width > 120) ? 75 : 62);
    if (left_pane_width < 40) left_pane_width = 40;
    if (left_pane_width > term_width - 10) left_pane_width = std::max(40, term_width - 10);
    pane_width_cached_ = left_pane_width;
    int right_pane_width = term_width - left_pane_width - 3;
    if (layout_ == TuiLayout::FullRight) {
        left_pane_width = 0;
        right_pane_width = term_width - 2;
    } else if (layout_ == TuiLayout::FullLeft) {
        left_pane_width = term_width - 2;
        right_pane_width = 0;
    }

    // StatusBar renders 3 header lines + 2 footer lines, and we add 1 separator before footer.
    int num_rows = term_height - 6;

    // Limit scrollback to the existing rows
    {
        int total = (panel_mode == TuiRightPanelMode::Terminal) ? vt_.get_lines_count() : 0;
        int max_scroll = std::max(0, total - num_rows);
        if (scroll_offset_ > max_scroll) {
            scroll_offset_ = max_scroll;
        }
    }

    // Rebuild visible console/display rows depending on right panel mode.
    lines_to_draw_.clear();
    if (right_pane_width > 0 && num_rows > 0) {
        if (panel_mode == TuiRightPanelMode::Terminal) {
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
        } else if (panel_mode == TuiRightPanelMode::Display) {
            for (int i = 0; i < num_rows; ++i) {
                lines_to_draw_.emplace_back(static_cast<size_t>(right_pane_width), ' ');
            }
        }

        while (lines_to_draw_.size() < static_cast<std::size_t>(num_rows)) {
            lines_to_draw_.emplace_back(static_cast<std::size_t>(right_pane_width), ' ');
        }
    }

    // Prepare log lines for left_pane_
    vt_log_.resize(300, num_rows);
    std::vector<std::string> log_lines;
    int total_written = vt_log_.get_scrollback_size() + vt_log_.get_cursor_y();
    if (vt_log_.get_cursor_x() > 0) {
        total_written += 1;
    }
    int start_log = std::max(0, total_written - 20);
    for (int i = start_log; i < total_written; ++i) {
        log_lines.push_back(vt_log_.get_line_as_string(i, 300, false));
    }

    // Update panes state
    left_pane_->set_kips(kips_);
    left_pane_->set_kips_history(kips_history_);
    left_pane_->set_paused(paused_);
    left_pane_->set_visible_rows(num_rows);
    left_pane_->set_active_runtime(static_cast<double>(runtime_duration_.count()) / 1000000.0);
    left_pane_->set_trace_buffer(&trace_buffer_);
    left_pane_->set_log_lines(std::move(log_lines));
    right_pane_->set_lines(lines_to_draw_);
    right_pane_->set_scroll_offset(scroll_offset_);

    status_bar_->set_paused(paused_);
    status_bar_->set_status_override(status_override_);
    status_bar_->update_kips(kips_);
    status_bar_->set_layout(layout_);
    status_bar_->set_active_page(left_pane_->get_page());
    status_bar_->set_scroll_offset(scroll_offset_);
    status_bar_->set_pane_widths(left_pane_width, right_pane_width);
    status_bar_->set_right_panel_mode(panel_mode);
    status_bar_->set_trace_enabled(trace_enabled_.load(std::memory_order_relaxed));

    lock.unlock();

    // Render loop
    std::vector<std::string> new_lines;
    new_lines.reserve(static_cast<size_t>(num_rows) + 6);

    auto append_split_lines = [&](const std::string& str) -> void {
        size_t start = 0;
        while (start < str.size()) {
            size_t pos = str.find('\n', start);
            if (pos == std::string::npos) {
                new_lines.push_back(str.substr(start));
                break;
            }
            new_lines.push_back(str.substr(start, pos - start));
            start = pos + 1;
        }
    };

    append_split_lines(status_bar_->render_row(0, term_width));

    for (int i = 0; i < num_rows; ++i) {
        if (layout_ == TuiLayout::Split) {
            std::string left = left_pane_->render_row(i, left_pane_width);
            std::string right = right_pane_->render_row(i, right_pane_width);
            new_lines.push_back(std::format("{}║\033[0m{}{}│\033[0m{}{}║\033[0m", kThemeBorder, left, kThemeBorder, right, kThemeBorder));
        } else if (layout_ == TuiLayout::FullRight) {
            std::string right = right_pane_->render_row(i, right_pane_width);
            new_lines.push_back(std::format("{}║\033[0m{}{}║\033[0m", kThemeBorder, right, kThemeBorder));
        } else {
            std::string left = left_pane_->render_row(i, left_pane_width);
            new_lines.push_back(std::format("{}║\033[0m{}{}║\033[0m", kThemeBorder, left, kThemeBorder));
        }
    }

    if (layout_ == TuiLayout::Split) {
        new_lines.push_back(std::format("{}╠{}╧{}╣\033[0m", kThemeBorder, make_repeated_string("═", left_pane_width), make_repeated_string("═", right_pane_width)));
    } else {
        new_lines.push_back(std::format("{}╠{}╣\033[0m", kThemeBorder, make_repeated_string("═", term_width - 2)));
    }

    append_split_lines(status_bar_->render_row(1, term_width));

    render_modal_overlay(new_lines, term_width, term_height);

    std::string update_cmds = "\033[?25l";

    if (force || last_screen_lines_.size() != new_lines.size()) {
        update_cmds += "\033[H";
        for (size_t i = 0; i < new_lines.size(); ++i) {
            update_cmds += new_lines[i];
            if (i + 1 < new_lines.size()) {
                update_cmds += "\n";
            }
        }
        last_screen_lines_ = new_lines;
    } else {
        for (size_t i = 0; i < new_lines.size(); ++i) {
            // In Display mode with Split layout, skip diff-updating body rows
            // because we will write the left pane body rows atomically at the end.
            if (panel_mode == TuiRightPanelMode::Display && layout_ == TuiLayout::Split &&
                i >= 3 && i < 3 + static_cast<size_t>(num_rows)) {
                continue;
            }
            if (new_lines[i] != last_screen_lines_[i]) {
                update_cmds += std::format("\033[{};1H", i + 1);
                update_cmds += new_lines[i];
                last_screen_lines_[i] = new_lines[i];
            }
        }
    }

    if (panel_mode == TuiRightPanelMode::Display && layout_ == TuiLayout::Split) {
        // 1. Draw Sixel graphic if framebuffer is dirty
        if (machine_.framebuffer && (force || machine_.framebuffer->is_tui_dirty())) {
            int active_w = 0;
            int active_h = 0;
            bool has_content = machine_.framebuffer->get_active_bounds(active_w, active_h);

            // Only render Sixel when framebuffer has actual drawn content
            if (has_content && active_w > 1 && active_h > 1) {
                // Optimize layout boundaries to maximize Sixel area
                int const avail_cols = right_pane_width - 4;  // borders + margin
                int const avail_rows = num_rows - 2;          // borders + margin (leaves 1 row top/bottom)

                if (avail_cols >= 4 && avail_rows >= 4) {
                    // Determine character cell dimensions in pixels
                    int cell_w = cell_width_px_;
                    int cell_h = cell_height_px_;
                    if (cell_w <= 0 || cell_h <= 0) {
                        struct winsize ws{};
                        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && // NOLINT(cppcoreguidelines-pro-type-vararg)
                            ws.ws_xpixel > 0 && ws.ws_ypixel > 0 &&
                            ws.ws_col > 0 && ws.ws_row > 0) {
                            cell_w = ws.ws_xpixel / ws.ws_col;
                            cell_h = ws.ws_ypixel / ws.ws_row;
                        }
                    }
                    if (cell_w <= 0 || cell_h <= 0) {
                        cell_w = 8;
                        cell_h = 16;
                    }

                    // Utilize full available pane area with a 4-pixel buffer for robustness
                    int target_w = avail_cols * cell_w - 4;
                    int target_h = avail_rows * cell_h - 4;

                    // Fit to framebuffer aspect ratio
                    double aspect = static_cast<double>(active_w) / active_h;
                    if (aspect <= 0.1) aspect = 1.6;

                    int fit_h = static_cast<int>(target_w / aspect);
                    if (fit_h > target_h) {
                        target_w = static_cast<int>(target_h * aspect);
                    } else {
                        target_h = fit_h;
                    }

                    // Sixel height must be multiple of 6
                    target_h = (target_h / 6) * 6;
                    if (target_h < 6) target_h = 6;
                    if (target_w < 6) target_w = 6;

                    // Compute cells occupied by the Sixel image
                    int const img_w_cells = (target_w + cell_w - 1) / cell_w;
                    int const img_h_cells = (target_h + cell_h - 1) / cell_h;

                    // Position: Center the image inside the available pane space
                    int const rem_cols = avail_cols - img_w_cells;
                    int const rem_rows = avail_rows - img_h_cells;

                    int const right_pane_active_col = (layout_ == TuiLayout::Split) ? (left_pane_width + 3 + 2) : 4;
                    int const img_col = right_pane_active_col + std::max(0, rem_cols / 2);
                    int const img_row = 5 + std::max(0, rem_rows / 2);

                    std::string sixel_graphics = "\0337"; // Save cursor (DEC)
                    sixel_graphics += std::format("\033[{};{}H", img_row, img_col);
                    sixel_graphics += machine_.framebuffer->get_sixel_escape(target_w, target_h);
                    sixel_graphics += "\0338"; // Restore cursor (DEC)
                    update_cmds += sixel_graphics;
                }
            }
        }

        // 2. Always rewrite/restore the left pane body text cells to columns 1..left_pane_width+2
        // to overlay them on top of any cells cleared by the Sixel graphic draw.
        for (int i = 0; i < num_rows; ++i) {
            std::string left = left_pane_->render_row(i, left_pane_width);
            update_cmds += std::format("\033[{};1H{}║\033[0m{}{}│\033[0m", i + 4, kThemeBorder, left, kThemeBorder);
        }

        update_cmds += std::format("\033[{};1H", term_height); // Park cursor
    }

    (void)(::write(STDOUT_FILENO, update_cmds.data(), update_cmds.size()) == 0);
    ::fflush(stdout);
}

void Tui::handle_mouse(int x, int y, int b) {
    if (!right_pane_ || !left_pane_) {
        return;
    }

    (void)y;
    if (x < pane_width_cached_) {
        if (b == 64) {
            scroll_regs(-2);
        } else if (b == 65) {
            scroll_regs(2);
        }
    } else {
        if (b == 64) {
            scroll(5);
        } else if (b == 65) {
            scroll(-5);
        }
    }
}

void Tui::cycle_reg_page() {
    bool has_f = (machine_.cpu.state().misa & (1ULL << ('f' - 'a'))) != 0;
    bool has_d = (machine_.cpu.state().misa & (1ULL << ('d' - 'a'))) != 0;
    bool has_v = (machine_.cpu.state().misa & (1ULL << ('v' - 'a'))) != 0;
    TuiRegPage rp = left_pane_->get_page();

    // If currently on a tool tab, jump back to GPR
    if (rp != TuiRegPage::GPR && rp != TuiRegPage::FPR && rp != TuiRegPage::VEC) {
        rp = TuiRegPage::GPR;
    } else {
        // Cycle within register sub-views only
        switch (rp) {
            case TuiRegPage::GPR:
                if (has_f || has_d) rp = TuiRegPage::FPR;
                else if (has_v)     rp = TuiRegPage::VEC;
                // else stay on GPR (no fp/vec extensions)
                break;
            case TuiRegPage::FPR:
                if (has_v) rp = TuiRegPage::VEC;
                else       rp = TuiRegPage::GPR;
                break;
            case TuiRegPage::VEC:
                rp = TuiRegPage::GPR;
                break;
            default:
                rp = TuiRegPage::GPR;
                break;
        }
    }
    left_pane_->set_page(rp);
    update_trace_active_cache();
    render(true);
}

void Tui::cycle_tool_page() {
    TuiRegPage rp = left_pane_->get_page();
    switch (rp) {
        case TuiRegPage::GPR:
        case TuiRegPage::FPR:
        case TuiRegPage::VEC:
            rp = TuiRegPage::PIPELINE;
            break;
        case TuiRegPage::PIPELINE:
            rp = TuiRegPage::CACHE;
            break;
        case TuiRegPage::CACHE:
            rp = TuiRegPage::TRACE;
            break;
        case TuiRegPage::TRACE:
            rp = TuiRegPage::EXPLAIN;
            break;
        case TuiRegPage::EXPLAIN:
            rp = TuiRegPage::STACK;
            break;
        case TuiRegPage::STACK:
        default:
            rp = TuiRegPage::GPR;
            break;
    }
    left_pane_->set_page(rp);
    update_trace_active_cache();
    render(true);
}

void Tui::set_reg_page(TuiRegPage page) {
    if (left_pane_) {
        left_pane_->set_page(page);
        update_trace_active_cache();
        render(true);
    }
}

void Tui::toggle_explain() {
    if (left_pane_) {
        if (left_pane_->get_page() == TuiRegPage::EXPLAIN) {
            left_pane_->set_page(TuiRegPage::GPR);
        } else {
            left_pane_->set_page(TuiRegPage::EXPLAIN);
        }
        update_trace_active_cache();
        render(true);
    }
}

void Tui::toggle_high_contrast() {
    machine_.s_high_contrast = !machine_.s_high_contrast;
    set_high_contrast(machine_.s_high_contrast);
    render(true);
}

void Tui::toggle_sakura_theme() {
    if (get_tui_theme() == TuiTheme::Sakura) {
        if (machine_.s_high_contrast) {
            set_tui_theme(TuiTheme::HighContrast);
        } else {
            set_tui_theme(TuiTheme::Adaptive);
        }
    } else {
        set_tui_theme(TuiTheme::Sakura);
    }
    render(true);
}

void Tui::cycle_right_panel_mode() {
    TuiRightPanelMode current = right_panel_mode_.load(std::memory_order_relaxed);
    TuiRightPanelMode next = (current == TuiRightPanelMode::Terminal) ? TuiRightPanelMode::Display : TuiRightPanelMode::Terminal;
    right_panel_mode_.store(next, std::memory_order_relaxed);
    scroll_offset_ = 0;
    render(true);
}

void Tui::toggle_trace_enabled() {
    trace_enabled_.store(!trace_enabled_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    update_trace_active_cache();
    render(true);
}

void Tui::update_trace_active_cache() {
    bool trace_page_active = left_pane_ && (left_pane_->get_page() == TuiRegPage::TRACE);
    trace_or_livetrace_active_.store(
        trace_enabled_.load(std::memory_order_relaxed) || trace_page_active,
        std::memory_order_relaxed
    );
}

void Tui::record_instruction(Register pc, simrv::isa::Opcode opcode, simrv::isa::OperationId op_id, uint8_t rd, Register rd_val,
                              uint8_t rs1, Register rs1_val, uint8_t rs2, Register rs2_val,
                              int64_t imm) {
    std::scoped_lock lock(trace_mutex_);
    if (trace_record_buffer_.empty()) {
        trace_record_buffer_.resize(kTraceBufferSize);
    }
    trace_record_buffer_[trace_buffer_head_] = TraceRecord{
        .pc = pc,
        .opcode = opcode,
        .op_id = op_id,
        .rd = rd,
        .rd_val = rd_val,
        .rs1 = rs1,
        .rs1_val = rs1_val,
        .rs2 = rs2,
        .rs2_val = rs2_val,
        .imm = imm
    };
    trace_buffer_head_ = (trace_buffer_head_ + 1) % kTraceBufferSize;
    if (trace_buffer_size_ < kTraceBufferSize) {
        trace_buffer_size_++;
    } else {
        trace_buffer_tail_ = (trace_buffer_tail_ + 1) % kTraceBufferSize;
    }
}

auto Tui::format_trace_record(const TraceRecord& rec) -> std::string {
    std::string op_name;
    if (static_cast<std::size_t>(rec.op_id) < simrv::pipeline::OPERATION_NAME.size()) {
        std::string_view name_sv = simrv::pipeline::OPERATION_NAME.at(static_cast<std::size_t>(rec.op_id));
        for (char c : name_sv) {
            op_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    } else {
        op_name = "unknown";
    }

    for (char& c : op_name) {
        if (c == '_') c = '.';
    }

    const bool rd_fp = isa::is_destination_fp(rec.opcode, rec.op_id);
    const bool rs1_fp = isa::is_rs1_fp(rec.opcode, rec.op_id);
    const bool rs2_fp = isa::is_rs2_fp(rec.opcode, rec.op_id);

    auto get_reg_name = [](uint8_t reg, bool is_reg_fp) -> std::string {
        static constexpr std::array<const char*, 32> abi_names = {
            "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
            "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
            "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
        };
        static constexpr std::array<const char*, 32> fp_names = {
            "ft0", "ft1", "ft2", "ft3", "ft4",  "ft5",  "ft6", "ft7", "fs0",  "fs1", "fa0",
            "fa1", "fa2", "fa3", "fa4", "fa5",  "fa6",  "fa7", "fs2", "fs3",  "fs4", "fs5",
            "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"
        };
        if (reg >= 32) return "??";
        return is_reg_fp ? fp_names[reg] : abi_names[reg];
    };

    std::string inst_str;
    std::string side_effect;

    bool is_load = false;
    bool is_store = false;
    bool is_branch = false;
    bool is_jal = (op_name == "jal");
    bool is_jalr = (op_name == "jalr");
    bool is_lui = (op_name == "lui");
    bool is_auipc = (op_name == "auipc");
    bool is_csr = op_name.starts_with("csr");
    bool is_system = (op_name == "ecall" || op_name == "ebreak" || op_name == "uret" ||
                      op_name == "sret" || op_name == "mret" || op_name == "wfi");

    if (op_name.starts_with("l") && !op_name.starts_with("lui")) {
        is_load = true;
    } else if (op_name.starts_with("s") && !op_name.starts_with("slt") && !op_name.starts_with("sll") &&
               !op_name.starts_with("sra") && !op_name.starts_with("srl") && !op_name.starts_with("sub") &&
               !op_name.starts_with("sret") && !op_name.starts_with("sfence") && !op_name.starts_with("sc")) {
        is_store = true;
    } else if (op_name.starts_with("b") && op_name != "break") {
        is_branch = true;
    }

    if (is_lui || is_auipc) {
        inst_str = std::format("{} {}, {:#x}", op_name, get_reg_name(rec.rd, rd_fp), static_cast<uint32_t>(rec.imm) >> 12);
        side_effect = std::format("{} = {:#x}", get_reg_name(rec.rd, rd_fp), rec.rd_val);
    } else if (is_jal) {
        if (rec.rd == 0) {
            inst_str = std::format("j {:#x}", rec.pc + rec.imm);
        } else {
            inst_str = std::format("{} {}, {:#x}", op_name, get_reg_name(rec.rd, rd_fp), rec.pc + rec.imm);
            side_effect = std::format("{} = {:#x}", get_reg_name(rec.rd, rd_fp), rec.rd_val);
        }
    } else if (is_jalr || is_load) {
        inst_str = std::format("{} {}, {}({})", op_name, get_reg_name(rec.rd, rd_fp), rec.imm, get_reg_name(rec.rs1, rs1_fp));
        side_effect = std::format("{} = {:#x}", get_reg_name(rec.rd, rd_fp), rec.rd_val);
    } else if (is_branch) {
        inst_str = std::format("{} {}, {}, {:#x}", op_name, get_reg_name(rec.rs1, rs1_fp), get_reg_name(rec.rs2, rs2_fp), rec.pc + rec.imm);
    } else if (is_store) {
        inst_str = std::format("{} {}, {}({})", op_name, get_reg_name(rec.rs2, rs2_fp), rec.imm, get_reg_name(rec.rs1, rs1_fp));
        side_effect = std::format("mem[{:#x}] = {:#x}", rec.rs1_val + rec.imm, rec.rs2_val);
    } else if (is_csr) {
        if (op_name.ends_with("i")) {
            inst_str = std::format("{} {}, {:#x}, {}", op_name, get_reg_name(rec.rd, rd_fp), rec.imm & 0xFFF, rec.rs1);
        } else {
            inst_str = std::format("{} {}, {:#x}, {}", op_name, get_reg_name(rec.rd, rd_fp), rec.imm & 0xFFF, get_reg_name(rec.rs1, rs1_fp));
        }
        side_effect = std::format("{} = {:#x}", get_reg_name(rec.rd, rd_fp), rec.rd_val);
    } else if (is_system) {
        inst_str = op_name;
    } else if (op_name.starts_with("amo")) {
        inst_str = std::format("{} {}, {}, ({})", op_name, get_reg_name(rec.rd, rd_fp), get_reg_name(rec.rs2, rec.rs2_val), get_reg_name(rec.rs1, rs1_fp));
        side_effect = std::format("{} = {:#x}", get_reg_name(rec.rd, rd_fp), rec.rd_val);
    } else if (op_name.ends_with("i") || op_name.ends_with("iw")) {
        inst_str = std::format("{} {}, {}, {}", op_name, get_reg_name(rec.rd, rd_fp), get_reg_name(rec.rs1, rs1_fp), rec.imm);
        side_effect = std::format("{} = {:#x}", get_reg_name(rec.rd, rd_fp), rec.rd_val);
    } else {
        inst_str = std::format("{} {}, {}, {}", op_name, get_reg_name(rec.rd, rd_fp), get_reg_name(rec.rs1, rs1_fp), get_reg_name(rec.rs2, rs2_fp));
        if (rec.rd != 0) {
            side_effect = std::format("{} = {:#x}", get_reg_name(rec.rd, rd_fp), rec.rd_val);
        }
    }

    std::string sym = machine_.symbols.lookup(rec.pc);
    std::string line;
    if (sym.empty()) {
        if (side_effect.empty()) {
            line = std::format("{:#x}: {}", rec.pc, inst_str);
        } else {
            line = std::format("{:#x}: {} [{}]", rec.pc, inst_str, side_effect);
        }
    } else {
        if (side_effect.empty()) {
            line = std::format("{:#x} <{}>: {}", rec.pc, sym, inst_str);
        } else {
            line = std::format("{:#x} <{}>: {} [{}]", rec.pc, sym, inst_str, side_effect);
        }
    }
    return line;
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
    if (left_pane_) {
        left_pane_->scroll(lines);
        render();
    }
}

void Tui::reset_scroll_regs() {
    if (left_pane_) {
        left_pane_->reset_scroll();
        render();
    }
}

void Tui::update_cache() {
    if (left_pane_) {
        left_pane_->update_cache();
    }
}

void Tui::adjust_left_pane_width(int delta) {
    struct winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w); // NOLINT(cppcoreguidelines-pro-type-vararg)
    int term_width = w.ws_col;

    int current = user_left_pane_width_ > 0 ? user_left_pane_width_ : ((term_width > 120) ? 75 : 62);
    current += delta;
    if (current < 40) current = 40;
    if (current > term_width - 10) current = std::max(40, term_width - 10);
    user_left_pane_width_ = current;
    render(true);
}

auto Tui::poll_keyboard(uint8_t& byte_out) -> bool {
    constexpr int stdin_fd = STDIN_FILENO;
    fd_set read_fds;
    struct timeval timeout{.tv_sec = 0, .tv_usec = 0};
    FD_ZERO(&read_fds);
    FD_SET(stdin_fd, &read_fds);
    if (select(stdin_fd + 1, &read_fds, nullptr, nullptr, &timeout) <= 0) {
        return false;
    }
    if (!FD_ISSET(stdin_fd, &read_fds)) {
        return false;
    }
    uint8_t byte = 0;
    if (::read(stdin_fd, &byte, 1) != 1) {
        return false;
    }
    byte_out = byte;
    return true;
}

void Tui::update() {
    if (tui_loop_paused_.load(std::memory_order_relaxed)) {
        return;
    }

    uint8_t byte = 0;
    while (poll_keyboard(byte)) {
        if (consume_control_sequence(byte)) {
            continue;
        }

        const auto key = static_cast<simrv::tui::TuiKey>(byte);
        if (key == simrv::tui::TuiKey::CtrlQ) {
            machine_.is_running_ = false;
            return;
        }
        if (key == simrv::tui::TuiKey::CtrlP) {
            pause_loop();
            return;
        }

        if (machine_.uart) {
            machine_.uart->push_rx_byte(byte);
        }
    }
}

void Tui::pause_loop() {
    tui_loop_paused_ = true;
    set_paused(true);

    bool const is_sim_thread = (std::this_thread::get_id() != main_thread_id_);
    if (is_sim_thread) {
        sim_thread_is_sleeping_.store(true, std::memory_order_relaxed);
    }

    if (std::this_thread::get_id() == main_thread_id_) {
        while (!sim_thread_is_sleeping_.load(std::memory_order_relaxed) && tui_loop_paused_ && machine_.is_running_ && !machine_.is_shutdown_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    render(true);

    while (tui_loop_paused_ && (machine_.is_running_ || machine_.is_shutdown_)) {
        if (simrv::tui::g_resized) {
            render(true);
        }
        uint8_t byte = 0;
        if (poll_keyboard(byte)) {
            if (consume_control_sequence(byte)) {
                continue;
            }

            const auto key = static_cast<simrv::tui::TuiKey>(byte);
            if (is_modal_active()) {
                if (byte == 27 || key == simrv::tui::TuiKey::Esc) {
                    close_modal();
                } else if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) {
                    submit_modal();
                } else if (active_modal_ == ModalType::Help && (key == simrv::tui::TuiKey::h || key == simrv::tui::TuiKey::H || key == simrv::tui::TuiKey::QuestionMark)) {
                    close_modal();
                } else if (byte == 8 || byte == 127 || key == simrv::tui::TuiKey::Backspace) {
                    if (!modal_input_.empty()) {
                        modal_input_.pop_back();
                        render(true);
                    }
                } else if (byte >= 32 && byte <= 126) {
                    if (active_modal_ != ModalType::Help) {
                        modal_input_.push_back(static_cast<char>(byte));
                        render(true);
                    }
                }
                continue;
            }

            if (key == simrv::tui::TuiKey::CtrlP || key == simrv::tui::TuiKey::c || key == simrv::tui::TuiKey::C) {
                if (!machine_.is_shutdown_) {
                    tui_loop_paused_ = false;
                }
            } else if (key == simrv::tui::TuiKey::CtrlR) {
                machine_.request_reboot();
                tui_loop_paused_ = false;
            } else if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) {
                reset_scroll();
            } else if (key == simrv::tui::TuiKey::CtrlQ || key == simrv::tui::TuiKey::CtrlC ||
                       key == simrv::tui::TuiKey::q || key == simrv::tui::TuiKey::Q) {
                machine_.is_running_ = false;
                machine_.is_shutdown_ = false;
                tui_loop_paused_ = false;
            } else if (key == simrv::tui::TuiKey::Tab) {
                cycle_layout();
            } else if (key == simrv::tui::TuiKey::r || key == simrv::tui::TuiKey::R) {
                cycle_reg_page();
            } else if (key == simrv::tui::TuiKey::l || key == simrv::tui::TuiKey::L) {
                cycle_tool_page();
            } else if (key == simrv::tui::TuiKey::e || key == simrv::tui::TuiKey::E) {
                toggle_explain();
            } else if (key == simrv::tui::TuiKey::Colon) {
                open_modal(ModalType::SetBreakpoint);
            } else if (key == simrv::tui::TuiKey::g || key == simrv::tui::TuiKey::G) {
                open_modal(ModalType::SetStepSize);
            } else if (key == simrv::tui::TuiKey::f || key == simrv::tui::TuiKey::F) {
                open_modal(ModalType::SetSpeed);
            } else if (key == simrv::tui::TuiKey::m || key == simrv::tui::TuiKey::M) {
                open_modal(ModalType::InspectAddress);
            } else if (key == simrv::tui::TuiKey::QuestionMark || key == simrv::tui::TuiKey::h || key == simrv::tui::TuiKey::H) {
                open_modal(ModalType::Help);
            } else if (key == simrv::tui::TuiKey::LeftBracket) {
                adjust_left_pane_width(-2);
            } else if (key == simrv::tui::TuiKey::RightBracket) {
                adjust_left_pane_width(2);
            } else if (key == simrv::tui::TuiKey::t || key == simrv::tui::TuiKey::T) {
                toggle_sakura_theme();
            } else if (key == simrv::tui::TuiKey::p || key == simrv::tui::TuiKey::P) {
                cycle_right_panel_mode();
            } else if (key == simrv::tui::TuiKey::v || key == simrv::tui::TuiKey::V) {
                toggle_trace_enabled();
            } else if (key == simrv::tui::TuiKey::u || key == simrv::tui::TuiKey::U) {
                scroll(5);
            } else if (key == simrv::tui::TuiKey::d || key == simrv::tui::TuiKey::D) {
                scroll(-5);
            } else if (key == simrv::tui::TuiKey::b || key == simrv::tui::TuiKey::B) {
                if (machine_.cpu.perform_backstep()) {
                    update_cache();
                    render(true);
                }
            } else if (key == simrv::tui::TuiKey::n || key == simrv::tui::TuiKey::N) {
                if (!machine_.is_shutdown_) {
                    uint64_t g = step_granularity_.load(std::memory_order_relaxed);
                    step_budget_.store(g, std::memory_order_relaxed);
                    tui_loop_paused_ = false;
                }
            } else if (key == simrv::tui::TuiKey::k || key == simrv::tui::TuiKey::K) {
                Address pc = machine_.cpu.state().pc;
                if (machine_.breakpoints.has_pc_breakpoint(pc)) {
                    machine_.breakpoints.remove_pc_breakpoint(pc);
                    set_status_override(std::format("Removed breakpoint at 0x{:08x}", pc));
                } else {
                    machine_.breakpoints.add_pc_breakpoint(pc);
                    set_status_override(std::format("Breakpoint set at 0x{:08x}", pc));
                }
                render(true);
            } else if (key == simrv::tui::TuiKey::o || key == simrv::tui::TuiKey::O) {
                machine_.s_rollback_enabled = !machine_.s_rollback_enabled;
                if (!machine_.s_rollback_enabled) {
                    machine_.cpu.undo_stack.clear();
                }
                render(true);
            } else if (key == simrv::tui::TuiKey::Plus || key == simrv::tui::TuiKey::Equal || key == simrv::tui::TuiKey::Dot) {
                static constexpr std::array<uint64_t, 10> kSpeedLevels = {1000000, 500000, 100000, 50000, 10000, 5000, 1000, 100, 10, 0};
                uint64_t cur_delay = step_delay_us_.load(std::memory_order_relaxed);
                uint64_t next_delay = 0;
                for (uint64_t lvl : kSpeedLevels) {
                    if (lvl < cur_delay) {
                        next_delay = lvl;
                        break;
                    }
                }
                step_delay_us_.store(next_delay, std::memory_order_relaxed);
                render(true);
            } else if (key == simrv::tui::TuiKey::Minus || key == simrv::tui::TuiKey::Comma) {
                static constexpr std::array<uint64_t, 10> kSpeedLevels = {0, 10, 100, 1000, 5000, 10000, 50000, 100000, 500000, 1000000};
                uint64_t cur_delay = step_delay_us_.load(std::memory_order_relaxed);
                uint64_t next_delay = 1000000;
                for (uint64_t lvl : kSpeedLevels) {
                    if (lvl > cur_delay) {
                        next_delay = lvl;
                        break;
                    }
                }
                step_delay_us_.store(next_delay, std::memory_order_relaxed);
                render(true);
            } else if (key == simrv::tui::TuiKey::s || key == simrv::tui::TuiKey::S || key == simrv::tui::TuiKey::Space) {
                if (!machine_.is_shutdown_) {
                    update_cache();
                    machine_.prepare_cycle();
                    machine_.cpu.run_cycle(machine_);
                    machine_.finalize_cycle();
                    render(true);
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    update_cache();
    set_paused(false);
    reset_scroll();
    render(true);

    if (is_sim_thread) {
        sim_thread_is_sleeping_.store(false, std::memory_order_relaxed);
    }
}

auto Tui::consume_control_sequence(uint8_t first_byte) -> bool {
    if (first_byte != 0x1b) {
        return false;
    }

    esc_buf_.clear();
    esc_buf_.push_back(static_cast<char>(first_byte));

    constexpr int kMaxSeqLen = 64;
    uint8_t byte = 0;
    while (static_cast<int>(esc_buf_.size()) < kMaxSeqLen) {
        bool polled = false;
        for (int retry = 0; retry < 5; ++retry) {
            if (poll_keyboard(byte)) {
                polled = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!polled) {
            break;
        }
        esc_buf_.push_back(static_cast<char>(byte));
        if (byte == 'M' || byte == 'm' || byte == '~' ||
            (byte >= 'A' && byte <= 'Z' && byte != 'O') ||
            (byte >= 'a' && byte <= 'z')) {
            break;
        }
    }

    // 1. Mouse reporting
    int button = 0;
    int x = 0;
    int y = 0;
    if (parse_sgr_mouse(esc_buf_, button, x, y)) {
        // Double-click prevention: only register left clicks on press event ('M')
        if (esc_buf_.back() == 'M' && button == 0 && y == 2) {
            const auto layout = get_layout();
            bool on_left = false;
            if (layout == TuiLayout::Split) {
                on_left = (x <= get_pane_width());
            } else if (layout == TuiLayout::FullLeft) {
                on_left = true;
            } else {
                on_left = false;
            }

            if (on_left) {
                cycle_reg_page();
            } else {
                if (machine_.is_shutdown_) {
                    machine_.request_reboot();
                    tui_loop_paused_ = false;
                } else {
                    if (tui_loop_paused_) {
                        tui_loop_paused_ = false;
                    } else {
                        pause_loop();
                    }
                }
            }
            return true;
        }

        handle_mouse(x, y, button);
        return true;
    }

    // 2. Alt modifier shortcuts
    if (esc_buf_.size() == 2) {
        char key = esc_buf_.at(1);
        if (key == 'p' || key == 'P') {
            cycle_right_panel_mode();
            return true;
        }
        if (key == 'r' || key == 'R') {
            cycle_reg_page();
            return true;
        }
        if (key == 'l' || key == 'L') {
            cycle_tool_page();
            return true;
        }
        if (key == 'e' || key == 'E') {
            toggle_explain();
            return true;
        }
        if (key == 'v' || key == 'V') {
            toggle_trace_enabled();
            return true;
        }
        if (key == 'h' || key == 'H') {
            toggle_high_contrast();
            return true;
        }
        if (key == 't' || key == 'T') {
            toggle_sakura_theme();
            return true;
        }
        if (key == 'u' || key == 'U') {
            scroll(5);
            return true;
        }
        if (key == 'd' || key == 'D') {
            scroll(-5);
            return true;
        }
        if (key == 'w' || key == 'W') {
            scroll_regs(-2);
            return true;
        }
        if (key == 's' || key == 'S') {
            scroll_regs(2);
            return true;
        }
        if (key == 'z' || key == 'Z') {
            reset_scroll_regs();
            return true;
        }
        if (key == 'c' || key == 'C') {
            reset_scroll();
            return true;
        }
    }

    // 3. Function key (F1) help shortcut
    if (esc_buf_ == "\033OP" || esc_buf_ == "\033[11~" || esc_buf_ == "\033[1;2P" || esc_buf_ == "\033[O1P") {
        if (active_modal_ == ModalType::Help) {
            close_modal();
        } else {
            open_modal(ModalType::Help);
        }
        return true;
    }

    if (esc_buf_.size() == 1) {
        if (is_modal_active()) {
            close_modal();
            return true;
        }
    }

    // 3. Forward all other escape sequences (like arrow keys) to the guest OS
    if (machine_.uart) {
        for (char c : esc_buf_) {
            machine_.uart->push_rx_byte(static_cast<uint8_t>(c));
        }
    }
    return true;
}

auto Tui::parse_sgr_mouse(const std::string& seq, int& b, int& x, int& y) -> bool {
    // Expected SGR mouse format: ESC [ < b ; x ; y M|m
    if (seq.size() < 7 || seq.at(0) != '\x1b' || seq.at(1) != '[' || seq.at(2) != '<') {
        return false;
    }

    const char tail = seq.back();
    if (tail != 'M' && tail != 'm') {
        return false;
    }

    std::string_view payload(seq.data() + 3, seq.size() - 4);
    const std::size_t semi1 = payload.find(';');
    if (semi1 == std::string_view::npos) {
        return false;
    }
    const std::size_t semi2 = payload.find(';', semi1 + 1);
    if (semi2 == std::string_view::npos) {
        return false;
    }

    const std::string_view b_text = payload.substr(0, semi1);
    const std::string_view x_text = payload.substr(semi1 + 1, semi2 - semi1 - 1);
    const std::string_view y_text = payload.substr(semi2 + 1);

    auto parse_int = [](std::string_view text, int& out) -> bool {
        if (text.empty()) {
            return false;
        }
        const char* first = text.data();
        const char* last = text.data() + text.size();
        const auto result = std::from_chars(first, last, out);
        return result.ec == std::errc{} && result.ptr == last;
    };

    if (!parse_int(b_text, b) || !parse_int(x_text, x) || !parse_int(y_text, y)) {
        return false;
    }

    return true;
}

void Tui::cycle_step_granularity(bool increase) {
    static constexpr std::array<uint64_t, 9> kPresets = {1, 5, 10, 50, 100, 500, 1000, 5000, 10000};
    uint64_t cur = step_granularity_.load(std::memory_order_relaxed);
    std::size_t idx = 3;
    for (std::size_t i = 0; i < kPresets.size(); ++i) {
        if (kPresets[i] == cur) {
            idx = i;
            break;
        }
    }
    if (increase) {
        if (idx + 1 < kPresets.size()) ++idx;
    } else {
        if (idx > 0) --idx;
    }
    step_granularity_.store(kPresets[idx], std::memory_order_relaxed);
}

void Tui::on_cycle_completed() {
    uint64_t budget = step_budget_.load(std::memory_order_relaxed);
    while (budget > 0) {
        if (step_budget_.compare_exchange_weak(budget, budget - 1, std::memory_order_relaxed)) {
            if (budget == 1) {
                pause_loop();
            }
            break;
        }
    }
    uint64_t delay = step_delay_us_.load(std::memory_order_relaxed);
    if (delay > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(delay));
    }
}

void Tui::open_modal(ModalType type) {
    active_modal_ = type;
    modal_input_.clear();
    set_paused(true);
    render(true);
}

void Tui::close_modal() {
    active_modal_ = ModalType::None;
    modal_input_.clear();
    render(true);
}

void Tui::submit_modal() {
    if (active_modal_ == ModalType::None) return;
    std::string text = modal_input_;
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();

    if (!text.empty() && active_modal_ != ModalType::Help) {
        switch (active_modal_) {
            case ModalType::SetBreakpoint: {
                Address addr = 0;
                bool ok = false;
                if (text.starts_with("0x") || text.starts_with("0X")) {
                    auto result = std::from_chars(text.data() + 2, text.data() + text.size(), addr, 16);
                    ok = (result.ec == std::errc{});
                } else if (std::all_of(text.begin(), text.end(), ::isxdigit)) {
                    auto result = std::from_chars(text.data(), text.data() + text.size(), addr, 16);
                    ok = (result.ec == std::errc{});
                } else {
                    auto sym_opt = machine_.symbols.lookup_name(text);
                    if (sym_opt.has_value()) {
                        addr = *sym_opt;
                        ok = true;
                    }
                }
                if (ok) {
                    machine_.breakpoints.add_pc_breakpoint(addr);
                    set_status_override(std::format("Breakpoint set at 0x{:08x}", addr));
                } else {
                    set_status_override(std::format("Symbol/address not found: {}", text));
                }
                break;
            }
            case ModalType::SetStepSize: {
                uint64_t val = 0;
                auto result = std::from_chars(text.data(), text.data() + text.size(), val);
                if (result.ec == std::errc{} && val > 0) {
                    step_granularity_.store(val, std::memory_order_relaxed);
                    set_status_override(std::format("StepN set to {} instructions", val));
                } else {
                    set_status_override(std::format("Invalid step size: {}", text));
                }
                break;
            }
            case ModalType::SetSpeed: {
                uint64_t val = 0;
                auto result = std::from_chars(text.data(), text.data() + text.size(), val);
                if (result.ec == std::errc{}) {
                    step_delay_us_.store(val, std::memory_order_relaxed);
                    set_status_override(std::format("Step delay set to {}us", val));
                } else {
                    set_status_override(std::format("Invalid speed delay: {}", text));
                }
                break;
            }
            case ModalType::InspectAddress: {
                Address addr = 0;
                bool ok = false;
                if (text.starts_with("0x") || text.starts_with("0X")) {
                    auto result = std::from_chars(text.data() + 2, text.data() + text.size(), addr, 16);
                    ok = (result.ec == std::errc{});
                } else if (std::all_of(text.begin(), text.end(), ::isxdigit)) {
                    auto result = std::from_chars(text.data(), text.data() + text.size(), addr, 16);
                    ok = (result.ec == std::errc{});
                } else {
                    auto sym_opt = machine_.symbols.lookup_name(text);
                    if (sym_opt.has_value()) {
                        addr = *sym_opt;
                        ok = true;
                    }
                }
                if (ok) {
                    left_pane_->set_inspect_addr(addr);
                    set_reg_page(TuiRegPage::STACK);
                    set_status_override(std::format("Inspecting memory at 0x{:08x}", addr));
                } else {
                    set_status_override(std::format("Invalid address/symbol: {}", text));
                }
                break;
            }
            default:
                break;
        }
    }
    active_modal_ = ModalType::None;
    modal_input_.clear();
    render(true);
}

void Tui::render_modal_overlay(std::vector<std::string>& lines, int term_width, int term_height) const {
    (void)term_height;
    if (active_modal_ == ModalType::None || lines.empty()) return;

    bool is_help = (active_modal_ == ModalType::Help);
    int box_w = is_help ? std::min(68, term_width - 4) : std::min(58, term_width - 4);
    if (box_w < 35) return;

    std::string m_bg = kThemeModalBg;
    std::string m_rst = std::string("\033[0m") + m_bg;

    std::vector<std::string> content_rows;
    auto add_row = [&](const std::string& formatted_str) {
        std::string s = formatted_str;
        std::string target = "\033[0m";
        size_t pos = 0;
        while ((pos = s.find(target, pos)) != std::string::npos) {
            s.replace(pos, target.length(), m_rst);
            pos += m_rst.length();
        }
        content_rows.push_back(s);
    };

    std::string title;
    switch (active_modal_) {
        case ModalType::SetBreakpoint:
            title = " SET BREAKPOINT ";
            add_row(std::format("{}Enter PC Address (hex) or Symbol:\033[0m", kThemeText));
            add_row(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, modal_input_));
            break;
        case ModalType::SetStepSize:
            title = " SET STEP SIZE (N) ";
            add_row(std::format("{}Enter Step Count N (instructions):\033[0m", kThemeText));
            add_row(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, modal_input_));
            break;
        case ModalType::SetSpeed:
            title = " SET SIMULATION SPEED ";
            add_row(std::format("{}Enter Step Delay (microseconds, 0=Max):\033[0m", kThemeText));
            add_row(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, modal_input_));
            break;
        case ModalType::InspectAddress:
            title = " INSPECT MEMORY ADDRESS ";
            add_row(std::format("{}Enter Target Address (hex) or Symbol:\033[0m", kThemeText));
            add_row(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, modal_input_));
            break;
        case ModalType::Help:
            title = " SIMULATOR KEYBOARD SHORTCUTS ";
            add_row(std::format(" {}{:<18}\033[0m {}Single instruction step\033[0m", kThemeSky, "[s] / [Space]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Step N instructions\033[0m", kThemeSky, "[n]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Undo / Step back 1 instruction\033[0m", kThemeSky, "[b]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Set PC Breakpoint modal\033[0m", kThemeSky, "[:]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Toggle PC breakpoint at current PC\033[0m", kThemeSky, "[k]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Set Step Size (N) modal\033[0m", kThemeSky, "[g]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Set Speed Delay (us) modal\033[0m", kThemeSky, "[f]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Inspect Memory Address modal\033[0m", kThemeSky, "[m]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Run / Pause simulation loop\033[0m", kThemeSky, "[c] / [Ctrl-P]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Cycle TUI panel layout\033[0m", kThemeSky, "[Tab]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Cycle register sub-views (GPR/FPR/VEC)\033[0m", kThemeSky, "[r] / [Alt-r]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Cycle tool tabs (Pipe/Cache/Trace/Exp/Stack)\033[0m", kThemeSky, "[l] / [Alt-l]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Cycle Right Pane mode\033[0m", kThemeSky, "[p] / [Alt-p]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Jump to Explainer / Trap Details\033[0m", kThemeSky, "[e] / [Alt-e]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Toggle trace recording\033[0m", kThemeSky, "[v] / [Alt-v]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Show this help dialog\033[0m", kThemeSky, "[F1] / [h] / [?]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Toggle High Contrast theme\033[0m", kThemeSky, "[Alt-h]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Toggle Sakura Pastel theme\033[0m", kThemeSky, "[Alt-t]", kThemeText));
            add_row(std::format(" {}{:<18}\033[0m {}Close modal dialog\033[0m", kThemeSky, "[Esc]", kThemeText));
            break;
        default:
            break;
    }

    std::vector<std::string> box_lines;
    int inner_w = box_w - 2;

    auto pad_center = [&](const std::string& text, int width) -> std::string {
        int text_len = get_display_width(text);
        if (text_len >= width) return format_to_width(text, width);
        int pad_l = (width - text_len) / 2;
        int pad_r = width - text_len - pad_l;
        return std::string(pad_l, ' ') + text + std::string(pad_r, ' ');
    };

    auto pad_left = [&](const std::string& text, int width) -> std::string {
        int text_len = get_display_width(text);
        if (text_len >= width) return format_to_width(text, width);
        return text + std::string(width - text_len, ' ');
    };

    std::string top_border = std::format("{}{}╔{}{}{}╗\033[0m", m_bg, kThemeBorder, pad_center(std::format("═ \033[1m{}{}{}{} ═", kThemeSky, title, m_rst, kThemeBorder), inner_w), m_bg, kThemeBorder);
    box_lines.push_back(top_border);
    box_lines.push_back(std::format("{}{}║{}{}{}║\033[0m", m_bg, kThemeBorder, pad_center("", inner_w), m_bg, kThemeBorder));

    for (const auto& row : content_rows) {
        box_lines.push_back(std::format("{}{}║{}{}{}║\033[0m", m_bg, kThemeBorder, pad_left(" " + row, inner_w), m_bg, kThemeBorder));
    }

    box_lines.push_back(std::format("{}{}║{}{}{}║\033[0m", m_bg, kThemeBorder, pad_center("", inner_w), m_bg, kThemeBorder));
    if (!is_help) {
        box_lines.push_back(std::format("{}{}║{}{}{}║\033[0m", m_bg, kThemeBorder, pad_center(std::format("{}[Enter]\033[0m{} {}Submit  |  {}[Esc]\033[0m{} {}Cancel", kThemeVal, m_rst, kThemeMuted, kThemeVal, m_rst, kThemeMuted), inner_w), m_bg, kThemeBorder));
    } else {
        box_lines.push_back(std::format("{}{}║{}{}{}║\033[0m", m_bg, kThemeBorder, pad_center(std::format("{}Press {}[Esc]\033[0m{}, {}[Enter]\033[0m{}, {}[h]\033[0m{} or {}[F1]\033[0m{} to close", kThemeMuted, kThemeVal, m_rst, kThemeVal, m_rst, kThemeVal, m_rst, kThemeVal, m_rst), inner_w), m_bg, kThemeBorder));
    }
    box_lines.push_back(std::format("{}{}╚{}╝\033[0m", m_bg, kThemeBorder, make_repeated_string("═", inner_w)));

    int box_h = static_cast<int>(box_lines.size());
    int start_y = (static_cast<int>(lines.size()) - box_h) / 2;
    int start_x = (term_width - box_w) / 2;
    if (start_y < 0) start_y = 0;
    if (start_x < 0) start_x = 0;

    for (size_t i = 0; i < box_lines.size(); ++i) {
        int r = start_y + static_cast<int>(i);
        if (r >= 0 && static_cast<size_t>(r) < lines.size()) {
            // box_lines[i] already starts with m_bg (\033[48;5;236m) and resets with \033[0m
            lines[r] = overlay_string(lines[r], box_lines[i], start_x, box_w);
        }
    }
}

}  // namespace simrv::tui
