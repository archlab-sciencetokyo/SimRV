/**
 * @file Tui.cpp
 * @brief Terminal User Interface rendering, input management, and layout engine.
 */
#include "simrv/tui/Tui.hpp"

#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>
#include <thread>

#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/Framebuffer.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/tui/TuiKey.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/LeftPane.hpp"
#include "simrv/tui/panels/RightPane.hpp"
#include "simrv/tui/panels/StatusBar.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::tui {

static struct termios
    g_saved_termios;                  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_termios_saved = false;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_tui_active = false;     // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

extern "C" void emergency_terminal_restore() {
    std::fflush(stdout);
    if (g_tui_active) {
        const char* shutdown_seq =
            "\033[0m\033[?1006l\033[?1000l\033[?25h\033[2J\033[H\033[?1049l\n";
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
        auto constexpr shutdown_seq =
            "\033[0m\033[?1006l\033[?1000l\033[?25h\033[2J\033[H\033[?1049l\n"sv;
        (void)(::write(STDOUT_FILENO, shutdown_seq.data(), shutdown_seq.size()) == 0);
        g_tui_active = false;
    }
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
    }
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

volatile std::sig_atomic_t g_resized =
    0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

namespace {

void handle_sigwinch(int sig) {
    (void)sig;
    g_resized = 1;
}

}  // namespace

Tui::Tui(simrv::core::Machine& machine) : machine_(machine), modal_(machine) {
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
    if (machine_.s_fn_memimg.empty()) {
        open_modal(ModalType::LoadBinary);
    }
}

Tui::~Tui() { shutdown(); }

void Tui::set_paused(bool p) {
    if (!p && machine_.is_shutdown_) {
        modal_.open_notice("SYSTEM SHUTDOWN",
                           "Target system has shutdown.\n\nPlease reboot [Ctrl-R], load a binary "
                           "[o], or quit [q].",
                           false);
        return;
    }
    if (!p && machine_.cpu.state().pc == 0) {
        modal_.open_notice(
            "NO PROGRAM LOADED",
            "Cannot run simulation: PC is 0x0.\n\nPlease load a program binary image first [o].",
            false);
        return;
    }
    const bool cur_paused = paused_.load(std::memory_order_relaxed);
    if (cur_paused != p) {
        paused_.store(p, std::memory_order_release);
        if (!p) {
            status_override_.clear();
            last_runtime_tick_ = std::chrono::steady_clock::now();
            last_speed_update_ = std::chrono::steady_clock::now();
            last_icount_ = machine_.cpu.e_icount;
        } else {
            if (last_runtime_tick_ != std::chrono::steady_clock::time_point{}) {
                runtime_duration_ += std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - last_runtime_tick_);
                last_runtime_tick_ = {};
            }
        }
        update_trace_active_cache();
    }
}

void Tui::initialize() {
    left_pane_ = std::make_unique<LeftPane>(machine_);
    right_pane_ = std::make_unique<RightPane>();
    status_bar_ = std::make_unique<StatusBar>(machine_);

    set_tui_theme(get_tui_theme());

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

    // Query character cell size in pixels (\033[16t) and primary device attributes for Sixel
    // support (\033[c)
    (void)(::write(STDOUT_FILENO, "\033[16t\033[c", 9) == 0);

    // Read the response from stdin in raw mode with 100ms timeout
    std::string resp;
    char query_ch = 0;
    auto start_time = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 start_time)
               .count() < 100) {
        fd_set read_fds;
        struct timeval tv{.tv_sec = 0, .tv_usec = 10000};  // 10ms
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        if (select(STDIN_FILENO + 1, &read_fds, nullptr, nullptr, &tv) > 0) {
            if (::read(STDIN_FILENO, &query_ch, 1) == 1) {
                resp.push_back(query_ch);
                if (resp.contains('t') && resp.contains('c')) {
                    break;
                }
            }
        }
    }

    // 1. Cell size response \033[6;<H>;<W>t
    size_t t_pos = resp.find('t');
    if (t_pos != std::string::npos) {
        size_t seq_start = resp.rfind("\033[6;", t_pos);
        if (seq_start != std::string::npos) {
            std::string_view payload(resp.data() + seq_start + 4, t_pos - (seq_start + 4));
            size_t semi = payload.find(';');
            if (semi != std::string_view::npos) {
                int q_height = 0;
                int q_width = 0;
                std::string_view h_str = payload.substr(0, semi);
                std::string_view w_str = payload.substr(semi + 1);
                auto res_h = std::from_chars(h_str.data(), h_str.data() + h_str.size(), q_height);
                auto res_w = std::from_chars(w_str.data(), w_str.data() + w_str.size(), q_width);
                if (res_h.ec == std::errc{} && res_w.ec == std::errc{} && q_height > 0 &&
                    q_width > 0) {
                    cell_height_px_ = q_height;
                    cell_width_px_ = q_width;
                }
            }
        }
    }

    // 2. Sixel support query (DA1 \033[?...c) and environment checks
    bool env_sixel = false;
    const char* term_env = std::getenv("TERM");
    const char* term_prog = std::getenv("TERM_PROGRAM");
    if (term_env && std::string_view(term_env).contains("sixel")) env_sixel = true;
    if (term_prog &&
        (std::string_view(term_prog) == "wezterm" || std::string_view(term_prog) == "foot" ||
         std::string_view(term_prog) == "mlterm" || std::string_view(term_prog) == "yaft" ||
         std::string_view(term_prog) == "ghostty")) {
        env_sixel = true;
    }

    bool da1_sixel = false;
    size_t c_pos = resp.find('c');
    if (c_pos != std::string::npos) {
        size_t da_start = resp.rfind("\033[?", c_pos);
        if (da_start != std::string::npos) {
            std::string_view params(resp.data() + da_start + 3, c_pos - (da_start + 3));
            size_t pstart = 0;
            while (pstart < params.size()) {
                size_t pend = params.find(';', pstart);
                std::string_view p = (pend == std::string_view::npos)
                                         ? params.substr(pstart)
                                         : params.substr(pstart, pend - pstart);
                if (p == "4") {
                    da1_sixel = true;
                    break;
                }
                if (pend == std::string_view::npos) break;
                pstart = pend + 1;
            }
        }
    }

    sixel_supported_ = env_sixel || da1_sixel;

    g_tui_active = true;

    const char* init_seq = "\033[?1049h\033[2J\033[H\033[?1000h\033[?1006h\033[?25l";
    (void)(::write(STDOUT_FILENO, init_seq, strlen(init_seq)) == 0);

    struct sigaction sa{};
    sa.sa_handler = handle_sigwinch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);

    simrv::log::set_tui_callback([this](const std::string& msg) -> void { print_log(msg); });

    if (machine_.s_fn_memimg.empty()) {
        open_modal(ModalType::LoadBinary);
    }
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

void Tui::render_update_speed(std::chrono::steady_clock::time_point now) {
    if (!is_paused()) {
        if (last_runtime_tick_ != std::chrono::steady_clock::time_point{}) {
            runtime_duration_ +=
                std::chrono::duration_cast<std::chrono::microseconds>(now - last_runtime_tick_);
        }
        last_runtime_tick_ = now;
        auto diff =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_speed_update_).count();
        if (diff >= 50) {
            uint64_t current_icount = machine_.cpu.e_icount;
            uint64_t insns_since_last = current_icount - last_icount_;
            if (diff > 0) {
                speed_ips_ = (insns_since_last * 1000ULL) / static_cast<uint64_t>(diff);
                kips_ = speed_ips_ / 1000;
                if (kips_ > max_kips_) max_kips_ = kips_;
                kips_history_.push_back(kips_);
                if (kips_history_.size() > 60) kips_history_.erase(kips_history_.begin());
                last_icount_ = current_icount;
                last_speed_update_ = now;
            }
        }
    } else {
        if (last_runtime_tick_ != std::chrono::steady_clock::time_point{}) {
            runtime_duration_ +=
                std::chrono::duration_cast<std::chrono::microseconds>(now - last_runtime_tick_);
            last_runtime_tick_ = std::chrono::steady_clock::time_point{};
        }
        uint64_t current_icount = machine_.cpu.e_icount;
        if (current_icount > last_icount_) {
            auto diff =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - last_speed_update_)
                    .count();
            if (diff > 0) {
                uint64_t insns_since_last = current_icount - last_icount_;
                speed_ips_ = (insns_since_last * 1000ULL) / static_cast<uint64_t>(diff);
                kips_ = speed_ips_ / 1000;
                if (kips_ > max_kips_) max_kips_ = kips_;
                kips_history_.push_back(kips_);
                if (kips_history_.size() > 60) kips_history_.erase(kips_history_.begin());
                last_icount_ = current_icount;
                last_speed_update_ = now;
            } else if (runtime_duration_.count() > 0) {
                speed_ips_ = (current_icount * 1000000ULL) /
                             static_cast<uint64_t>(runtime_duration_.count());
                kips_ = speed_ips_ / 1000;
                if (kips_ > max_kips_) max_kips_ = kips_;
                kips_history_.push_back(kips_);
                if (kips_history_.size() > 60) kips_history_.erase(kips_history_.begin());
                last_icount_ = current_icount;
                last_speed_update_ = now;
            }
        }
    }
}

void Tui::render_build_lines(int left_pane_width, int right_pane_width, int num_rows,
                             TuiRightPanelMode panel_mode) {
    lines_to_draw_.clear();
    if (right_pane_width > 0 && num_rows > 0) {
        if (panel_mode == TuiRightPanelMode::Terminal) {
            vt_.resize(right_pane_width, num_rows);
            int total = vt_.get_lines_count();
            int end_exclusive = std::max(0, total - scroll_offset_);
            int start = std::max(0, end_exclusive - num_rows);
            int cursor_abs_line = vt_.get_scrollback_size() + vt_.get_cursor_y();
            bool is_live = (scroll_offset_ == 0);
            for (int i = start; i < end_exclusive; ++i) {
                bool draw_cursor = is_live && (i == cursor_abs_line) && vt_.is_cursor_visible();
                lines_to_draw_.push_back(vt_.get_line_as_string(i, right_pane_width, draw_cursor));
            }
        } else if (panel_mode == TuiRightPanelMode::Display) {
            for (int i = 0; i < num_rows; ++i)
                lines_to_draw_.emplace_back(static_cast<size_t>(right_pane_width), ' ');
        }
        while (lines_to_draw_.size() < static_cast<std::size_t>(num_rows)) {
            lines_to_draw_.emplace_back(static_cast<std::size_t>(right_pane_width), ' ');
        }
    }

    int log_width = std::max(10, left_pane_width - 2);
    vt_log_.resize(log_width, num_rows);
    std::vector<std::string> log_lines;
    int total_written = vt_log_.get_scrollback_size() + vt_log_.get_cursor_y() +
                        (vt_log_.get_cursor_x() > 0 ? 1 : 0);
    int start_log = std::max(0, total_written - 20);
    for (int i = start_log; i < total_written; ++i) {
        log_lines.push_back(vt_log_.get_line_as_string(i, log_width, false));
    }

    left_pane_->set_kips(kips_);
    left_pane_->set_max_kips(max_kips_);
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
}

void Tui::render_draw_sixel(int left_pane_width, int right_pane_width, int num_rows,
                            std::string& update_cmds) {
    if (machine_.framebuffer && (true || machine_.framebuffer->is_tui_dirty())) {
        int active_w = 0;
        int active_h = 0;
        if (machine_.framebuffer->get_active_bounds(active_w, active_h) && active_w > 1 &&
            active_h > 1) {
            int avail_cols = right_pane_width - 4;
            int avail_rows = num_rows - 2;
            if (avail_cols >= 4 && avail_rows >= 4) {
                int cell_w = cell_width_px_ > 0 ? cell_width_px_ : 8;
                int cell_h = cell_height_px_ > 0 ? cell_height_px_ : 16;
                int target_w = avail_cols * cell_w - 4;
                int target_h = avail_rows * cell_h - 4;
                double aspect = static_cast<double>(active_w) / active_h;
                if (aspect <= 0.1) aspect = 1.6;
                int fit_h = static_cast<int>(target_w / aspect);
                if (fit_h > target_h)
                    target_w = static_cast<int>(target_h * aspect);
                else
                    target_h = fit_h;
                target_h = std::max(6, (target_h / 6) * 6);
                target_w = std::max(6, target_w);
                int img_w_cells = (target_w + cell_w - 1) / cell_w;
                int img_h_cells = (target_h + cell_h - 1) / cell_h;
                int rem_cols = avail_cols - img_w_cells;
                int rem_rows = avail_rows - img_h_cells;
                int right_pane_active_col =
                    (layout_ == TuiLayout::Split) ? (left_pane_width + 5) : 4;
                int img_col = right_pane_active_col + std::max(0, rem_cols / 2);
                int img_row = 5 + std::max(0, rem_rows / 2);
                update_cmds +=
                    std::format("\0337\033[{};{}H{}\0338", img_row, img_col,
                                machine_.framebuffer->get_sixel_escape(target_w, target_h));
            }
        }
    }
    if (!modal_.is_active()) {
        for (int i = 0; i < num_rows; ++i) {
            std::string left = left_pane_->render_row(i, left_pane_width);
            update_cmds += std::format("\033[{};1H{}║\033[0m{}{}│\033[0m", i + 4, kThemeBorder,
                                       left, kThemeBorder);
        }
    }
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

    while (!local_tx.empty()) {
        vt_.write_char(local_tx.front());
        local_tx.pop();
    }
    while (!local_log.empty()) {
        vt_log_.write_string(local_log.front());
        local_log.pop();
    }

    if (!left_pane_ || !right_pane_ || !status_bar_) return;

    TuiRightPanelMode const panel_mode = right_panel_mode_.load(std::memory_order_relaxed);
    static auto last_draw_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - last_draw_time).count();

    if (!force && !g_resized) {
        bool const is_active = !paused_ || has_tx || has_log;
        if ((is_active && elapsed_ms < 16) || (!is_active && elapsed_ms < 200)) return;
    }
    last_draw_time = now;
    if (g_resized) g_resized = 0;

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
        for (const auto& rec : local_records) trace_buffer_.push_back(format_trace_record(rec));
    }

    struct winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int term_width = w.ws_col;
    int term_height = w.ws_row;
    if (term_width < 40 || term_height < 10) return;

    render_update_speed(now);

    int left_pane_width = user_left_pane_width_ > 0
                              ? user_left_pane_width_
                              : ((term_width > 120) ? 75 : std::max(40, (term_width * 55) / 100));
    left_pane_width = std::clamp(left_pane_width, 40, std::max(20, term_width - 10));
    pane_width_cached_ = left_pane_width;
    int right_pane_width = std::max(0, term_width - left_pane_width - 3);
    if (layout_ == TuiLayout::FullRight) {
        left_pane_width = 0;
        right_pane_width = term_width - 2;
    } else if (layout_ == TuiLayout::FullLeft) {
        left_pane_width = term_width - 2;
        right_pane_width = 0;
    }

    int num_rows = term_height - 7;
    int total = (panel_mode == TuiRightPanelMode::Terminal) ? vt_.get_lines_count() : 0;
    scroll_offset_ = std::min(scroll_offset_, std::max(0, total - num_rows));

    render_build_lines(left_pane_width, right_pane_width, num_rows, panel_mode);
    lock.unlock();

    std::vector<std::string> new_lines;
    new_lines.reserve(static_cast<size_t>(num_rows) + 6);
    auto append_split_lines = [&](const std::string& str) {
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
            new_lines.push_back(
                std::format("{}║\033[0m{}{}│\033[0m{}{}║\033[0m", kThemeBorder,
                            left_pane_->render_row(i, left_pane_width), kThemeBorder,
                            right_pane_->render_row(i, right_pane_width), kThemeBorder));
        } else if (layout_ == TuiLayout::FullRight) {
            new_lines.push_back(std::format("{}║\033[0m{}{}║\033[0m", kThemeBorder,
                                            right_pane_->render_row(i, right_pane_width),
                                            kThemeBorder));
        } else {
            new_lines.push_back(std::format("{}║\033[0m{}{}║\033[0m", kThemeBorder,
                                            left_pane_->render_row(i, left_pane_width),
                                            kThemeBorder));
        }
    }

    if (layout_ == TuiLayout::Split) {
        new_lines.push_back(std::format("{}╠{}╧{}╣\033[0m", kThemeBorder,
                                        make_repeated_string("═", left_pane_width),
                                        make_repeated_string("═", right_pane_width)));
    } else {
        new_lines.push_back(
            std::format("{}╠{}╣\033[0m", kThemeBorder, make_repeated_string("═", term_width - 2)));
    }

    append_split_lines(status_bar_->render_row(1, term_width));
    modal_.render_overlay(new_lines, term_width, term_height);

    std::string update_cmds = "\033[?25l";
    if (force || last_screen_lines_.size() != new_lines.size()) {
        update_cmds += "\033[H";
        for (size_t i = 0; i < new_lines.size(); ++i) {
            update_cmds += new_lines[i];
            if (i + 1 < new_lines.size()) update_cmds += "\n";
        }
        last_screen_lines_ = new_lines;
    } else {
        for (size_t i = 0; i < new_lines.size(); ++i) {
            if (sixel_supported_ && panel_mode == TuiRightPanelMode::Display &&
                layout_ == TuiLayout::Split && i >= 3 && i < 3 + static_cast<size_t>(num_rows))
                continue;
            if (new_lines[i] != last_screen_lines_[i]) {
                update_cmds += std::format("\033[{};1H{}", i + 1, new_lines[i]);
                last_screen_lines_[i] = new_lines[i];
            }
        }
    }

    if (sixel_supported_ && panel_mode == TuiRightPanelMode::Display &&
        layout_ == TuiLayout::Split) {
        render_draw_sixel(left_pane_width, right_pane_width, num_rows, update_cmds);
        update_cmds += std::format("\033[{};1H", term_height);
    }

    (void)(::write(STDOUT_FILENO, update_cmds.data(), update_cmds.size()) == 0);
    ::fflush(stdout);
}

void Tui::handle_mouse_left_pane(int x, int y, int b) {
    if (b == 0) {
        if (y == 4) {
            int const col = x - 2;
            if (col < 0) return;
            auto tab = left_pane_->get_tab_at_col(col);
            if (tab.has_value()) {
                if (*tab == TuiRegPage::CACHE && left_pane_->get_page() == TuiRegPage::CACHE) {
                    left_pane_->toggle_cache_inspect_type();
                    render(true);
                    return;
                }
                left_pane_->set_previous_page(left_pane_->get_page());
                set_reg_page(*tab);
            } else {
                bool const is_regs = (left_pane_->get_page() == TuiRegPage::GPR ||
                                      left_pane_->get_page() == TuiRegPage::FPR ||
                                      left_pane_->get_page() == TuiRegPage::VEC);
                if (col < (is_regs ? 10 : 4)) cycle_reg_page();
            }
        } else if (y >= 5) {
            int logical_row = (y - 5) + left_pane_->get_scroll_offset();
            auto page = left_pane_->get_page();
            if (page == TuiRegPage::CACHE) {
                if (logical_row == 0 || logical_row == 4) {
                    left_pane_->toggle_cache_inspect_type();
                    render(true);
                }
            } else if (page == TuiRegPage::PIPELINE) {
                Register clicked_pc = left_pane_->get_pipeline_pc_at_row(logical_row);
                if (clicked_pc != 0) {
                    left_pane_->set_previous_page(TuiRegPage::PIPELINE);
                    left_pane_->set_explain_pc(clicked_pc);
                    set_reg_page(TuiRegPage::EXPLAIN);
                }
            } else if (page == TuiRegPage::EXPLAIN) {
                if (logical_row <= 1) {
                    auto prev = left_pane_->get_previous_page();
                    if (prev.has_value()) set_reg_page(*prev);
                }
            } else if (page == TuiRegPage::GPR || page == TuiRegPage::FPR) {
                auto reg_val =
                    left_pane_->get_register_value_at_row(logical_row, x, pane_width_cached_);
                if (reg_val.has_value()) {
                    left_pane_->set_inspect_addr(*reg_val);
                    open_modal(ModalType::InspectAddress);
                }
            } else if (page == TuiRegPage::STACK) {
                auto stack_addr = left_pane_->get_stack_addr_at_row(logical_row);
                if (stack_addr.has_value()) {
                    left_pane_->set_inspect_addr(*stack_addr);
                    open_modal(ModalType::InspectAddress);
                }
            }
        }
    } else if (b == 64)
        scroll_regs(-2);
    else if (b == 65)
        scroll_regs(2);
}

void Tui::handle_mouse(int x, int y, int b) {
    if (!right_pane_ || !left_pane_) return;

    if (x < pane_width_cached_) {
        if (!paused_) {
            if (y >= 5) {
                int logical_row = (y - 5) + left_pane_->get_scroll_offset();
                if (left_pane_->is_running_label_click(logical_row, x - 2, pane_width_cached_)) {
                    pause_loop();
                }
            }
            return;
        }
        handle_mouse_left_pane(x, y, b);
    } else {
        if (b == 64)
            scroll(5);
        else if (b == 65)
            scroll(-5);
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
                if (has_f || has_d)
                    rp = TuiRegPage::FPR;
                else if (has_v)
                    rp = TuiRegPage::VEC;
                // else stay on GPR (no fp/vec extensions)
                break;
            case TuiRegPage::FPR:
                if (has_v)
                    rp = TuiRegPage::VEC;
                else
                    rp = TuiRegPage::GPR;
                break;
            case TuiRegPage::VEC:
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
            if (machine_.s_cycle_accurate) {
                rp = TuiRegPage::CACHE;
            } else {
                rp = TuiRegPage::TLB;
            }
            break;
        case TuiRegPage::CACHE:
            rp = machine_.s_cycle_accurate ? TuiRegPage::BPRED : TuiRegPage::TLB;
            break;
        case TuiRegPage::BPRED:
            rp = machine_.s_cycle_accurate ? TuiRegPage::HAZARD : TuiRegPage::TLB;
            break;
        case TuiRegPage::HAZARD:
            rp = TuiRegPage::TLB;
            break;
        case TuiRegPage::TLB:
            rp = TuiRegPage::BUS;
            break;
        case TuiRegPage::BUS:
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
    if (!machine_.s_cycle_accurate &&
        (page == TuiRegPage::CACHE || page == TuiRegPage::BPRED || page == TuiRegPage::HAZARD)) {
        set_status_override(
            "CA Inspector Page disabled in Functional Mode (Enable Cycle-Accurate mode "
            "\033[1m[,]\033[22m or -ca)");
        page = TuiRegPage::TLB;
    }
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
    TuiRightPanelMode next = (current == TuiRightPanelMode::Terminal) ? TuiRightPanelMode::Display
                                                                      : TuiRightPanelMode::Terminal;
    right_panel_mode_.store(next, std::memory_order_relaxed);
    scroll_offset_ = 0;
    render(true);
}

void Tui::toggle_trace_enabled() {
    trace_enabled_.store(!trace_enabled_.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
    update_trace_active_cache();
    render(true);
}

void Tui::update_trace_active_cache() {
    bool trace_page_active = left_pane_ && (left_pane_->get_page() == TuiRegPage::TRACE);
    trace_or_livetrace_active_.store(
        trace_enabled_.load(std::memory_order_relaxed) || trace_page_active,
        std::memory_order_relaxed);
}

void Tui::record_instruction(Register pc, simrv::isa::Opcode opcode, simrv::isa::OperationId op_id,
                             uint8_t rd, Register rd_val, uint8_t rs1, Register rs1_val,
                             uint8_t rs2, Register rs2_val, int64_t imm) {
    std::scoped_lock lock(trace_mutex_);
    if (trace_record_buffer_.empty()) {
        trace_record_buffer_.resize(kTraceBufferSize);
    }
    trace_record_buffer_[trace_buffer_head_] = TraceRecord{.pc = pc,
                                                           .opcode = opcode,
                                                           .op_id = op_id,
                                                           .rd = rd,
                                                           .rd_val = rd_val,
                                                           .rs1 = rs1,
                                                           .rs1_val = rs1_val,
                                                           .rs2 = rs2,
                                                           .rs2_val = rs2_val,
                                                           .imm = imm};
    trace_buffer_head_ = (trace_buffer_head_ + 1) % kTraceBufferSize;
    if (trace_buffer_size_ < kTraceBufferSize) {
        trace_buffer_size_++;
    } else {
        trace_buffer_tail_ = (trace_buffer_tail_ + 1) % kTraceBufferSize;
    }
}

void Tui::format_trace_inst(const TraceRecord& rec, const std::string& op_name, bool rd_fp,
                            bool rs1_fp, bool rs2_fp, std::string& inst_str,
                            std::string& side_effect) {
    auto get_reg_name = [](uint8_t reg, bool is_reg_fp) -> std::string {
        static constexpr std::array<const char*, 32> abi_names = {
            "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
            "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
            "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
        static constexpr std::array<const char*, 32> fp_names = {
            "ft0", "ft1", "ft2", "ft3", "ft4",  "ft5",  "ft6", "ft7", "fs0",  "fs1", "fa0",
            "fa1", "fa2", "fa3", "fa4", "fa5",  "fa6",  "fa7", "fs2", "fs3",  "fs4", "fs5",
            "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};
        if (reg >= 32) return "??";
        return is_reg_fp ? fp_names[reg] : abi_names[reg];
    };

    bool is_load = op_name.starts_with("l") && !op_name.starts_with("lui");
    bool is_store = op_name.starts_with("s") && !op_name.starts_with("slt") &&
                    !op_name.starts_with("sll") && !op_name.starts_with("sra") &&
                    !op_name.starts_with("srl") && !op_name.starts_with("sub") &&
                    !op_name.starts_with("sret") && !op_name.starts_with("sfence") &&
                    !op_name.starts_with("sc");
    bool is_branch = op_name.starts_with("b") && op_name != "break";
    bool is_jal = (op_name == "jal");
    bool is_jalr = (op_name == "jalr");
    bool is_lui = (op_name == "lui");
    bool is_auipc = (op_name == "auipc");
    bool is_csr = op_name.starts_with("csr");
    bool is_system = (op_name == "ecall" || op_name == "ebreak" || op_name == "uret" ||
                      op_name == "sret" || op_name == "mret" || op_name == "wfi");

    if (is_lui || is_auipc) {
        inst_str = std::format("{} {}, {:#x}", op_name, get_reg_name(rec.rd, rd_fp),
                               static_cast<uint32_t>(rec.imm) >> 12);
        side_effect = std::format("{} = {:#x}", get_reg_name(rec.rd, rd_fp), rec.rd_val);
    } else if (is_jal) {
        if (rec.rd == 0)
            inst_str = std::format("j {:#x}", rec.pc + rec.imm);
        else {
            inst_str =
                std::format("{} {}, {:#x}", op_name, get_reg_name(rec.rd, rd_fp), rec.pc + rec.imm);
            side_effect = std::format("{} = {:#x}", get_reg_name(rec.rd, rd_fp), rec.rd_val);
        }
    } else if (is_jalr || is_load) {
        inst_str = std::format("{} {}, {}({})", op_name, get_reg_name(rec.rd, rd_fp), rec.imm,
                               get_reg_name(rec.rs1, rs1_fp));
        side_effect = std::format("{} = {:#x}", get_reg_name(rec.rd, rd_fp), rec.rd_val);
    } else if (is_branch) {
        inst_str = std::format("{} {}, {}, {:#x}", op_name, get_reg_name(rec.rs1, rs1_fp),
                               get_reg_name(rec.rs2, rs2_fp), rec.pc + rec.imm);
    } else if (is_store) {
        inst_str = std::format("{} {}, {}({})", op_name, get_reg_name(rec.rs2, rs2_fp), rec.imm,
                               get_reg_name(rec.rs1, rs1_fp));
        side_effect = std::format("mem[{:#x}] = {:#x}", rec.rs1_val + rec.imm, rec.rs2_val);
    } else if (is_csr) {
        if (op_name.ends_with("i"))
            inst_str = std::format("{} {}, {:#x}, {}", op_name, get_reg_name(rec.rd, rd_fp),
                                   rec.imm & 0xFFF, rec.rs1);
        else
            inst_str = std::format("{} {}, {:#x}, {}", op_name, get_reg_name(rec.rd, rd_fp),
                                   rec.imm & 0xFFF, get_reg_name(rec.rs1, rs1_fp));
        side_effect = std::format("{} = {:#x}", get_reg_name(rec.rd, rd_fp), rec.rd_val);
    } else if (is_system) {
        inst_str = op_name;
    } else if (op_name.starts_with("amo")) {
        inst_str = std::format("{} {}, {}, ({})", op_name, get_reg_name(rec.rd, rd_fp),
                               get_reg_name(rec.rs2, rec.rs2_val), get_reg_name(rec.rs1, rs1_fp));
        side_effect = std::format("{} = {:#x}", get_reg_name(rec.rd, rd_fp), rec.rd_val);
    } else if (op_name.ends_with("i") || op_name.ends_with("iw")) {
        inst_str = std::format("{} {}, {}, {}", op_name, get_reg_name(rec.rd, rd_fp),
                               get_reg_name(rec.rs1, rs1_fp), rec.imm);
        side_effect = std::format("{} = {:#x}", get_reg_name(rec.rd, rd_fp), rec.rd_val);
    } else {
        inst_str = std::format("{} {}, {}, {}", op_name, get_reg_name(rec.rd, rd_fp),
                               get_reg_name(rec.rs1, rs1_fp), get_reg_name(rec.rs2, rs2_fp));
        if (rec.rd != 0)
            side_effect = std::format("{} = {:#x}", get_reg_name(rec.rd, rd_fp), rec.rd_val);
    }
}

auto Tui::format_trace_record(const TraceRecord& rec) -> std::string {
    std::string op_name;
    if (static_cast<std::size_t>(rec.op_id) < simrv::pipeline::OPERATION_NAME.size()) {
        std::string_view name_sv =
            simrv::pipeline::OPERATION_NAME.at(static_cast<std::size_t>(rec.op_id));
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

    std::string inst_str;
    std::string side_effect;
    format_trace_inst(rec, op_name, rd_fp, rs1_fp, rs2_fp, inst_str, side_effect);

    std::string sym = machine_.symbols.lookup(rec.pc);
    if (sym.empty()) {
        return side_effect.empty() ? std::format("{:#x}: {}", rec.pc, inst_str)
                                   : std::format("{:#x}: {} [{}]", rec.pc, inst_str, side_effect);
    }
    return side_effect.empty()
               ? std::format("{:#x} <{}>: {}", rec.pc, sym, inst_str)
               : std::format("{:#x} <{}>: {} [{}]", rec.pc, sym, inst_str, side_effect);
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

void Tui::reset_speed_history() {
    last_speed_update_ = std::chrono::steady_clock::now();
    last_icount_ = machine_.cpu.e_icount;
    speed_ips_ = 0;
    kips_ = 0;
    max_kips_ = 0;
    kips_history_.clear();
}

void Tui::adjust_left_pane_width(int delta) {
    struct winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);  // NOLINT(cppcoreguidelines-pro-type-vararg)
    int term_width = w.ws_col;

    int current = user_left_pane_width_ > 0
                      ? user_left_pane_width_
                      : ((term_width > 120) ? 75 : std::max(40, (term_width * 55) / 100));
    current += delta;
    if (current < 40) current = 40;
    if (current > term_width - 10) current = std::max(20, term_width - 10);
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
    uint8_t byte = 0;
    while (poll_keyboard(byte)) {
        if (consume_control_sequence(byte)) {
            continue;
        }

        const auto key = static_cast<simrv::tui::TuiKey>(byte);
        if (handle_modal_keyboard_input(byte, key)) {
            continue;
        }

        if (paused_.load(std::memory_order_relaxed)) {
            handle_normal_keyboard_input(byte, key);
        } else {
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
}

auto Tui::handle_modal_settings_misa(ModalType mtype, uint8_t byte, TuiKey key) -> bool {
    if (mtype == ModalType::Settings) {
        if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q') {
            close_modal();
        } else if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) {
            submit_modal();
        } else if (byte == ' ') {
            modal_.toggle_setting_at_cursor();
            render(true);
        } else if (byte == 'm' || byte == 'M') {
            open_modal(ModalType::ConfigureMisa);
            render(true);
        } else if (byte == 'y' || byte == 'Y') {
            if (!machine_.s_cycle_accurate) {
                set_status_override(
                    "System Config is available in CA Mode only. Enable CA Mode in Settings [,]");
                render(true);
            } else
                open_modal(ModalType::ConfigureSystem);
        }
        return true;
    }
    if (mtype == ModalType::ConfigureMisa) {
        if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q')
            close_modal();
        else if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline)
            submit_modal();
        else if (byte == ' ') {
            modal_.toggle_misa_at_cursor();
            render(true);
        } else if (byte == 'p' || byte == 'P') {
            modal_.apply_misa_profile(0);
            render(true);
        } else if (byte == 'i' || byte == 'I') {
            modal_.apply_misa_profile(1);
            render(true);
        } else if (byte == 'g' || byte == 'G') {
            modal_.apply_misa_profile(2);
            render(true);
        }
        return true;
    }
    return false;
}

auto Tui::handle_modal_sysconfig_bp(ModalType mtype, uint8_t byte, TuiKey key) -> bool {
    if (mtype == ModalType::ConfigureSystem) {
        if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q')
            close_modal();
        else if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline)
            submit_modal();
        else if (byte == ' ') {
            modal_.toggle_sysconfig_at_cursor();
            render(true);
        } else if (byte >= '0' && byte <= '9') {
            modal_.push_sysconfig_digit(static_cast<char>(byte));
            render(true);
        } else if (byte == 8 || byte == 127 || key == simrv::tui::TuiKey::Backspace) {
            modal_.pop_sysconfig_digit();
            render(true);
        }
        return true;
    }
    if (mtype == ModalType::ManageBreakpoints) {
        if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q')
            close_modal();
        else if (byte == 8 || byte == 127 || key == simrv::tui::TuiKey::Backspace || byte == 'd' ||
                 byte == 'D' || key == simrv::tui::TuiKey::Enter ||
                 key == simrv::tui::TuiKey::Newline) {
            if (modal_.remove_bp_at_cursor(
                    [this](const std::string& msg) { set_status_override(msg); }))
                render(true);
        } else if (byte == 'c' || byte == 'C') {
            machine_.breakpoints.clear_pc_breakpoints();
            machine_.breakpoints.clear_watchpoints();
            modal_.open_notice("BREAKPOINTS CLEARED", "Cleared all breakpoints and watchpoints.",
                               false);
            render(true);
        } else if (byte == ':' || byte == 'a' || byte == 'A')
            open_modal(ModalType::SetBreakpoint);
        else if (byte == 'w' || byte == 'W')
            open_modal(ModalType::SetWatchpoint);
        return true;
    }
    return false;
}

auto Tui::handle_modal_keyboard_input(uint8_t byte, TuiKey key) -> bool {
    if (!is_modal_active()) return false;

    auto mtype = get_active_modal();
    if (handle_modal_settings_misa(mtype, byte, key) ||
        handle_modal_sysconfig_bp(mtype, byte, key)) {
        return true;
    }
    if (mtype == ModalType::Notice) {
        if (byte == 27 || key == simrv::tui::TuiKey::Esc || key == simrv::tui::TuiKey::Enter ||
            key == simrv::tui::TuiKey::Newline || byte == ' ' || byte == 'q' || byte == 'Q')
            close_modal();
        return true;
    }

    if (byte == 27 || key == simrv::tui::TuiKey::Esc) {
        if (get_active_modal() != ModalType::LoadBinary || !machine_.s_fn_memimg.empty())
            close_modal();
    } else if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline)
        submit_modal();
    else if (get_active_modal() == ModalType::Help &&
             (key == simrv::tui::TuiKey::h || key == simrv::tui::TuiKey::H ||
              key == simrv::tui::TuiKey::QuestionMark))
        close_modal();
    else if (get_active_modal() == ModalType::LoadBinary && byte == 9) {
        modal_.toggle_load_mode();
        render(true);
    } else if (byte == 8 || byte == 127 || key == simrv::tui::TuiKey::Backspace) {
        modal_.pop_char();
        render(true);
    } else if (byte >= 32 && byte <= 126 && get_active_modal() != ModalType::Help) {
        modal_.push_char(static_cast<char>(byte));
        render(true);
    }
    return true;
}

auto Tui::handle_debug_keyboard_input(TuiKey key) -> bool {
    if (!machine_.s_debug_mode) {
        modal_.open_notice("DEBUG MODE REQUIRED",
                           "Debug features are disabled in Normal Mode.\n\nPlease enable TUI Debug "
                           "Mode in Simulator Settings [,] first.",
                           false);
        render(true);
        return true;
    }
    switch (key) {
        case simrv::tui::TuiKey::Colon:
            open_modal(ModalType::SetBreakpoint);
            break;
        case simrv::tui::TuiKey::w:
        case simrv::tui::TuiKey::W:
            open_modal(ModalType::SetWatchpoint);
            break;
        case simrv::tui::TuiKey::i:
        case simrv::tui::TuiKey::I:
            open_modal(ModalType::InspectAddress);
            break;
        case simrv::tui::TuiKey::m:
        case simrv::tui::TuiKey::M:
            open_modal(ModalType::ManageBreakpoints);
            break;
        case simrv::tui::TuiKey::b:
        case simrv::tui::TuiKey::B:
            if (machine_.cpu.perform_backstep()) {
                update_cache();
                render(true);
            }
            break;
        case simrv::tui::TuiKey::k:
        case simrv::tui::TuiKey::K: {
            Address pc = machine_.cpu.state().pc;
            if (machine_.breakpoints.has_pc_breakpoint(pc)) {
                machine_.breakpoints.remove_pc_breakpoint(pc);
                modal_.open_notice("BREAKPOINT REMOVED",
                                   std::format("Removed PC breakpoint at 0x{:08x}", pc), false);
            } else {
                machine_.breakpoints.add_pc_breakpoint(pc);
                modal_.open_notice("BREAKPOINT CREATED",
                                   std::format("PC breakpoint set at 0x{:08x}", pc), false);
            }
            render(true);
            break;
        }
        default:
            return false;
    }
    return true;
}

auto Tui::handle_speed_keyboard_input(TuiKey key) -> bool {
    if (key == simrv::tui::TuiKey::Plus || key == simrv::tui::TuiKey::Equal ||
        key == simrv::tui::TuiKey::Dot || key == simrv::tui::TuiKey::Minus) {
        static constexpr std::array<uint64_t, 10> kSpeedLevels = {
            1000000, 500000, 100000, 50000, 10000, 5000, 1000, 100, 10, 0};
        uint64_t cur_delay = step_delay_us_.load(std::memory_order_relaxed);
        uint64_t next_delay = (key == simrv::tui::TuiKey::Minus) ? 1000000 : 0;
        if (key == simrv::tui::TuiKey::Minus) {
            for (uint64_t lvl : std::array<uint64_t, 10>{0, 10, 100, 1000, 5000, 10000, 50000,
                                                         100000, 500000, 1000000}) {
                if (lvl > cur_delay) {
                    next_delay = lvl;
                    break;
                }
            }
        } else {
            for (uint64_t lvl : kSpeedLevels) {
                if (lvl < cur_delay) {
                    next_delay = lvl;
                    break;
                }
            }
        }
        step_delay_us_.store(next_delay, std::memory_order_relaxed);
        if (next_delay != cur_delay) reset_speed_history();
        render(true);
        return true;
    }
    return false;
}

auto Tui::handle_navigation_keyboard_input(uint8_t byte, TuiKey key) -> bool {
    switch (key) {
        case simrv::tui::TuiKey::Tab:
            cycle_layout();
            return true;
        case simrv::tui::TuiKey::r:
        case simrv::tui::TuiKey::R:
            cycle_reg_page();
            return true;
        case simrv::tui::TuiKey::l:
        case simrv::tui::TuiKey::L:
            cycle_tool_page();
            return true;
        case simrv::tui::TuiKey::e:
        case simrv::tui::TuiKey::E:
            toggle_explain();
            return true;
        case simrv::tui::TuiKey::f:
        case simrv::tui::TuiKey::F:
            open_modal(ModalType::SetSpeed);
            return true;
        case simrv::tui::TuiKey::QuestionMark:
        case simrv::tui::TuiKey::h:
        case simrv::tui::TuiKey::H:
            open_modal(ModalType::Help);
            return true;
        case simrv::tui::TuiKey::LeftBracket:
            adjust_left_pane_width(-2);
            return true;
        case simrv::tui::TuiKey::RightBracket:
            adjust_left_pane_width(2);
            return true;
        case simrv::tui::TuiKey::t:
        case simrv::tui::TuiKey::T:
            toggle_sakura_theme();
            return true;
        case simrv::tui::TuiKey::p:
        case simrv::tui::TuiKey::P:
            cycle_right_panel_mode();
            return true;
        case simrv::tui::TuiKey::v:
        case simrv::tui::TuiKey::V:
            toggle_trace_enabled();
            return true;
        case simrv::tui::TuiKey::u:
        case simrv::tui::TuiKey::U:
            scroll(5);
            return true;
        case simrv::tui::TuiKey::d:
        case simrv::tui::TuiKey::D:
            scroll(-5);
            return true;
        case simrv::tui::TuiKey::o:
        case simrv::tui::TuiKey::O:
            open_modal(ModalType::LoadBinary);
            return true;
        default:
            if (byte == ',' || key == simrv::tui::TuiKey::Comma) {
                open_modal(ModalType::Settings);
                return true;
            }
            if (byte == 'y' || byte == 'Y') {
                if (!machine_.s_cycle_accurate) {
                    modal_.open_notice(
                        "CA MODE REQUIRED",
                        "System Config is available in Cycle-Accurate (CA) Mode only.\n\nPlease "
                        "enable CA Mode in Simulator Settings [,] first.",
                        false);
                    render(true);
                } else
                    open_modal(ModalType::ConfigureSystem);
                return true;
            }
            return false;
    }
}

auto Tui::handle_normal_keyboard_input(uint8_t byte, TuiKey key) -> void {
    if (key == simrv::tui::TuiKey::CtrlP || key == simrv::tui::TuiKey::c ||
        key == simrv::tui::TuiKey::C) {
        if (machine_.is_shutdown_) {
            modal_.open_notice("SYSTEM SHUTDOWN",
                               "Target system has shutdown.\n\nPlease reboot [Ctrl-R], load a "
                               "binary [o], or quit [q].",
                               false);
            render(true);
            return;
        }
        if (machine_.s_fn_memimg.empty() && machine_.cpu.state().pc == 0) {
            modal_.open_notice("NO PROGRAM LOADED",
                               "Cannot run simulation: PC is 0x0.\n\nPlease load a program binary "
                               "image first [o].",
                               false);
            render(true);
            return;
        }
        unpause_loop();
        return;
    }
    if (key == simrv::tui::TuiKey::CtrlR || key == simrv::tui::TuiKey::r ||
        key == simrv::tui::TuiKey::R) {
        machine_.request_reboot();
        return;
    }
    if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) {
        reset_scroll();
        return;
    }
    if (key == simrv::tui::TuiKey::CtrlQ || key == simrv::tui::TuiKey::CtrlC ||
        key == simrv::tui::TuiKey::q || key == simrv::tui::TuiKey::Q) {
        machine_.is_running_ = false;
        machine_.is_shutdown_ = false;
        unpause_loop();
        return;
    }

    if (handle_navigation_keyboard_input(byte, key) || handle_speed_keyboard_input(key)) return;

    if (key == simrv::tui::TuiKey::Colon || key == simrv::tui::TuiKey::w ||
        key == simrv::tui::TuiKey::W || key == simrv::tui::TuiKey::i ||
        key == simrv::tui::TuiKey::I || key == simrv::tui::TuiKey::m ||
        key == simrv::tui::TuiKey::M || key == simrv::tui::TuiKey::b ||
        key == simrv::tui::TuiKey::B || key == simrv::tui::TuiKey::k ||
        key == simrv::tui::TuiKey::K) {
        if (handle_debug_keyboard_input(key)) return;
    }

    if (key == simrv::tui::TuiKey::s || key == simrv::tui::TuiKey::S ||
        key == simrv::tui::TuiKey::Space) {
        if (machine_.s_fn_memimg.empty() && machine_.cpu.state().pc == 0) {
            modal_.open_notice(
                "NO PROGRAM LOADED",
                "Cannot step: PC is 0x0.\n\nPlease load a program binary image first [o].", false);
            render(true);
            return;
        }
        if (machine_.is_shutdown_) {
            machine_.request_reboot();
            unpause_loop();
        } else {
            update_cache();
            machine_.prepare_cycle();
            machine_.cpu.run_cycle(machine_);
            machine_.finalize_cycle();
            render(true);
        }
    }
}

void Tui::pause_loop() {
    set_paused(true);
    update_cache();
    render(true);
}

void Tui::unpause_loop() { set_paused(false); }

void Tui::execute_footer_action(TuiFooterAction action) {
    switch (action) {
        case TuiFooterAction::Reboot:
            machine_.request_reboot();
            break;
        case TuiFooterAction::Step:
            if (machine_.is_shutdown_) {
                modal_.open_notice("SYSTEM SHUTDOWN",
                                   "Target system has shutdown.\n\nPlease reboot [Ctrl-R], load a "
                                   "binary [o], or quit [q].",
                                   false);
                render(true);
            } else if (machine_.cpu.state().pc == 0) {
                modal_.open_notice(
                    "NO PROGRAM LOADED",
                    "Cannot step: PC is 0x0.\n\nPlease load a program binary image first [o].",
                    false);
                render(true);
            } else {
                update_cache();
                machine_.prepare_cycle();
                machine_.cpu.run_cycle(machine_);
                machine_.finalize_cycle();
                render(true);
            }
            break;
        case TuiFooterAction::StepBack:
            if (machine_.cpu.perform_backstep()) {
                update_cache();
                render(true);
            }
            break;
        case TuiFooterAction::RunPause:
            if (machine_.is_shutdown_) {
                modal_.open_notice("SYSTEM SHUTDOWN",
                                   "Target system has shutdown.\n\nPlease reboot [Ctrl-R], load a "
                                   "binary [o], or quit [q].",
                                   false);
                render(true);
            } else if (paused_ && machine_.cpu.state().pc == 0) {
                modal_.open_notice("NO PROGRAM LOADED",
                                   "Cannot run simulation: PC is 0x0.\n\nPlease load a program "
                                   "binary image first [o].",
                                   false);
                render(true);
            } else if (paused_) {
                unpause_loop();
            } else {
                pause_loop();
            }
            break;
        case TuiFooterAction::CycleRegs:
            cycle_reg_page();
            break;
        case TuiFooterAction::CycleTools:
            cycle_tool_page();
            break;
        case TuiFooterAction::SetBreakpoint:
            open_modal(ModalType::SetBreakpoint);
            break;
        case TuiFooterAction::SetWatchpoint:
            open_modal(ModalType::SetWatchpoint);
            break;
        case TuiFooterAction::TogglePcBreakpoint: {
            Address pc = machine_.cpu.state().pc;
            if (machine_.breakpoints.has_pc_breakpoint(pc)) {
                machine_.breakpoints.remove_pc_breakpoint(pc);
                modal_.open_notice("BREAKPOINT REMOVED",
                                   std::format("Removed PC breakpoint at 0x{:08x}", pc), false);
            } else {
                machine_.breakpoints.add_pc_breakpoint(pc);
                modal_.open_notice("BREAKPOINT CREATED",
                                   std::format("PC breakpoint set at 0x{:08x}", pc), false);
            }
            render(true);
            break;
        }
        case TuiFooterAction::SetSpeed:
            open_modal(ModalType::SetSpeed);
            break;
        case TuiFooterAction::InspectMem:
            open_modal(ModalType::InspectAddress);
            break;
        case TuiFooterAction::LoadBinary:
            open_modal(ModalType::LoadBinary);
            break;
        case TuiFooterAction::ToggleHelp:
            open_modal(ModalType::Help);
            break;
        case TuiFooterAction::Quit:
            machine_.is_running_ = false;
            machine_.is_shutdown_ = false;
            unpause_loop();
            break;
        case TuiFooterAction::CycleLayout:
            cycle_layout();
            break;
        case TuiFooterAction::TogglePanel:
            cycle_right_panel_mode();
            break;
        case TuiFooterAction::ToggleTrace:
            toggle_trace_enabled();
            break;
        case TuiFooterAction::OpenSettings:
            open_modal(ModalType::Settings);
            break;
        case TuiFooterAction::ConfigureMisa:
            open_modal(ModalType::ConfigureMisa);
            break;
        case TuiFooterAction::ConfigureSystem:
            if (!machine_.s_cycle_accurate) {
                set_status_override(
                    "System Config is available in CA Mode only. Enable CA Mode in Settings [,]");
                render(true);
            } else {
                open_modal(ModalType::ConfigureSystem);
            }
            break;
        case TuiFooterAction::ManageBreakpoints:
            open_modal(ModalType::ManageBreakpoints);
            break;
    }
}

auto Tui::handle_alt_key(char key, uint8_t byte) -> bool {
    switch (key) {
        case 'b':
        case 'B':
            machine_.s_rollback_enabled = !machine_.s_rollback_enabled;
            if (!machine_.s_rollback_enabled) machine_.cpu.undo_stack.clear();
            set_status_override(std::format("Rollback tracking {}",
                                            machine_.s_rollback_enabled ? "enabled" : "disabled"));
            return true;
        case 'p':
        case 'P':
            cycle_right_panel_mode();
            return true;
        case 'r':
        case 'R':
            cycle_reg_page();
            return true;
        case 'l':
        case 'L':
            cycle_tool_page();
            return true;
        case 'e':
        case 'E':
            toggle_explain();
            return true;
        case 'o':
        case 'O':
            open_modal(ModalType::LoadBinary);
            return true;
        case 'v':
        case 'V':
            toggle_trace_enabled();
            return true;
        case 'h':
        case 'H':
            toggle_high_contrast();
            return true;
        case 't':
        case 'T':
            toggle_sakura_theme();
            return true;
        case 'u':
        case 'U':
            scroll(5);
            return true;
        case 'w':
        case 'W':
            scroll_regs(-2);
            return true;
        case 's':
        case 'S':
            open_modal(ModalType::Settings);
            return true;
        case 'm':
        case 'M':
            open_modal(ModalType::ConfigureMisa);
            return true;
        case 'z':
        case 'Z':
            reset_scroll_regs();
            return true;
        case 'c':
        case 'C':
            reset_scroll();
            return true;
        default:
            if (byte == 15) {
                open_modal(ModalType::LoadBinary);
                return true;
            }
            return false;
    }
}

auto Tui::handle_arrow_key_sequence() -> bool {
    if (esc_buf_ == "\033[A" || esc_buf_ == "\033OA") {
        if (get_active_modal() == ModalType::Settings) {
            modal_.move_settings_cursor(-1);
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::ConfigureMisa) {
            modal_.move_misa_cursor(-1);
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::ConfigureSystem) {
            modal_.move_sysconfig_cursor(-1);
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::ManageBreakpoints) {
            modal_.move_bp_cursor(-1);
            render(true);
            return true;
        }
        if (paused_ && left_pane_ && left_pane_->get_page() == TuiRegPage::CACHE) {
            left_pane_->cycle_cache_way(-1);
            render(true);
            return true;
        }
    } else if (esc_buf_ == "\033[B" || esc_buf_ == "\033OB") {
        if (get_active_modal() == ModalType::Settings) {
            modal_.move_settings_cursor(1);
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::ConfigureMisa) {
            modal_.move_misa_cursor(1);
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::ConfigureSystem) {
            modal_.move_sysconfig_cursor(1);
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::ManageBreakpoints) {
            modal_.move_bp_cursor(1);
            render(true);
            return true;
        }
        if (paused_ && left_pane_ && left_pane_->get_page() == TuiRegPage::CACHE) {
            left_pane_->cycle_cache_way(1);
            render(true);
            return true;
        }
    } else if (esc_buf_ == "\033[C" || esc_buf_ == "\033OC") {
        if (get_active_modal() == ModalType::Settings) {
            modal_.toggle_setting_at_cursor();
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::ConfigureMisa) {
            modal_.toggle_misa_at_cursor();
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::ConfigureSystem) {
            modal_.adjust_sysconfig_at_cursor(1);
            render(true);
            return true;
        }
        if (paused_ && left_pane_ && left_pane_->get_page() == TuiRegPage::CACHE) {
            left_pane_->select_next_cache_set(1);
            render(true);
            return true;
        }
    } else if (esc_buf_ == "\033[D" || esc_buf_ == "\033OD") {
        if (get_active_modal() == ModalType::Settings) {
            modal_.toggle_setting_at_cursor();
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::ConfigureMisa) {
            modal_.toggle_misa_at_cursor();
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::ConfigureSystem) {
            modal_.adjust_sysconfig_at_cursor(-1);
            render(true);
            return true;
        }
        if (paused_ && left_pane_ && left_pane_->get_page() == TuiRegPage::CACHE) {
            left_pane_->select_next_cache_set(-1);
            render(true);
            return true;
        }
    } else if (esc_buf_ == "\033[5~") {
        scroll(-10);
        return true;
    } else if (esc_buf_ == "\033[6~") {
        scroll(10);
        return true;
    } else if (esc_buf_ == "\033[H" || esc_buf_ == "\033[1~") {
        reset_scroll();
        return true;
    }
    return false;
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
            (byte >= 'A' && byte <= 'Z' && byte != 'O') || (byte >= 'a' && byte <= 'z')) {
            break;
        }
    }

    // 1. Mouse reporting
    int button = 0;
    int x = 0;
    int y = 0;
    if (parse_sgr_mouse(esc_buf_, button, x, y)) {
        if (esc_buf_.back() == 'M' && (button == 0 || button == 1 || button == 2)) {
            if (is_modal_active()) {
                close_modal();
                return true;
            }
        }
        if (esc_buf_.back() == 'M' && button == 0 && y == 2) {
            struct winsize w{};
            ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
            int term_w = w.ws_col > 0 ? w.ws_col : 80;
            if (status_bar_) {
                if (status_bar_->is_pos_on_status_badge(x, term_w)) {
                    if (machine_.is_shutdown_) {
                        machine_.request_reboot();
                        unpause_loop();
                    } else if (paused_) {
                        unpause_loop();
                    } else {
                        pause_loop();
                    }
                } else if (status_bar_->is_pos_on_right_panel_mode(x)) {
                    cycle_right_panel_mode();
                }
            }
            return true;
        }

        struct winsize w_footer{};
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w_footer);
        int term_h = w_footer.ws_row > 0 ? w_footer.ws_row : 24;

        if (esc_buf_.back() == 'M' && button == 0 && (y == term_h - 2 || y == term_h - 1)) {
            int col = x - 2;
            int row_idx = (y == term_h - 2) ? 0 : 1;
            if (status_bar_) {
                auto act_opt = status_bar_->get_footer_action_at_col(col, row_idx);
                if (act_opt.has_value()) {
                    execute_footer_action(act_opt.value());
                    return true;
                }
            }
        }

        if (esc_buf_.back() == 'M' && button == 0 && y == 4) {
            int pane_w = get_pane_width();
            if (x >= 2 && x <= pane_w + 1) {
                int col = x - 2;
                if (left_pane_) {
                    auto tab_opt = left_pane_->get_tab_at_col(col);
                    if (tab_opt.has_value()) {
                        if (*tab_opt == TuiRegPage::CACHE &&
                            left_pane_->get_page() == TuiRegPage::CACHE) {
                            left_pane_->toggle_cache_inspect_type();
                            render(true);
                        } else {
                            set_reg_page(tab_opt.value());
                        }
                    } else {
                        cycle_reg_page();
                    }
                }
                return true;
            }
        }

        if (esc_buf_.back() == 'M') {
            handle_mouse(x, y, button);
        }
        return true;
    }

    // 2. Alt modifier shortcuts
    if (esc_buf_.size() == 2) {
        if (handle_alt_key(esc_buf_.at(1), byte)) return true;
    }

    // 3. Arrow keys
    if (handle_arrow_key_sequence()) return true;

    // 4. Function key (F1) help shortcut
    if (esc_buf_ == "\033OP" || esc_buf_ == "\033[11~" || esc_buf_ == "\033[1;2P" ||
        esc_buf_ == "\033[O1P") {
        if (get_active_modal() == ModalType::Help)
            close_modal();
        else
            open_modal(ModalType::Help);
        return true;
    }

    if (esc_buf_.size() == 1) {
        if (is_modal_active()) {
            close_modal();
            return true;
        }
    }

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

void Tui::on_cycle_completed_slow() {
    uint64_t delay = step_delay_us_.load(std::memory_order_relaxed);
    if (delay > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(delay));
    }
}

}  // namespace simrv::tui
