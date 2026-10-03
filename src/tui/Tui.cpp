/**
 * @file Tui.cpp
 * @brief Terminal User Interface rendering, input management, and layout engine.
 */
#include "simrv/tui/Tui.hpp"

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
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
#include "simrv/device/Uart.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/tui/InspectionReport.hpp"
#include "simrv/tui/TuiBackend.hpp"
#include "simrv/tui/TuiFrameRenderer.hpp"
#include "simrv/tui/TuiGuidance.hpp"
#include "simrv/tui/TuiInputRouter.hpp"
#include "simrv/tui/TuiKey.hpp"
#include "simrv/tui/TuiKeybindings.hpp"
#include "simrv/tui/TuiLayoutPolicy.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/modals/ToolPickerModal.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"
#include "simrv/tui/panels/StatusBar.hpp"
#include "simrv/tui/panels/TerminalPane.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::tui {

static struct termios
    g_saved_termios;                  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_termios_saved = false;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_tui_active = false;     // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

namespace {

constexpr int kInspectorContentStartRow = 4;
constexpr int kInspectorLogAreaHeight = 6;

[[nodiscard]] auto inspector_log_start_row(int terminal_height) -> int {
    int const content_rows = std::max(1, terminal_height - framework::kFrameChromeRows);
    return kInspectorContentStartRow + (content_rows - kInspectorLogAreaHeight);
}

[[nodiscard]] auto inspector_content_end_row(int terminal_height) -> int {
    int const content_rows = std::max(1, terminal_height - framework::kFrameChromeRows);
    return kInspectorContentStartRow + content_rows - 1;
}

void write_all(int fd, std::string_view data) {
    while (!data.empty()) {
        const auto written = ::write(fd, data.data(), data.size());
        if (written > 0) {
            data.remove_prefix(static_cast<std::size_t>(written));
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

}  // namespace

extern "C" void emergency_terminal_restore() {
    std::fflush(stdout);
    if (g_tui_active) {
        const char* shutdown_seq =
            "\033[0m\033[?1016l\033[?1006l\033[?1003l\033[?1000l\033[?25h\033[2J\033[H\033[?"
            "1049l\n";
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
            "\033[0m\033[?1016l\033[?1006l\033[?1003l\033[?1000l\033[?25h\033[2J\033[H\033[?1049l\n"sv;
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
    student_guide_enabled_ = machine_.class_mode_enabled();
    mission_.configure(machine_.mission_id(), machine_.binary_path());
    update_trace_active_cache();
    vt_.set_scroll_offset_callback([this](int lines) -> void {
        if (scroll_offset_ > 0) {
            scroll_offset_ += lines;
        }
    });
    vt_.set_response_callback([this](std::string_view response) -> void {
        for (const char byte : response) write_guest_input(static_cast<uint8_t>(byte));
    });
    if (machine_.binary_path().empty()) {
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
    if (!p && machine_.primary_hart().state().pc == 0) {
        modal_.open_notice(
            "NO PROGRAM LOADED",
            "Cannot run simulation: PC is 0x0.\n\nPlease load a program binary image first [o].",
            false);
        return;
    }
    const bool cur_paused = paused_.load(std::memory_order_relaxed);
    const auto cur_machine_state = machine_.execution_state();
    const auto target_machine_state =
        p ? simrv::core::ExecutionState::Paused : simrv::core::ExecutionState::Running;
    if (cur_paused != p || cur_machine_state != target_machine_state) {
        paused_.store(p, std::memory_order_release);
        if (!p) {
            clear_status_override();
            last_runtime_tick_ = std::chrono::steady_clock::now();
            last_speed_update_ = std::chrono::steady_clock::now();
            last_icount_ = machine_.primary_hart().e_icount;
            machine_.resume();
        } else {
            if (last_runtime_tick_ != std::chrono::steady_clock::time_point{}) {
                runtime_duration_ += std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - last_runtime_tick_);
                last_runtime_tick_ = {};
            }
            machine_.pause();
        }
        update_trace_active_cache();
        trigger_immediate_render();
    }
}

void Tui::initialize() {
    backend_ = std::make_shared<LocalTuiBackend>(machine_);
    inspector_pane_ = std::make_unique<InspectorPane>(machine_, this);
    inspector_pane_->set_mission_progress(&mission_);
    terminal_pane_ = std::make_unique<TerminalPane>();
    status_bar_ = std::make_unique<StatusBar>(machine_, this);

    set_high_contrast(machine_.high_contrast_enabled());
    if (!machine_.is_paused()) {
        machine_.pause();
    }

    machine_.primary_hart().pipeline_sim.config.record_snapshots = true;
    for (size_t h = 0; h < machine_.num_harts(); ++h) {
        machine_.hart(h).pipeline_sim.config.record_snapshots = true;
    }

    inspector_pane_->update_cache();

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
    term.c_iflag &= ~(ICRNL | INLCR | IGNCR);
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

    const char* init_seq =
        "\033[?1049h\033[2J\033[H\033[?1000h\033[?1003h\033[?1006h\033[?1016h\033[?25l";
    (void)(::write(STDOUT_FILENO, init_seq, strlen(init_seq)) == 0);

    struct sigaction sa{};
    sa.sa_handler = handle_sigwinch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);

    simrv::log::set_tui_callback([this](const std::string& msg) -> void { print_log(msg); });

    start_ui_thread();

    if (machine_.binary_path().empty()) {
        open_modal(ModalType::LoadBinary);
    }
}

void Tui::shutdown() {
    stop_ui_thread();
    if (backend_) backend_->detach();
    simrv::log::set_tui_callback(nullptr);
    emergency_terminal_restore();

    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);
}

void Tui::start_ui_thread() {
    if (ui_running_.load(std::memory_order_relaxed)) {
        return;
    }
    ui_wake_.reset(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
    if (!ui_wake_) throw std::runtime_error("cannot create TUI wake descriptor");
    machine_.request_tui_sample();
    ui_running_.store(true, std::memory_order_release);
    ui_thread_ =
        std::jthread([this](const std::stop_token& stop_token) { ui_render_loop(stop_token); });
}

void Tui::stop_ui_thread() {
    if (!ui_running_.load(std::memory_order_relaxed)) {
        return;
    }
    ui_running_.store(false, std::memory_order_release);
    trigger_immediate_render();
    if (ui_thread_.joinable()) {
        ui_thread_.request_stop();
        if (ui_thread_.get_id() != std::this_thread::get_id()) {
            ui_thread_.join();
        } else {
            ui_thread_.detach();
        }
    }
}

void Tui::trigger_immediate_render() {
    if (!render_requested_.exchange(true, std::memory_order_acq_rel) && ui_wake_) {
        const uint64_t one = 1;
        (void)::write(ui_wake_.get(), &one, sizeof(one));
    }
}

void Tui::ui_render_loop(const std::stop_token& stop_token) {
    auto next_sample = std::chrono::steady_clock::now();
    while (!stop_token.stop_requested() && ui_running_.load(std::memory_order_relaxed)) {
        try {
            render_requested_.store(false, std::memory_order_release);
            processing_ui_input_.store(true, std::memory_order_release);
            update();
            processing_ui_input_.store(false, std::memory_order_release);
            const auto now = std::chrono::steady_clock::now();
            const auto interval =
                std::chrono::milliseconds(std::max(1u, 1000 / std::clamp(target_fps(), 1u, 120u)));
            if (now >= next_sample) {
                if (!is_paused() && backend_) backend_->request_sample();
                next_sample = now + interval;
            }
            const bool force = full_render_requested_.exchange(false, std::memory_order_acq_rel);
            if (is_paused() && (force || g_resized)) update_cache();
            render(force);
            const auto deadline = is_paused() && !frame_dirty_
                                      ? now + std::chrono::milliseconds(200)
                                      : last_draw_time_ + interval;
            const int timeout = static_cast<int>(
                std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::milliseconds>(
                                         deadline - std::chrono::steady_clock::now())
                                         .count()));
            pollfd fds[] = {{STDIN_FILENO, POLLIN, 0}, {ui_wake_.get(), POLLIN, 0}};
            (void)::poll(fds, 2, timeout);
            if (fds[1].revents & POLLIN) {
                uint64_t pending;
                (void)::read(ui_wake_.get(), &pending, sizeof(pending));
            }
        } catch (const std::exception& e) {
            simrv::log::error("TUI render loop exception: {}", e.what());
        } catch (...) {
            simrv::log::error("TUI render loop unknown exception");
        }
    }
}

void Tui::handle_char_write(char ch) {
    bool notify = false;
    {
        std::scoped_lock lock(io_mutex_);
        notify = tx_buffer_.empty();
        tx_buffer_.push_back(ch);
    }
    if (notify) trigger_immediate_render();
}

void Tui::print_log(const std::string& msg) {
    {
        std::scoped_lock lock(io_mutex_);
        log_fifo_.push(msg);
    }
    trigger_immediate_render();
}

void Tui::render_update_speed(std::chrono::steady_clock::time_point now) {
    const uint64_t current_icount = machine_.tui_execution_snapshot().instruction_count;
    if (!is_paused()) {
        if (last_runtime_tick_ != std::chrono::steady_clock::time_point{}) {
            runtime_duration_ +=
                std::chrono::duration_cast<std::chrono::microseconds>(now - last_runtime_tick_);
        }
        last_runtime_tick_ = now;
        auto diff =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_speed_update_).count();
        if (diff >= 50) {
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

auto Tui::get_terminal_pane_start_line(int num_rows) const -> int {
    int total_lines = vt_.get_scrollback_size() + vt_.get_cursor_y() + 1;
    int end_exclusive = std::max(0, total_lines - scroll_offset_);
    return std::max(0, end_exclusive - num_rows);
}

void Tui::render_build_lines(int inspector_width, int terminal_width, int num_rows,
                             TuiRightPanelMode panel_mode) {
    if (!status_override_.empty() &&
        status_override_expires_at_ != std::chrono::steady_clock::time_point::max() &&
        std::chrono::steady_clock::now() >= status_override_expires_at_) {
        clear_status_override();
    }
    if (!inspector_pane_ || !terminal_pane_ || !status_bar_) return;
    cached_num_rows_ = num_rows;
    lines_to_draw_.clear();
    if (terminal_width > 0 && num_rows > 0) {
        vt_.resize(terminal_width, num_rows);
        int total = vt_.get_lines_count();
        int start = get_terminal_pane_start_line(num_rows);
        int end_exclusive = std::min(total, start + num_rows);

        // Terminal output is parsed in guest-sized chunks. Reuse complete ANSI rows when a
        // frame observes the same chunk and geometry; selections intentionally bypass this
        // cache because they add presentation-only attributes.
        const uint64_t terminal_generation = vt_.generation();
        const bool reuse_terminal_rows =
            !selection_.is_active && terminal_rows_generation_ == terminal_generation &&
            terminal_rows_width_ == terminal_width && terminal_rows_count_ == num_rows &&
            terminal_rows_start_ == start;
        if (reuse_terminal_rows) {
            lines_to_draw_ = terminal_rows_cache_;
        } else {
            int const content_start_y = selection_.content_start_y;
            int vt_sel_start = start + (selection_.start_y - content_start_y);
            int vt_sel_end = start + (selection_.end_y - content_start_y);
            int sx1 = selection_.start_x;
            int sx2 = selection_.end_x;
            if (vt_sel_start > vt_sel_end || (vt_sel_start == vt_sel_end && sx1 > sx2)) {
                std::swap(vt_sel_start, vt_sel_end);
                std::swap(sx1, sx2);
            }

            for (int i = start; i < end_exclusive; ++i) {
                bool draw_cursor = false;
                int sel_start_x = -1;
                int sel_end_x = -1;
                if (selection_.is_active && selection_.pane == SelectionPane::TerminalPane) {
                    if (i >= vt_sel_start && i <= vt_sel_end) {
                        if (vt_sel_start == vt_sel_end) {
                            sel_start_x = sx1;
                            sel_end_x = sx2;
                        } else if (i == vt_sel_start) {
                            sel_start_x = sx1;
                            sel_end_x = terminal_width - 1;
                        } else if (i == vt_sel_end) {
                            sel_start_x = 0;
                            sel_end_x = sx2;
                        } else {
                            sel_start_x = 0;
                            sel_end_x = terminal_width - 1;
                        }
                    }
                }
                lines_to_draw_.push_back(
                    vt_.get_line_as_string(i, terminal_width, draw_cursor, sel_start_x, sel_end_x));
            }
            if (!selection_.is_active) {
                terminal_rows_cache_ = lines_to_draw_;
                terminal_rows_generation_ = terminal_generation;
                terminal_rows_width_ = terminal_width;
                terminal_rows_count_ = num_rows;
                terminal_rows_start_ = start;
            }
        }
        while (lines_to_draw_.size() < static_cast<std::size_t>(num_rows)) {
            lines_to_draw_.emplace_back(static_cast<std::size_t>(terminal_width), ' ');
        }
    }

    bool const log_visible = (num_rows >= 15 && !workbench_slots_.empty() &&
                              workbench_slots_[0].page != TuiRegPage::EXPLAIN &&
                              workbench_slots_[0].page != TuiRegPage::TRACE);
    if (log_visible) {
        int const log_width = std::max(10, inspector_width - 2);
        inspector_pane_->set_log_lines(log_buffer_.get_wrapped_lines(log_width, 100));
    } else {
        inspector_pane_->set_log_lines({});
    }

    inspector_pane_->set_selected_hart(selected_hart_);
    inspector_pane_->set_kips(kips_);
    inspector_pane_->set_max_kips(max_kips_);
    inspector_pane_->set_kips_history(kips_history_);
    inspector_pane_->set_paused(paused_);
    inspector_pane_->set_student_guide_enabled(student_guide_enabled_);
    inspector_pane_->set_visible_rows(num_rows);
    inspector_pane_->set_active_runtime(static_cast<double>(runtime_duration_.count()) / 1000000.0);
    inspector_pane_->set_trace_buffer(&trace_buffer_);
    inspector_pane_->refresh_execution_snapshot();
    terminal_pane_->set_lines(lines_to_draw_);
    terminal_pane_->set_scroll_offset(scroll_offset_);

    status_bar_->set_paused(paused_);
    status_bar_->set_status_override(status_override_);
    status_bar_->update_kips(kips_);
    status_bar_->set_layout(layout_);
    status_bar_->set_active_page(inspector_pane_->get_page());
    status_bar_->set_scroll_offset(scroll_offset_);
    status_bar_->set_pane_widths(inspector_width, terminal_width);
    status_bar_->set_column_width_overrides(user_column_widths_);
    status_bar_->set_right_panel_mode(panel_mode);
}

void Tui::render_draw_sixel(int panel_x, int display_width, int display_rows,
                            std::string& update_cmds) {
    if (modal_.is_active() || display_width <= 0 || display_rows <= 0) return;

    // Ensure a clean background across the entire display body before positioning
    // the Sixel graphic, clearing any leftover modal or text layer cells.
    if (!sixel_rendered_) {
        const std::string blank_row(static_cast<size_t>(display_width), ' ');
        for (int r = 5; r < 4 + display_rows; ++r) {
            update_cmds += std::format("\033[0m\033[{};{}H{}", r, panel_x, blank_row);
        }
    }

    const auto fb = machine_.framebuffer_view();
    if (fb.data() == nullptr) return;

    constexpr int source_width = 640;
    constexpr int source_height = 480;
    constexpr int palette_side = 6;
    constexpr int palette_size = palette_side * palette_side * palette_side;
    // Fit the 4:3 framebuffer to the panel's pixel rectangle, then round to whole terminal
    // cells so the placement remains stable as the terminal is resized.
    // The first row is the column header in the multi-column workbench.  Fit only
    // inside the body so the sixel never reaches the footer and triggers terminal
    // scrolling while it is placed.
    const int body_rows = std::max(1, display_rows - 1);
    const int cell_width = std::max(1, cell_width_px_);
    const int cell_height = std::max(1, cell_height_px_);
    // Fit the native framebuffer in pixels. Keep the source raster at native
    // resolution whenever the panel is large enough; resampling the 640x480
    // console font is what made the boot text unreadable.
    int width = std::min(source_width, std::max(1, display_width * cell_width));
    int height = std::max(1, width * source_height / source_width);
    const int max_height = std::max(1, body_rows * cell_height);
    if (height > max_height) {
        height = max_height;
        width = std::max(1, height * source_width / source_height);
    }
    const int image_cols = std::max(1, (width + cell_width - 1) / cell_width);
    const int image_rows = std::max(1, (height + cell_height - 1) / cell_height);

    const int x_offset = std::max(0, (display_width - image_cols) / 2);
    const int y_offset = std::max(0, (body_rows - image_rows) / 2);
    const int display_x = panel_x + x_offset;

    static constexpr auto kColorLut = [] {
        std::array<uint8_t, 256> lut{};
        for (int i = 0; i < 256; ++i) {
            lut[i] = static_cast<uint8_t>((i * (palette_side - 1) + 127) / 255);
        }
        return lut;
    }();

    static const std::string kSixelPaletteHeader = [] {
        std::string header;
        header.reserve(palette_size * 20);
        for (int color = 0; color < palette_size; ++color) {
            const int red = (color / (palette_side * palette_side)) % palette_side;
            const int green = (color / palette_side) % palette_side;
            const int blue = color % palette_side;
            header +=
                std::format("#{};2;{};{};{}", color, red * 100 / (palette_side - 1),
                            green * 100 / (palette_side - 1), blue * 100 / (palette_side - 1));
        }
        return header;
    }();

    // Raster attributes bound the graphic to the display column's pixel-sized rectangle.
    // Anchor at the top of the display body.  Anchoring at the bottom makes terminals
    // reserve space for the graphic and scroll the character grid to fit it.
    // Windows Terminal interprets the raster attributes as vertical:horizontal
    // pixel aspect values, so 1:1 is required here.  Using 7:1 makes the console
    // glyphs seven times taller than their native framebuffer shape.
    // Use cursor-positioned sixel so the graphic can live in its assigned panel.
    // The dimensions above ensure it fits before the footer, avoiding scroll.
    update_cmds += std::format("\033[5;{}r\033[?80l\033[{};{}H\033Pq\"1;1;{};{}", 4 + display_rows,
                               5 + y_offset, display_x, width, height);
    update_cmds += kSixelPaletteHeader;

    const auto* fb_bytes = reinterpret_cast<const uint8_t*>(fb.data());
    const bool same_dims = (width == source_width && height == source_height);
    std::vector<int> src_x_offset;
    if (!same_dims) {
        src_x_offset.resize(width);
        for (int x = 0; x < width; ++x) {
            src_x_offset[x] = std::min(source_width - 1, x * source_width / width) * 4;
        }
    }

    const size_t total_strips = static_cast<size_t>((height + 5) / 6);
    if (sixel_cached_width_ != width || sixel_cached_height_ != height) {
        sixel_cached_width_ = width;
        sixel_cached_height_ = height;
        sixel_strip_signatures_.clear();
        sixel_cached_strips_.clear();
    }
    if (sixel_strip_signatures_.size() != total_strips) {
        sixel_strip_signatures_.assign(total_strips, 0);
        sixel_cached_strips_.resize(total_strips);
    }

    std::vector<uint8_t> strip_masks(static_cast<size_t>(palette_size * width), 0);
    std::array<int16_t, palette_size> last_x_for_color;
    last_x_for_color.fill(-1);
    std::vector<uint8_t> present_colors;
    present_colors.reserve(32);

    auto append_uint = [](std::string& out, unsigned val) {
        char buf[16];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
        out.append(buf, static_cast<size_t>(ptr - buf));
    };

    for (int y = 0; y < height; y += 6) {
        const size_t strip_idx = static_cast<size_t>(y / 6);
        const int max_bits = std::min(6, height - y);

        uint64_t strip_hash = 1469598103934665603ULL;
        if (same_dims) {
            const auto* p64 = reinterpret_cast<const uint64_t*>(
                fb_bytes + static_cast<size_t>(y * source_width * 4));
            const size_t num_words =
                static_cast<size_t>(max_bits * source_width * 4) / sizeof(uint64_t);
            for (size_t w = 0; w < num_words; ++w) {
                strip_hash = (strip_hash ^ p64[w]) * 1099511628211ULL;
            }
        } else {
            for (int bit = 0; bit < max_bits; ++bit) {
                const int src_y = std::min(source_height - 1, (y + bit) * source_height / height);
                const auto* p64 = reinterpret_cast<const uint64_t*>(
                    fb_bytes + static_cast<size_t>(src_y * source_width * 4));
                const size_t words_per_row =
                    static_cast<size_t>(source_width * 4) / sizeof(uint64_t);
                for (size_t w = 0; w < words_per_row; ++w) {
                    strip_hash = (strip_hash ^ p64[w]) * 1099511628211ULL;
                }
            }
        }

        if (strip_hash != 0 && strip_hash == sixel_strip_signatures_[strip_idx] &&
            !sixel_cached_strips_[strip_idx].empty()) {
            update_cmds += sixel_cached_strips_[strip_idx];
            continue;
        }

        std::array<const uint8_t*, 6> row_ptrs;
        if (same_dims) {
            for (int bit = 0; bit < max_bits; ++bit) {
                row_ptrs[bit] = fb_bytes + static_cast<size_t>((y + bit) * source_width * 4);
            }
            for (int x = 0; x < width; ++x) {
                const int x4 = x * 4;
                for (int bit = 0; bit < max_bits; ++bit) {
                    const auto* p = row_ptrs[bit] + x4;
                    const uint8_t c = static_cast<uint8_t>((kColorLut[p[2]] * 36) +
                                                           (kColorLut[p[1]] * 6) + kColorLut[p[0]]);
                    if (last_x_for_color[c] < 0) {
                        present_colors.push_back(c);
                    }
                    strip_masks[c * width + x] |= static_cast<uint8_t>(1 << bit);
                    last_x_for_color[c] = static_cast<int16_t>(x);
                }
            }
        } else {
            for (int bit = 0; bit < max_bits; ++bit) {
                const int src_y = std::min(source_height - 1, (y + bit) * source_height / height);
                row_ptrs[bit] = fb_bytes + static_cast<size_t>(src_y * source_width * 4);
            }
            for (int x = 0; x < width; ++x) {
                const int x_off = src_x_offset[x];
                for (int bit = 0; bit < max_bits; ++bit) {
                    const auto* p = row_ptrs[bit] + x_off;
                    const uint8_t c = static_cast<uint8_t>((kColorLut[p[2]] * 36) +
                                                           (kColorLut[p[1]] * 6) + kColorLut[p[0]]);
                    if (last_x_for_color[c] < 0) {
                        present_colors.push_back(c);
                    }
                    strip_masks[c * width + x] |= static_cast<uint8_t>(1 << bit);
                    last_x_for_color[c] = static_cast<int16_t>(x);
                }
            }
        }

        std::string strip_cmds;
        for (const uint8_t color : present_colors) {
            const int last_x = last_x_for_color[color];
            if (last_x < 0) continue;

            strip_cmds += '#';
            append_uint(strip_cmds, color);

            const uint8_t* masks = &strip_masks[color * width];
            int x = 0;
            while (x <= last_x) {
                const uint8_t mask = masks[x];
                int run = 1;
                while (x + run <= last_x && masks[x + run] == mask && run < 255) {
                    ++run;
                }
                const char sixel_char = static_cast<char>(63 + mask);
                if (run >= 4) {
                    strip_cmds += '!';
                    append_uint(strip_cmds, run);
                    strip_cmds += sixel_char;
                } else {
                    strip_cmds.append(static_cast<size_t>(run), sixel_char);
                }
                x += run;
            }
            strip_cmds += '$';
        }
        strip_cmds += '-';

        for (const uint8_t c : present_colors) {
            const int last_x = last_x_for_color[c];
            if (last_x >= 0) {
                std::memset(&strip_masks[c * width], 0, static_cast<size_t>(last_x + 1));
                last_x_for_color[c] = -1;
            }
        }
        present_colors.clear();

        sixel_strip_signatures_[strip_idx] = strip_hash;
        sixel_cached_strips_[strip_idx] = strip_cmds;
        update_cmds += strip_cmds;
    }
    update_cmds += "\033\\\033[r\033[?80l";
}

auto Tui::render_framebuffer_row(int row, int width, int rows) const -> std::string {
    const auto fb = machine_.framebuffer_view();
    constexpr int source_width = 640;
    constexpr int source_height = 480;
    if (fb.data() == nullptr || width <= 0 || rows <= 0)
        return std::string(std::max(0, width), ' ');

    const int pixel_width = std::max(1, width);
    const int pixel_height = std::max(2, rows * 2);
    const int top = std::clamp(row * 2, 0, pixel_height - 1);
    const int bottom = std::min(pixel_height - 1, top + 1);
    std::string result;
    result.reserve(static_cast<size_t>(width) * 32);
    for (int x = 0; x < width; ++x) {
        const int source_x = std::min(source_width - 1, x * source_width / pixel_width);
        const int source_top = std::min(source_height - 1, top * source_height / pixel_height);
        const int source_bottom =
            std::min(source_height - 1, bottom * source_height / pixel_height);
        const auto* p_top = fb.unchecked_ptr(
            fb.base() + static_cast<Address>((source_top * source_width + source_x) * 4));
        const auto* p_bottom = fb.unchecked_ptr(
            fb.base() + static_cast<Address>((source_bottom * source_width + source_x) * 4));
        result += std::format(
            "\033[38;2;{};{};{}m\033[48;2;{};{};{}m▀", std::to_integer<uint8_t>(p_top[2]),
            std::to_integer<uint8_t>(p_top[1]), std::to_integer<uint8_t>(p_top[0]),
            std::to_integer<uint8_t>(p_bottom[2]), std::to_integer<uint8_t>(p_bottom[1]),
            std::to_integer<uint8_t>(p_bottom[0]));
    }
    result += "\033[0m";
    return result;
}

auto Tui::display_coords_to_fb(int x, int y, size_t col_idx, int term_width, int term_height) const
    -> std::optional<std::pair<int, int>> {
    auto const col_widths = column_widths(term_width);
    if (col_idx >= col_widths.count) return std::nullopt;

    int display_panel_x = 2;
    for (size_t c = 0; c < col_idx && c < col_widths.count; ++c) {
        display_panel_x += col_widths.widths[c] + 1;
    }
    const int col_width = col_widths.widths[col_idx];
    const int num_rows = std::max(1, term_height - framework::kFrameChromeRows);
    const int body_rows = std::max(1, num_rows - 1);
    const int cell_width = std::max(1, cell_width_px_);
    const int cell_height = std::max(1, cell_height_px_);
    constexpr int source_width = 640;
    constexpr int source_height = 480;

    int width = std::min(source_width, std::max(1, col_width * cell_width));
    int height = width * source_height / source_width;
    const int max_height = std::max(1, body_rows * cell_height);
    if (height > max_height) {
        height = max_height;
        width = std::max(1, height * source_width / source_height);
    }
    const int image_cols = std::max(1, (width + cell_width - 1) / cell_width);
    const int image_rows = std::max(1, (height + cell_height - 1) / cell_height);
    const int x_offset = std::max(0, (col_width - image_cols) / 2);
    const int y_offset = std::max(0, (body_rows - image_rows) / 2);
    const int display_x = display_panel_x + x_offset;
    const int display_y = 5 + y_offset;

    // Check if coordinates are reporting in SGR-Pixel mode (1016)
    if (x > term_width || y > term_height) {
        const int panel_pixel_x = (display_x - 1) * cell_width;
        const int panel_pixel_y = (display_y - 1) * cell_height;
        const int px = (x - 1) - panel_pixel_x;
        const int py = (y - 1) - panel_pixel_y;
        if (px >= 0 && px < width && py >= 0 && py < height) {
            const int fb_x = std::clamp(px * source_width / width, 0, source_width - 1);
            const int fb_y = std::clamp(py * source_height / height, 0, source_height - 1);
            return std::make_pair(fb_x, fb_y);
        }
        return std::nullopt;
    }

    const int rel_col = x - display_x;
    const int rel_row = y - display_y;
    if (rel_col >= 0 && rel_col < image_cols && rel_row >= 0 && rel_row < image_rows) {
        // Map through the cell center, scaled by the actual rendered image dimension
        // rather than the cell-padded image_cols/image_rows, preserving exact aspect ratio.
        const int px = std::clamp(rel_col * cell_width + (cell_width / 2), 0, width - 1);
        const int py = std::clamp(rel_row * cell_height + (cell_height / 2), 0, height - 1);
        const int fb_x = std::clamp(px * source_width / width, 0, source_width - 1);
        const int fb_y = std::clamp(py * source_height / height, 0, source_height - 1);
        return std::make_pair(fb_x, fb_y);
    }
    return std::nullopt;
}

void Tui::handle_display_mouse(int x, int y, int b, size_t col_idx, int term_width,
                               int term_height) {
    if (b == 64 || b == 65) {
        machine_.send_input_mouse_wheel(b == 64 ? 1 : -1);
        return;
    }
    auto const fb_pos = display_coords_to_fb(x, y, col_idx, term_width, term_height);
    if (!fb_pos) return;
    const auto [fb_x, fb_y] = *fb_pos;
    machine_.send_input_mouse_motion(fb_x, fb_y);
    display_mouse_last_fb_ = std::make_pair(fb_x, fb_y);
    uint16_t btn = 0;
    if (b == 0)
        btn = 0x110;  // BTN_LEFT
    else if (b == 1)
        btn = 0x112;  // BTN_MIDDLE
    else if (b == 2)
        btn = 0x111;  // BTN_RIGHT
    if (btn != 0) {
        machine_.send_input_mouse_button(btn, true);
    }
}

void Tui::render(bool force) {
    if (ui_running_.load(std::memory_order_acquire) && ui_thread_.joinable() &&
        (std::this_thread::get_id() != ui_thread_.get_id() ||
         processing_ui_input_.load(std::memory_order_acquire))) {
        if (force) full_render_requested_.store(true, std::memory_order_release);
        trigger_immediate_render();
        return;
    }
    std::unique_lock<std::mutex> lock(tui_mutex_);

    std::string local_tx;
    std::queue<std::string> local_log;
    {
        std::scoped_lock io_lock(io_mutex_);
        std::swap(tx_buffer_, local_tx);
        std::swap(log_fifo_, local_log);
    }
    const bool has_tx = !local_tx.empty();
    const bool has_log = !local_log.empty();
    frame_dirty_ = frame_dirty_ || has_tx || has_log;

    if (!local_tx.empty()) vt_.write_string(local_tx);

    while (!local_log.empty()) {
        log_buffer_.push(std::move(local_log.front()));
        local_log.pop();
    }

    if (!inspector_pane_ || !terminal_pane_ || !status_bar_) return;

    bool has_display = false;
    size_t display_col = 0;
    bool has_console = false;
    size_t console_col = 0;
    for (size_t c = 0; c < workbench_slots_.size(); ++c) {
        if (workbench_slots_[c].page == TuiRegPage::DISPLAY) {
            has_display = true;
            display_col = c;
        } else if (workbench_slots_[c].page == TuiRegPage::CONSOLE) {
            has_console = true;
            console_col = c;
        }
    }
    TuiRightPanelMode const panel_mode =
        has_display && !has_console ? TuiRightPanelMode::Display : TuiRightPanelMode::Terminal;
    auto now = std::chrono::steady_clock::now();
    const auto min_interval_us = 1'000'000U / std::clamp(target_fps(), 1u, 120u);
    const auto elapsed_us =
        std::chrono::duration_cast<std::chrono::microseconds>(now - last_draw_time_).count();
    const bool resized = g_resized != 0;
    const bool status_expiring =
        !status_override_.empty() &&
        status_override_expires_at_ != std::chrono::steady_clock::time_point::max();

    // A paused, unchanged frame has no sampled execution state to consume.  Input, logs,
    // resizes, explicit renders, and expiring status messages still invalidate it immediately.
    if (!force && !resized && is_paused() && !frame_dirty_ &&
        !trace_or_livetrace_active_.load(std::memory_order_relaxed) && !status_expiring) {
        render_stats_.suppressed_frames++;
        return;
    }

    if (!has_display && ui_running_.load(std::memory_order_relaxed) && !resized &&
        !status_expiring && elapsed_us + 1000 < min_interval_us) {
        frame_dirty_ = true;
        render_stats_.throttled_frames++;
        return;
    }
    if (!has_display && !force && !resized && elapsed_us + 1000 < min_interval_us) {
        render_stats_.suppressed_frames++;
        return;
    }
    frame_dirty_ = false;
    last_draw_time_ = now;
    if (resized) g_resized = 0;

    if (is_paused() || trace_or_livetrace_active_.load(std::memory_order_relaxed)) {
        drain_trace_records();
    }

    if (cached_term_width_ <= 0 || cached_term_height_ <= 0 || resized) {
        struct winsize w{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 && w.ws_row > 0) {
            cached_term_width_ = w.ws_col;
            cached_term_height_ = w.ws_row;
        } else {
            if (cached_term_width_ <= 0) cached_term_width_ = 80;
            if (cached_term_height_ <= 0) cached_term_height_ = 24;
        }
    }
    int const term_width = cached_term_width_;
    int const term_height = cached_term_height_;
    const FrameGeometry frame = calculate_frame_geometry(
        term_width, term_height, layout_, user_inspector_width_, user_column_widths_);
    if (!frame.renderable) return;

    render_update_speed(now);

    const int inspector_width = frame.panes.left;
    const int terminal_width = frame.panes.right;
    pane_width_cached_ = inspector_width;

    const int num_rows = frame.content_rows;
    auto col_widths = column_widths(term_width);
    if (workbench_slots_.size() != col_widths.count && col_widths.count > 0) {
        sync_workbench_slots();
    }
    const bool multi_headers = (col_widths.count >= 2);
    const bool draw_sixel = sixel_supported_ && has_display && !modal_.is_active();
    uint64_t framebuffer_signature = 0;
    if (draw_sixel) {
        const auto fb = machine_.framebuffer_view();
        if (fb.data() != nullptr && fb.size() >= sizeof(uint64_t)) {
            constexpr uint64_t fnv_offset = 1469598103934665603ULL;
            constexpr uint64_t fnv_prime = 1099511628211ULL;
            const auto* p64 = reinterpret_cast<const uint64_t*>(fb.data());
            const size_t n64 = fb.size() / sizeof(uint64_t);
            uint64_t h1 = fnv_offset;
            uint64_t h2 = fnv_prime;
            for (size_t i = 0; i < n64; i += 2) {
                h1 = (h1 ^ p64[i]) * fnv_prime;
                h2 = (h2 ^ p64[i + 1]) * fnv_offset;
            }
            framebuffer_signature = h1 ^ (h2 * 31ULL);
        }
    }
    const bool sixel_geometry_changed =
        draw_sixel && sixel_rendered_ &&
        (sixel_column_ != display_col || sixel_panel_width_ != col_widths.widths[display_col] ||
         sixel_panel_rows_ != num_rows);
    // A sixel graphic is a terminal-side object, not a character-grid cell.  Re-emitting it
    // on every ordinary render appends another graphic and can make terminals scroll the TUI
    // away.  Only emit a new object when it first becomes visible or its panel geometry
    // changes.
    const bool sixel_needs_draw =
        draw_sixel && (!sixel_rendered_ || sixel_geometry_changed ||
                       framebuffer_signature != sixel_framebuffer_signature_);
    int display_panel_x = 2;
    if (has_display) {
        for (size_t c = 0; c < display_col && c < col_widths.count; ++c)
            display_panel_x += col_widths.widths[c] + 1;
    }
    int const term_content_rows = multi_headers ? std::max(1, num_rows - 1) : num_rows;

    int console_width = terminal_width;
    if (has_console && console_col < col_widths.count) {
        console_width = col_widths.widths[console_col];
    }

    int total =
        (has_console || panel_mode == TuiRightPanelMode::Terminal) ? vt_.get_lines_count() : 0;
    scroll_offset_ = std::min(scroll_offset_, std::max(0, total - term_content_rows));

    render_build_lines(inspector_width, console_width, term_content_rows, panel_mode);
    lock.unlock();

    std::vector<std::string> new_lines = compose_multi_frame_lines(
        frame, term_width, col_widths, status_bar_->render_row(0, term_width),
        status_bar_->render_row(1, term_width),
        [this, total_cols = col_widths.count, multi_headers](size_t col_idx, int row,
                                                             int width) -> std::string {
            if (col_idx < workbench_slots_.size()) {
                const auto page = workbench_slots_[col_idx].page;
                const bool is_focused = (col_idx == focused_slot_index_);

                if (page == TuiRegPage::CONSOLE) {
                    if (multi_headers) {
                        if (row == 0) {
                            return inspector_pane_->render_column_header(
                                static_cast<int>(col_idx), "Console", is_focused, width, "", true,
                                total_cols > 1);
                        }
                        return terminal_pane_->render_row(row - 1, width);
                    }
                    return terminal_pane_->render_row(row, width);
                }
                if (page == TuiRegPage::DISPLAY) {
                    if (multi_headers && row == 0)
                        return inspector_pane_->render_column_header(static_cast<int>(col_idx),
                                                                     "Display", is_focused, width,
                                                                     "", true, total_cols > 1);
                    return std::string(static_cast<size_t>(std::max(0, width)), ' ');
                }
                inspector_pane_->set_page(page);
                auto rendered = inspector_pane_->render_column_row(
                    row, width, static_cast<int>(col_idx), total_cols, is_focused, multi_headers);
                if (selection_.is_active && selection_.pane == SelectionPane::InspectorPane &&
                    selection_.col_idx == col_idx) {
                    // The composed body always starts on terminal row 4.  The selected content
                    // starts at row 5 for multi-column headers and row 6 for a single
                    // inspector, so compare against the actual screen row rather than adding
                    // the content origin twice.
                    const int screen_y = 4 + row;
                    int start_y = selection_.start_y;
                    int end_y = selection_.end_y;
                    if (start_y > end_y) std::swap(start_y, end_y);
                    if (screen_y >= start_y && screen_y <= end_y) {
                        rendered = std::format("\033[7m{}\033[0m", rendered);
                    }
                }
                return rendered;
            }
            return "";
        });
    modal_.render_overlay(new_lines, term_width, term_height);

    const bool geometry_changed = (last_screen_lines_.size() != new_lines.size());
    const bool modal_closed = modal_active_last_frame_ && !modal_.is_active();
    modal_active_last_frame_ = modal_.is_active();
    const bool clear_sixel =
        sixel_rendered_ && (!draw_sixel || resized || full_screen_redraw_requested_ ||
                            geometry_changed || sixel_geometry_changed);
    const bool is_full_redraw =
        geometry_changed || resized || full_screen_redraw_requested_ || clear_sixel || modal_closed;
    full_screen_redraw_requested_ = false;

    std::string update_cmds;
    update_cmds.reserve(static_cast<std::size_t>(term_width) * new_lines.size() / 2);

    if (is_full_redraw) {
        update_cmds += "\033[H";
        for (size_t i = 0; i < new_lines.size(); ++i) {
            update_cmds += std::format("\033[{};1H{}", i + 1, new_lines[i]);
            render_stats_.lines_drawn++;
        }
        last_screen_lines_ = new_lines;
        render_stats_.full_redraws++;
    } else {
        bool any_line_changed = false;
        auto ansi_slice = [](std::string_view line, int begin_col, int end_col) {
            std::string result;
            int column = 0;
            for (size_t i = 0; i < line.size();) {
                if (line[i] == '\033') {
                    size_t end = i + 1;
                    if (end < line.size() && line[end] == '[') {
                        ++end;
                        while (end < line.size() && !(line[end] >= '@' && line[end] <= '~')) ++end;
                        if (end < line.size()) ++end;
                    } else if (end < line.size()) {
                        ++end;
                    }
                    if (column < end_col) result.append(line.substr(i, end - i));
                    i = end;
                    continue;
                }

                size_t bytes = 1;
                const auto lead = static_cast<unsigned char>(line[i]);
                if ((lead & 0xE0U) == 0xC0U)
                    bytes = 2;
                else if ((lead & 0xF0U) == 0xE0U)
                    bytes = 3;
                else if ((lead & 0xF8U) == 0xF0U)
                    bytes = 4;
                bytes = std::min(bytes, line.size() - i);
                const int glyph_width = get_display_width(line.substr(i, bytes));
                if (column >= begin_col && column < end_col) result.append(line.substr(i, bytes));
                column += glyph_width;
                i += bytes;
                if (column >= end_col && begin_col == 0) break;
            }
            return result;
        };

        const int display_panel_begin = display_panel_x - 1;
        const int display_panel_end = display_panel_begin + col_widths.widths[display_col];

        for (size_t i = 0; i < new_lines.size(); ++i) {
            if (new_lines[i] != last_screen_lines_[i]) {
                any_line_changed = true;
                const bool preserve_sixel = draw_sixel && sixel_rendered_ && !sixel_needs_draw &&
                                            !modal_.is_active() && i >= 4 &&
                                            i < 4 + static_cast<size_t>(num_rows);
                if (preserve_sixel) {
                    // Keep the display column's image-bearing cells untouched, but continue
                    // updating the inspector and neighboring columns on the same screen row.
                    update_cmds +=
                        std::format("\033[{};1H{}\033[{};{}H{}", i + 1,
                                    ansi_slice(new_lines[i], 0, display_panel_begin), i + 1,
                                    display_panel_end + 1,
                                    ansi_slice(new_lines[i], display_panel_end, term_width));
                } else {
                    update_cmds += std::format("\033[{};1H{}", i + 1, new_lines[i]);
                }
                last_screen_lines_[i] = new_lines[i];
                render_stats_.lines_drawn++;
            } else {
                render_stats_.lines_skipped++;
            }
        }
        if (any_line_changed) {
            render_stats_.differential_redraws++;
        }
    }

    int target_cursor_x = -1;
    int target_cursor_y = -1;
    bool target_cursor_visible = false;

    if (sixel_needs_draw && has_display && display_col < col_widths.count &&
        col_widths.widths[display_col] > 0) {
        render_draw_sixel(display_panel_x, col_widths.widths[display_col], num_rows, update_cmds);
    }
    if (has_console && console_col < col_widths.count && !modal_.is_active()) {
        size_t const c_idx = console_col;
        int col_start_x = 2;
        for (size_t c = 0; c < c_idx && c < col_widths.count; ++c) {
            col_start_x += col_widths.widths[c] + 1;
        }
        int const target_x = std::clamp(col_start_x + vt_.get_cursor_x(), col_start_x,
                                        col_start_x + col_widths.widths[c_idx] - 1);
        int const cursor_abs_line = vt_.get_scrollback_size() + vt_.get_cursor_y();
        int const start_line = get_terminal_pane_start_line(term_content_rows);
        int const line_offset = cursor_abs_line - start_line;
        if (line_offset >= 0 && line_offset < term_content_rows) {
            int const content_start_y = multi_headers ? 5 : 4;
            target_cursor_y = content_start_y + line_offset;
            target_cursor_x = target_x;
            if (!paused_ && vt_.is_cursor_visible()) {
                target_cursor_visible = true;
            }
        }
    } else {
        target_cursor_y = term_height;
        target_cursor_x = 1;
        target_cursor_visible = false;
    }

    const bool cursor_pos_changed =
        (target_cursor_x != last_cursor_x_ || target_cursor_y != last_cursor_y_);
    const bool cursor_vis_changed = (target_cursor_visible != last_cursor_visible_);

    if (cursor_vis_changed || is_full_redraw) {
        if (target_cursor_visible) {
            update_cmds += "\033[?25h";
        } else {
            update_cmds += "\033[?25l";
        }
        last_cursor_visible_ = target_cursor_visible;
    }

    if (target_cursor_x > 0 && target_cursor_y > 0) {
        if (cursor_pos_changed || is_full_redraw || !update_cmds.empty()) {
            update_cmds += std::format("\033[{};{}H", target_cursor_y, target_cursor_x);
            last_cursor_x_ = target_cursor_x;
            last_cursor_y_ = target_cursor_y;
        }
    } else {
        last_cursor_x_ = target_cursor_x;
        last_cursor_y_ = target_cursor_y;
    }

    if (update_cmds.empty()) {
        render_stats_.suppressed_frames++;
        return;
    }

    std::string clear_graphic;
    if (clear_sixel) {
        // Sixel graphics live in a terminal-side layer. Explicitly overwrite the old
        // placement with reset spaces before drawing a modal or the normal TUI again.
        // Keep this as a standalone VT write: Windows Terminal may retain the image
        // layer when the erase is bundled inside synchronized output or an alt-screen
        // reset sequence.
        clear_graphic = "\033[0m\033[?7l";
        for (int row = 5; row < 4 + sixel_panel_rows_; ++row) {
            clear_graphic +=
                std::format("\033[0m\033[{};{}H{}", row, sixel_panel_x_,
                            std::string(static_cast<size_t>(std::max(0, sixel_panel_width_)), ' '));
        }
        clear_graphic += "\033[0m\033[?7h";
    }

    const bool clean_panel_before_first_sixel =
        draw_sixel && (!sixel_rendered_ || sixel_geometry_changed || is_full_redraw);
    if (clean_panel_before_first_sixel && has_display && display_col < col_widths.count &&
        num_rows > 0 && col_widths.widths[display_col] > 0) {
        clear_graphic += "\033[0m\033[?7l";
        const int display_width = std::max(0, col_widths.widths[display_col]);
        const std::string blank_row(static_cast<size_t>(display_width), ' ');
        for (int row = 5; row < 4 + num_rows; ++row) {
            clear_graphic += std::format("\033[0m\033[{};{}H{}", row, display_panel_x, blank_row);
        }
        clear_graphic += "\033[0m\033[?7h";
    }

    if (!clear_graphic.empty()) write_all(STDOUT_FILENO, clear_graphic);

    std::string frame_output;
    frame_output.reserve(update_cmds.size() + 16);
    frame_output += "\033[?2026h";
    frame_output += update_cmds;
    frame_output += "\033[?2026l";
    write_all(STDOUT_FILENO, frame_output);
    sixel_rendered_ = draw_sixel;
    if (draw_sixel) {
        sixel_column_ = display_col;
        sixel_panel_width_ = col_widths.widths[display_col];
        sixel_panel_rows_ = num_rows;
        sixel_panel_x_ = display_panel_x;
        sixel_framebuffer_signature_ = framebuffer_signature;
    } else {
        sixel_column_ = std::numeric_limits<size_t>::max();
        sixel_panel_width_ = 0;
        sixel_panel_rows_ = 0;
        sixel_panel_x_ = 0;
        sixel_framebuffer_signature_ = 0;
    }
}

void Tui::handle_mouse_inspector(int x, int y, int b, bool multi_column, bool is_secondary,
                                 int col_width) {
    winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int const term_height =
        (cached_term_height_ > 0) ? cached_term_height_ : (w.ws_row > 0 ? w.ws_row : 24);
    int const num_rows = std::max(1, term_height - framework::kFrameChromeRows);
    int const log_start_y = inspector_log_start_row(term_height);
    bool const has_log_area = (!is_secondary && inspector_pane_ && num_rows >= 15 &&
                               inspector_pane_->get_page() != TuiRegPage::EXPLAIN &&
                               inspector_pane_->get_page() != TuiRegPage::TRACE);

    if (b == 0) {
        if (has_log_area && y == log_start_y) {
            inspector_pane_->reset_log_scroll();
            render(true);
            return;
        }
        if (has_log_area && y > log_start_y) {
            return;
        }

        if (!multi_column) {
            if (y == 4) {
                int const col = x - 2;
                if (col < 0) return;
                auto tab = inspector_pane_->get_tab_at(0, col);
                if (tab.has_value()) {
                    set_reg_page(*tab);
                }
                return;
            }
            if (y == 5) {
                int const col = x - 2;
                if (col < 0) return;
                auto tab = inspector_pane_->get_tab_at(1, col);
                if (tab.has_value()) {
                    if (*tab == TuiRegPage::CACHE &&
                        inspector_pane_->get_page() == TuiRegPage::CACHE) {
                        inspector_pane_->toggle_cache_inspect_type();
                        render(true);
                        return;
                    }
                    inspector_pane_->set_previous_page(inspector_pane_->get_page());
                    set_reg_page(*tab);
                }
                return;
            }
        }

        const int content_start_y = multi_column ? 5 : 6;
        if (y >= content_start_y) {
            int const target_width = (col_width > 0) ? col_width : pane_width_cached_;
            int logical_row = (y - content_start_y) + inspector_pane_->get_scroll_offset();
            auto page = inspector_pane_->get_page();
            // Running panes render a sampled/spinner view and deliberately hide values that
            // require a coherent architectural snapshot. Their old hitboxes must not remain
            // active while the visible value is unavailable.
            if (!paused_) return;
            if (page == TuiRegPage::CACHE) {
                if (logical_row == 0 || logical_row == 4) {
                    inspector_pane_->toggle_cache_inspect_type();
                    render(true);
                }
            } else if (page == TuiRegPage::PIPELINE) {
                Register clicked_pc = inspector_pane_->get_pipeline_pc_at_row(logical_row);
                if (clicked_pc != 0) {
                    inspector_pane_->set_previous_page(TuiRegPage::PIPELINE);
                    inspector_pane_->set_explain_pc(clicked_pc);
                    set_reg_page(TuiRegPage::EXPLAIN);
                }
            } else if (page == TuiRegPage::EXPLAIN) {
                if (logical_row <= 1) {
                    auto prev = inspector_pane_->get_previous_page();
                    if (prev.has_value()) set_reg_page(*prev);
                }
            } else if (page == TuiRegPage::GPR || page == TuiRegPage::FPR) {
                auto reg_val =
                    inspector_pane_->get_register_value_at_row(logical_row, x, target_width);
                if (reg_val.has_value()) {
                    inspector_pane_->set_inspect_addr(*reg_val);
                    open_modal(ModalType::InspectAddress);
                }
            } else if (page == TuiRegPage::STACK) {
                auto stack_addr = inspector_pane_->get_stack_addr_at_row(logical_row);
                if (stack_addr.has_value()) {
                    inspector_pane_->set_inspect_addr(*stack_addr);
                    open_modal(ModalType::InspectAddress);
                }
            }
        }
    } else if ((b == 66 || b == 68) && inspector_pane_->supports_horizontal_scroll()) {
        inspector_pane_->scroll_horizontal(-8);
        render(true);
    } else if ((b == 67 || b == 69) && inspector_pane_->supports_horizontal_scroll()) {
        inspector_pane_->scroll_horizontal(8);
        render(true);
    } else if (b == 64) {
        if (has_log_area && y >= log_start_y) {
            inspector_pane_->scroll_log(2);
            render(true);
        } else {
            scroll_inspector(-2);
        }
    } else if (b == 65) {
        if (has_log_area && y >= log_start_y) {
            inspector_pane_->scroll_log(-2);
            render(true);
        } else {
            scroll_inspector(2);
        }
    }
}

static auto base64_encode(std::string_view input) -> std::string {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    uint32_t val = 0;
    int valb = -6;
    for (uint8_t c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(kTable[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(kTable[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4 != 0) {
        out.push_back('=');
    }
    return out;
}

void Tui::copy_to_clipboard(std::string_view text) {
    if (text.empty()) return;
    std::string b64 = base64_encode(text);
    std::string seq = "\033]52;c;" + b64 + "\a";
    (void)(::write(STDOUT_FILENO, seq.data(), seq.size()) == 0);
    ::fflush(stdout);
}

void Tui::clear_selection() {
    selection_ = SelectionState{};
    render(true);
}

void Tui::copy_active_selection() {
    if (!selection_.is_active) return;
    std::string text;

    if (selection_.pane == SelectionPane::TerminalPane && terminal_pane_) {
        int start_line = get_terminal_pane_start_line(cached_num_rows_);
        int start_r = start_line + (selection_.start_y - selection_.content_start_y);
        int end_r = start_line + (selection_.end_y - selection_.content_start_y);
        text = vt_.get_text_in_range(start_r, selection_.start_x, end_r, selection_.end_x);
    } else if (selection_.pane == SelectionPane::InspectorPane && inspector_pane_) {
        int start_r = selection_.start_y - selection_.content_start_y;
        int end_r = selection_.end_y - selection_.content_start_y;
        text = inspector_pane_->get_text_in_range(start_r, selection_.start_x, end_r,
                                                  selection_.end_x, selection_.pane_width);
    }

    if (!text.empty()) {
        copy_to_clipboard(text);
        set_status_override(std::format("COPIED {} B TO CLIPBOARD", text.size()));
    }
}

void Tui::handle_mouse(int x, int y, int b) {
    if (!terminal_pane_ || !inspector_pane_) return;

    struct winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int const term_width =
        (cached_term_width_ > 0) ? cached_term_width_ : (w.ws_col > 0 ? w.ws_col : 80);
    int const term_height =
        (cached_term_height_ > 0) ? cached_term_height_ : (w.ws_row > 0 ? w.ws_row : 24);

    auto const col_widths = column_widths(term_width);
    if (col_widths.count == 0) return;

    // Find which column x falls into
    size_t clicked_col = 0;
    int cur_x = 1;
    int col_local_x = 0;
    for (size_t i = 0; i < col_widths.count; ++i) {
        int const cw = col_widths.widths[i];
        if (x >= cur_x && (x < cur_x + cw + 1 || i + 1 == col_widths.count)) {
            clicked_col = i;
            col_local_x = std::max(0, x - cur_x - 1);
            break;
        }
        cur_x += cw + 1;
    }

    const size_t previous_focused_slot = focused_slot_index_;

    if (b == 0 && clicked_col < workbench_slots_.size() && y != 4) {
        focused_slot_index_ = clicked_col;
        update_trace_active_cache();
    }

    if (y < 4) {
        // Header clicks: row 2 is the actual header text row; rows 1 and 3 are top border and
        // divider
        if (b == 0 && status_bar_ && y == 2) {
            auto hit = status_bar_->get_header_action_at_col(x, term_width);
            execute_header_action(hit);
        }
        return;
    }

    // Body clicks (y >= 4)
    if (clicked_col < workbench_slots_.size()) {
        auto page = workbench_slots_[clicked_col].page;
        if (b == 64) {
            focused_slot_index_ = clicked_col;
            if (page == TuiRegPage::CONSOLE) {
                scroll(5);
            } else if (page == TuiRegPage::DISPLAY) {
                machine_.send_input_mouse_wheel(1);
                return;
            } else {
                inspector_pane_->set_page(page);
                int const num_rows = std::max(1, term_height - framework::kFrameChromeRows);
                int const log_start_y = inspector_log_start_row(term_height);
                bool const log_area = clicked_col == 0 && num_rows >= 15 &&
                                      page != TuiRegPage::EXPLAIN && page != TuiRegPage::TRACE &&
                                      y >= log_start_y;
                if (log_area)
                    inspector_pane_->scroll_log(2);
                else
                    inspector_pane_->scroll(-2, col_widths.widths[clicked_col], clicked_col > 0);
                render(true);
            }
            return;
        }
        if (b == 65) {
            focused_slot_index_ = clicked_col;
            if (page == TuiRegPage::CONSOLE) {
                scroll(-5);
            } else if (page == TuiRegPage::DISPLAY) {
                machine_.send_input_mouse_wheel(-1);
                return;
            } else {
                inspector_pane_->set_page(page);
                int const num_rows = std::max(1, term_height - framework::kFrameChromeRows);
                int const log_start_y = inspector_log_start_row(term_height);
                bool const log_area = clicked_col == 0 && num_rows >= 15 &&
                                      page != TuiRegPage::EXPLAIN && page != TuiRegPage::TRACE &&
                                      y >= log_start_y;
                if (log_area)
                    inspector_pane_->scroll_log(-2);
                else
                    inspector_pane_->scroll(2, col_widths.widths[clicked_col], clicked_col > 0);
                render(true);
            }
            return;
        }
        if (b == 66 || b == 67 || b == 68 || b == 69) {
            focused_slot_index_ = clicked_col;
            if (page != TuiRegPage::CONSOLE) {
                inspector_pane_->set_page(page);
                if (inspector_pane_->supports_horizontal_scroll(col_widths.widths[clicked_col])) {
                    inspector_pane_->scroll_horizontal((b == 66 || b == 68) ? -4 : 4,
                                                       col_widths.widths[clicked_col],
                                                       clicked_col > 0);
                    render(true);
                }
            }
            return;
        }
        const bool col_has_header = (col_widths.count >= 2);

        if (b == 0 && y == 4 && col_has_header) {
            if (col_widths.count > 1 && col_widths.widths[clicked_col] >= 30 &&
                col_local_x >= col_widths.widths[clicked_col] - 5) {
                close_column(clicked_col);
                return;
            }
            if (clicked_col == previous_focused_slot) {
                bool has_menu = true;
                if (clicked_col < workbench_slots_.size()) {
                    has_menu = inspector_pane_->has_tool_menu(workbench_slots_[clicked_col].page);
                }
                if (has_menu) {
                    open_tool_picker(clicked_col);
                }
            } else {
                focused_slot_index_ = clicked_col;
                if (clicked_col < workbench_slots_.size()) {
                    inspector_pane_->set_page(workbench_slots_[clicked_col].page);
                }
                update_trace_active_cache();
                render(true);
            }
            return;
        }

        if (page == TuiRegPage::CONSOLE) {
            if (b == 0) {
                render(false);
            }
            return;
        }

        if (page == TuiRegPage::DISPLAY) {
            if (b == 0 || b == 1 || b == 2) display_mouse_capture_ = clicked_col;
            handle_display_mouse(x, y, b, clicked_col, term_width, term_height);
            return;
        }

        if (clicked_col < workbench_slots_.size()) {
            inspector_pane_->set_page(workbench_slots_[clicked_col].page);
        }
        const bool is_multi = (col_widths.count >= 2);
        const bool is_secondary = (clicked_col > 0);
        const int col_width = col_widths.widths[clicked_col];
        handle_mouse_inspector(col_local_x + 2, y, b, is_multi, is_secondary, col_width);
    }
}

void Tui::sync_workbench_slots() {
    size_t desired = 2;
    if (layout_ == TuiLayout::FullLeft || layout_ == TuiLayout::FullRight) {
        desired = 1;
    } else if (layout_ == TuiLayout::Split) {
        desired = 2;
    } else if (layout_ == TuiLayout::ThreeColumn) {
        desired = 3;
    } else if (layout_ == TuiLayout::FourColumn) {
        desired = 4;
    }

    if (workbench_slots_.empty()) {
        workbench_slots_.push_back({TuiRegPage::GPR, 0});
    }

    if (desired == 1) {
        if (layout_ == TuiLayout::FullRight) {
            workbench_slots_ = {{TuiRegPage::CONSOLE, 0}};
        } else {
            workbench_slots_ = {{TuiRegPage::GPR, 0}};
        }
    } else if (desired == 2) {
        if (workbench_slots_.size() < 2) {
            workbench_slots_.push_back({TuiRegPage::CONSOLE, 0});
        } else if (workbench_slots_.size() > 2) {
            workbench_slots_.resize(2);
        }
    } else if (desired == 3) {
        if (workbench_slots_.size() < 3) {
            if (workbench_slots_.size() == 1) {
                workbench_slots_.push_back({TuiRegPage::STACK, 0});
                workbench_slots_.push_back({TuiRegPage::CONSOLE, 0});
            } else {
                workbench_slots_.insert(workbench_slots_.begin() + 1, {TuiRegPage::STACK, 0});
            }
        } else if (workbench_slots_.size() > 3) {
            workbench_slots_.resize(3);
        }
    } else if (desired == 4) {
        if (workbench_slots_.size() == 1) {
            workbench_slots_ = {{TuiRegPage::GPR, 0},
                                {TuiRegPage::STACK, 0},
                                {TuiRegPage::TRACE, 0},
                                {TuiRegPage::CONSOLE, 0}};
        } else if (workbench_slots_.size() == 2) {
            auto last = workbench_slots_.back();
            workbench_slots_.pop_back();
            workbench_slots_.push_back({TuiRegPage::STACK, 0});
            workbench_slots_.push_back({TuiRegPage::TRACE, 0});
            workbench_slots_.push_back(last);
        } else if (workbench_slots_.size() == 3) {
            auto last = workbench_slots_.back();
            workbench_slots_.pop_back();
            workbench_slots_.push_back({TuiRegPage::TRACE, 0});
            workbench_slots_.push_back(last);
        } else if (workbench_slots_.size() > 4) {
            workbench_slots_.resize(4);
        }
    }

    if (focused_slot_index_ >= workbench_slots_.size()) {
        focused_slot_index_ = 0;
    }

    if (!machine_.runtime_profile.is_cycle_mode()) {
        for (auto& slot : workbench_slots_) {
            if (slot.page == TuiRegPage::CACHE) slot.page = TuiRegPage::TLB;
            if (slot.page == TuiRegPage::BPRED || slot.page == TuiRegPage::HAZARD) {
                slot.page = TuiRegPage::TRACE;
            }
        }
    }
    update_trace_active_cache();
}

void Tui::apply_layout_preset(LayoutPreset preset) {
    winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int const term_width =
        (cached_term_width_ > 0) ? cached_term_width_ : (w.ws_col > 0 ? w.ws_col : 80);
    int const max_cols = framework::max_supported_columns(term_width);
    const bool is_cycle = machine_.runtime_profile.is_cycle_mode();
    user_inspector_width_ = -1;
    user_column_widths_ = framework::kNoColumnWidthOverrides;
    resize_history_.clear();

    switch (preset) {
        case LayoutPreset::GeneralDebug:
            if (max_cols >= 4) {
                layout_ = TuiLayout::FourColumn;
                workbench_slots_ = {{TuiRegPage::GPR, 0},
                                    {TuiRegPage::STACK, 0},
                                    {TuiRegPage::TRACE, 0},
                                    {TuiRegPage::CONSOLE, 0}};
            } else if (max_cols >= 3) {
                layout_ = TuiLayout::ThreeColumn;
                workbench_slots_ = {
                    {TuiRegPage::GPR, 0}, {TuiRegPage::STACK, 0}, {TuiRegPage::CONSOLE, 0}};
            } else if (max_cols >= 2) {
                layout_ = TuiLayout::Split;
                workbench_slots_ = {{TuiRegPage::GPR, 0}, {TuiRegPage::CONSOLE, 0}};
            } else {
                layout_ = TuiLayout::FullLeft;
                workbench_slots_ = {{TuiRegPage::GPR, 0}};
            }
            set_status_override("Layout Preset: General Debug [Alt-1]");
            break;

        case LayoutPreset::Microarchitecture:
            if (max_cols >= 4) {
                layout_ = TuiLayout::FourColumn;
                if (is_cycle) {
                    workbench_slots_ = {{TuiRegPage::PIPELINE, 0},
                                        {TuiRegPage::CACHE, 0},
                                        {TuiRegPage::HAZARD, 0},
                                        {TuiRegPage::CONSOLE, 0}};
                } else {
                    workbench_slots_ = {{TuiRegPage::PIPELINE, 0},
                                        {TuiRegPage::STACK, 0},
                                        {TuiRegPage::TRACE, 0},
                                        {TuiRegPage::CONSOLE, 0}};
                }
            } else if (max_cols >= 3) {
                layout_ = TuiLayout::ThreeColumn;
                if (is_cycle) {
                    workbench_slots_ = {{TuiRegPage::PIPELINE, 0},
                                        {TuiRegPage::CACHE, 0},
                                        {TuiRegPage::CONSOLE, 0}};
                } else {
                    workbench_slots_ = {{TuiRegPage::PIPELINE, 0},
                                        {TuiRegPage::STACK, 0},
                                        {TuiRegPage::CONSOLE, 0}};
                }
            } else if (max_cols >= 2) {
                layout_ = TuiLayout::Split;
                workbench_slots_ = {{TuiRegPage::PIPELINE, 0}, {TuiRegPage::CONSOLE, 0}};
            } else {
                layout_ = TuiLayout::FullLeft;
                workbench_slots_ = {{TuiRegPage::PIPELINE, 0}};
            }
            set_status_override("Layout Preset: Microarchitecture [Alt-2]");
            break;

        case LayoutPreset::TraceExecution:
            if (max_cols >= 4) {
                layout_ = TuiLayout::FourColumn;
                workbench_slots_ = {{TuiRegPage::GPR, 0},
                                    {TuiRegPage::TRACE, 0},
                                    {TuiRegPage::EXPLAIN, 0},
                                    {TuiRegPage::CONSOLE, 0}};
            } else if (max_cols >= 3) {
                layout_ = TuiLayout::ThreeColumn;
                workbench_slots_ = {
                    {TuiRegPage::GPR, 0}, {TuiRegPage::TRACE, 0}, {TuiRegPage::CONSOLE, 0}};
            } else if (max_cols >= 2) {
                layout_ = TuiLayout::Split;
                workbench_slots_ = {{TuiRegPage::TRACE, 0}, {TuiRegPage::CONSOLE, 0}};
            } else {
                layout_ = TuiLayout::FullLeft;
                workbench_slots_ = {{TuiRegPage::TRACE, 0}};
            }
            set_status_override("Layout Preset: Trace & Execution [Alt-3]");
            break;

        case LayoutPreset::MemoryInterconnect:
            if (max_cols >= 4) {
                layout_ = TuiLayout::FourColumn;
                if (is_cycle) {
                    workbench_slots_ = {{TuiRegPage::STACK, 0},
                                        {TuiRegPage::CACHE, 0},
                                        {TuiRegPage::TLB, 0},
                                        {TuiRegPage::BUS, 0}};
                } else {
                    workbench_slots_ = {{TuiRegPage::STACK, 0},
                                        {TuiRegPage::TLB, 0},
                                        {TuiRegPage::BUS, 0},
                                        {TuiRegPage::CONSOLE, 0}};
                }
            } else if (max_cols >= 3) {
                layout_ = TuiLayout::ThreeColumn;
                if (is_cycle) {
                    workbench_slots_ = {
                        {TuiRegPage::STACK, 0}, {TuiRegPage::CACHE, 0}, {TuiRegPage::BUS, 0}};
                } else {
                    workbench_slots_ = {
                        {TuiRegPage::STACK, 0}, {TuiRegPage::TLB, 0}, {TuiRegPage::BUS, 0}};
                }
            } else if (max_cols >= 2) {
                layout_ = TuiLayout::Split;
                workbench_slots_ = {{TuiRegPage::STACK, 0}, {TuiRegPage::BUS, 0}};
            } else {
                layout_ = TuiLayout::FullLeft;
                workbench_slots_ = {{TuiRegPage::STACK, 0}};
            }
            set_status_override("Layout Preset: Memory & Interconnect [Alt-4]");
            break;
    }

    if (focused_slot_index_ >= workbench_slots_.size()) {
        focused_slot_index_ = 0;
    }
    if (inspector_pane_) {
        inspector_pane_->set_page(workbench_slots_[focused_slot_index_].page);
    }
    resize_history_.clear();
    update_trace_active_cache();
    request_full_screen_redraw();
    render(true);
}

void Tui::focus_next_slot() {
    if (workbench_slots_.empty()) return;
    focused_slot_index_ = (focused_slot_index_ + 1) % workbench_slots_.size();
    update_trace_active_cache();
    set_status_override(std::format("Focused Column {}: {}", focused_slot_index_ + 1,
                                    get_page_name(workbench_slots_[focused_slot_index_].page)));
    render(true);
}

void Tui::focus_prev_slot() {
    if (workbench_slots_.empty()) return;
    focused_slot_index_ =
        (focused_slot_index_ == 0) ? workbench_slots_.size() - 1 : focused_slot_index_ - 1;
    update_trace_active_cache();
    set_status_override(std::format("Focused Column {}: {}", focused_slot_index_ + 1,
                                    get_page_name(workbench_slots_[focused_slot_index_].page)));
    render(true);
}

void Tui::set_workbench_slot_page(size_t slot_idx, TuiRegPage page) {
    if (slot_idx < workbench_slots_.size()) {
        workbench_slots_[slot_idx].page = page;
        if (slot_idx == 0 && inspector_pane_) {
            inspector_pane_->set_page(page);
        }
        update_trace_active_cache();
        render(true);
    }
}

void Tui::cycle_slot_page(size_t slot_idx) {
    if (slot_idx >= workbench_slots_.size() || !inspector_pane_ ||
        workbench_slots_[slot_idx].page == TuiRegPage::CONSOLE) {
        return;
    }
    focused_slot_index_ = slot_idx;
    auto const next = inspector_pane_->next_page_for_slot(workbench_slots_[slot_idx].page,
                                                          machine_.runtime_profile.is_cycle_mode());
    set_workbench_slot_page(slot_idx, next);
}

void Tui::open_tool_picker(size_t slot_idx) {
    if (workbench_slots_.empty()) return;
    if (slot_idx >= workbench_slots_.size()) slot_idx = 0;
    focused_slot_index_ = slot_idx;
    modal_.open_tool_picker(static_cast<int>(slot_idx), workbench_slots_[slot_idx].page,
                            static_cast<int>(workbench_slots_.size()));
    render(true);
}

void Tui::swap_workbench_slots(size_t slot_a, size_t slot_b) {
    if (slot_a >= workbench_slots_.size() || slot_b >= workbench_slots_.size() ||
        slot_a == slot_b) {
        return;
    }
    std::swap(workbench_slots_[slot_a], workbench_slots_[slot_b]);
    if (inspector_pane_) {
        inspector_pane_->set_page(workbench_slots_[focused_slot_index_].page);
    }
    resize_history_.clear();
    update_trace_active_cache();
    set_status_override(std::format("Swapped Column {} and Column {}", slot_a + 1, slot_b + 1));
    render(true);
}

void Tui::move_focused_column_left() {
    if (workbench_slots_.size() <= 1 || focused_slot_index_ == 0) return;
    const size_t prev = focused_slot_index_ - 1;
    std::swap(workbench_slots_[focused_slot_index_], workbench_slots_[prev]);
    focused_slot_index_ = prev;
    if (inspector_pane_) {
        inspector_pane_->set_page(workbench_slots_[focused_slot_index_].page);
    }
    resize_history_.clear();
    update_trace_active_cache();
    set_status_override(std::format("Moved Column to Position {}", focused_slot_index_ + 1));
    render(true);
}

void Tui::move_focused_column_right() {
    if (workbench_slots_.size() <= 1 || focused_slot_index_ + 1 >= workbench_slots_.size()) return;
    const size_t next = focused_slot_index_ + 1;
    std::swap(workbench_slots_[focused_slot_index_], workbench_slots_[next]);
    focused_slot_index_ = next;
    if (inspector_pane_) {
        inspector_pane_->set_page(workbench_slots_[focused_slot_index_].page);
    }
    resize_history_.clear();
    update_trace_active_cache();
    set_status_override(std::format("Moved Column to Position {}", focused_slot_index_ + 1));
    render(true);
}

auto Tui::add_workbench_column() -> bool {
    if (workbench_slots_.size() >= 4) {
        set_status_override("Maximum columns reached (4 columns max)");
        render(true);
        return false;
    }

    const size_t target_count = workbench_slots_.size() + 1;
    const int req_width = framework::min_width_for_columns(static_cast<uint8_t>(target_count));

    winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int const term_width =
        (cached_term_width_ > 0) ? cached_term_width_ : (w.ws_col > 0 ? w.ws_col : 80);

    if (term_width < req_width) {
        set_status_override(
            std::format("Terminal too narrow for {} columns (needs {} cols, current is {})",
                        target_count, req_width, term_width));
        render(true);
        return false;
    }

    const std::array<TuiRegPage, 7> candidates = {
        TuiRegPage::CONSOLE, TuiRegPage::STACK, TuiRegPage::TRACE, TuiRegPage::PIPELINE,
        TuiRegPage::DISASM,  TuiRegPage::CACHE, TuiRegPage::GPR};
    TuiRegPage new_page = TuiRegPage::CONSOLE;
    for (auto c : candidates) {
        bool already_used = false;
        for (const auto& slot : workbench_slots_) {
            if (slot.page == c) {
                already_used = true;
                break;
            }
        }
        if (!already_used) {
            new_page = c;
            break;
        }
    }

    size_t const insert_at = std::min(focused_slot_index_ + 1, workbench_slots_.size());
    workbench_slots_.insert(workbench_slots_.begin() + static_cast<std::ptrdiff_t>(insert_at),
                            {new_page, 0});
    if (target_count == 2) {
        layout_ = TuiLayout::Split;
    } else if (target_count == 3) {
        layout_ = TuiLayout::ThreeColumn;
    } else if (target_count == 4) {
        layout_ = TuiLayout::FourColumn;
    }
    user_inspector_width_ = -1;
    user_column_widths_ = framework::kNoColumnWidthOverrides;
    resize_history_.clear();

    focused_slot_index_ = insert_at;
    if (inspector_pane_ && new_page != TuiRegPage::CONSOLE) {
        inspector_pane_->set_page(new_page);
    }
    update_trace_active_cache();
    set_status_override(
        std::format("Added Column {}: {}", workbench_slots_.size(), get_page_name(new_page)));
    request_full_screen_redraw();
    render(true);
    return true;
}

auto Tui::close_column(size_t slot_idx) -> bool {
    if (workbench_slots_.size() <= 1) {
        set_status_override("Cannot close the last remaining column");
        render(true);
        return false;
    }
    if (slot_idx >= workbench_slots_.size()) {
        return false;
    }

    const auto closed_page = workbench_slots_[slot_idx].page;
    workbench_slots_.erase(workbench_slots_.begin() + slot_idx);

    const size_t rem = workbench_slots_.size();
    if (rem == 1) {
        layout_ = (workbench_slots_[0].page == TuiRegPage::CONSOLE) ? TuiLayout::FullRight
                                                                    : TuiLayout::FullLeft;
    } else if (rem == 2) {
        layout_ = TuiLayout::Split;
    } else if (rem == 3) {
        layout_ = TuiLayout::ThreeColumn;
    }
    user_inspector_width_ = -1;
    user_column_widths_ = framework::kNoColumnWidthOverrides;
    resize_history_.clear();

    if (focused_slot_index_ >= workbench_slots_.size()) {
        focused_slot_index_ = workbench_slots_.size() - 1;
    } else if (focused_slot_index_ > slot_idx) {
        focused_slot_index_--;
    }

    if (inspector_pane_ && workbench_slots_[focused_slot_index_].page != TuiRegPage::CONSOLE) {
        inspector_pane_->set_page(workbench_slots_[focused_slot_index_].page);
    }
    update_trace_active_cache();
    set_status_override(
        std::format("Closed Column {} ({})", slot_idx + 1, get_page_name(closed_page)));
    request_full_screen_redraw();
    render(true);
    return true;
}

auto Tui::close_focused_column() -> bool { return close_column(focused_slot_index_); }

auto Tui::focused_page() const -> TuiRegPage {
    if (focused_slot_index_ < workbench_slots_.size()) {
        return workbench_slots_[focused_slot_index_].page;
    }
    return inspector_pane_ ? inspector_pane_->get_page() : TuiRegPage::GPR;
}

void Tui::cycle_reg_page(bool reverse) {
    bool has_f = (machine_.primary_hart().state().misa & (1ULL << ('f' - 'a'))) != 0;
    bool has_d = (machine_.primary_hart().state().misa & (1ULL << ('d' - 'a'))) != 0;
    bool has_v = (machine_.primary_hart().state().misa & (1ULL << ('v' - 'a'))) != 0;
    TuiRegPage rp = focused_page();
    TuiCategoryGroup grp = get_category_group(rp);

    if (!reverse) {
        switch (grp) {
            case TuiCategoryGroup::Regs:
                switch (rp) {
                    case TuiRegPage::GPR:
                        if (has_f || has_d)
                            rp = TuiRegPage::FPR;
                        else if (has_v)
                            rp = TuiRegPage::VEC;
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
                break;
            case TuiCategoryGroup::Memory:
                switch (rp) {
                    case TuiRegPage::STACK:
                        rp = machine_.runtime_profile.is_cycle_mode() ? TuiRegPage::CACHE
                                                                      : TuiRegPage::TLB;
                        break;
                    case TuiRegPage::CACHE:
                        rp = TuiRegPage::TLB;
                        break;
                    case TuiRegPage::TLB:
                        rp = TuiRegPage::BUS;
                        break;
                    case TuiRegPage::BUS:
                    default:
                        rp = TuiRegPage::STACK;
                        break;
                }
                break;
            case TuiCategoryGroup::Pipeline:
                switch (rp) {
                    case TuiRegPage::PIPELINE:
                        rp = machine_.runtime_profile.is_cycle_mode() ? TuiRegPage::BPRED
                                                                      : TuiRegPage::PIPELINE;
                        break;
                    case TuiRegPage::BPRED:
                        rp = TuiRegPage::HAZARD;
                        break;
                    case TuiRegPage::HAZARD:
                    default:
                        rp = TuiRegPage::PIPELINE;
                        break;
                }
                break;
            case TuiCategoryGroup::Tools:
                if (rp == TuiRegPage::EXPLAIN)
                    rp = TuiRegPage::TRACE;
                else if (rp == TuiRegPage::TRACE)
                    rp = TuiRegPage::CONSOLE;
                else
                    rp = TuiRegPage::EXPLAIN;
                break;
        }
    } else {
        switch (grp) {
            case TuiCategoryGroup::Regs:
                switch (rp) {
                    case TuiRegPage::GPR:
                        if (has_v)
                            rp = TuiRegPage::VEC;
                        else if (has_f || has_d)
                            rp = TuiRegPage::FPR;
                        break;
                    case TuiRegPage::FPR:
                        rp = TuiRegPage::GPR;
                        break;
                    case TuiRegPage::VEC:
                        if (has_f || has_d)
                            rp = TuiRegPage::FPR;
                        else
                            rp = TuiRegPage::GPR;
                        break;
                    default:
                        rp = TuiRegPage::GPR;
                        break;
                }
                break;
            case TuiCategoryGroup::Memory:
                switch (rp) {
                    case TuiRegPage::STACK:
                        rp = TuiRegPage::BUS;
                        break;
                    case TuiRegPage::CACHE:
                        rp = TuiRegPage::STACK;
                        break;
                    case TuiRegPage::TLB:
                        rp = machine_.runtime_profile.is_cycle_mode() ? TuiRegPage::CACHE
                                                                      : TuiRegPage::STACK;
                        break;
                    case TuiRegPage::BUS:
                    default:
                        rp = TuiRegPage::TLB;
                        break;
                }
                break;
            case TuiCategoryGroup::Pipeline:
                switch (rp) {
                    case TuiRegPage::PIPELINE:
                        rp = machine_.runtime_profile.is_cycle_mode() ? TuiRegPage::HAZARD
                                                                      : TuiRegPage::PIPELINE;
                        break;
                    case TuiRegPage::BPRED:
                        rp = TuiRegPage::PIPELINE;
                        break;
                    case TuiRegPage::HAZARD:
                    default:
                        rp = TuiRegPage::BPRED;
                        break;
                }
                break;
            case TuiCategoryGroup::Tools:
                if (rp == TuiRegPage::EXPLAIN)
                    rp = TuiRegPage::CONSOLE;
                else if (rp == TuiRegPage::TRACE)
                    rp = TuiRegPage::EXPLAIN;
                else
                    rp = TuiRegPage::TRACE;
                break;
        }
    }

    set_reg_page(rp);
}

void Tui::cycle_tool_page(bool reverse) {
    TuiCategoryGroup const grp = get_category_group(focused_page());
    TuiCategoryGroup next_grp = TuiCategoryGroup::Regs;
    if (!reverse) {
        switch (grp) {
            case TuiCategoryGroup::Regs:
                next_grp = TuiCategoryGroup::Memory;
                break;
            case TuiCategoryGroup::Memory:
                next_grp = TuiCategoryGroup::Pipeline;
                break;
            case TuiCategoryGroup::Pipeline:
                next_grp = TuiCategoryGroup::Tools;
                break;
            case TuiCategoryGroup::Tools:
            default:
                next_grp = TuiCategoryGroup::Regs;
                break;
        }
    } else {
        switch (grp) {
            case TuiCategoryGroup::Regs:
                next_grp = TuiCategoryGroup::Tools;
                break;
            case TuiCategoryGroup::Memory:
                next_grp = TuiCategoryGroup::Regs;
                break;
            case TuiCategoryGroup::Pipeline:
                next_grp = TuiCategoryGroup::Memory;
                break;
            case TuiCategoryGroup::Tools:
            default:
                next_grp = TuiCategoryGroup::Pipeline;
                break;
        }
    }
    set_reg_page(get_default_page_for_group(next_grp, machine_.runtime_profile.is_cycle_mode()));
}

void Tui::set_reg_page(TuiRegPage page) {
    if (!machine_.runtime_profile.is_cycle_mode() &&
        (page == TuiRegPage::CACHE || page == TuiRegPage::BPRED || page == TuiRegPage::HAZARD)) {
        set_status_override(
            "CA Inspector Page disabled in Functional Mode (Enable Cycle-Accurate mode "
            "\033[1m[,]\033[22m or --mode cycle-accurate)");
        page = TuiRegPage::TLB;
    }
    if (focused_slot_index_ < workbench_slots_.size()) {
        workbench_slots_[focused_slot_index_].page = page;
    }
    if (inspector_pane_) {
        inspector_pane_->set_page(page);
        update_trace_active_cache();
        render(true);
    }
}

void Tui::toggle_explain() {
    if (inspector_pane_) {
        if (inspector_pane_->get_page() == TuiRegPage::EXPLAIN) {
            inspector_pane_->set_page(TuiRegPage::GPR);
        } else {
            inspector_pane_->set_page(TuiRegPage::EXPLAIN);
        }
        update_trace_active_cache();
        render(true);
    }
}

void Tui::export_inspection_report() {
    if (!is_paused()) {
        modal_.open_notice("PAUSE REQUIRED",
                           "Pause the simulator before exporting inspection state.", true);
        render(true);
        return;
    }
    const std::string& destination = machine_.configuration().tui.inspection_output;
    if (destination.empty()) {
        modal_.open_notice("OUTPUT PATH REQUIRED",
                           "Restart with --inspection-output <FILE>, then press [x] while paused.",
                           true);
        render(true);
        return;
    }

    drain_trace_records();
    const std::string report = make_inspection_report(machine_, selected_hart_, trace_buffer_);
    const auto result = write_inspection_report(destination, report, inspection_overwrite_armed_);
    if (!result) {
        inspection_overwrite_armed_ = false;
        modal_.open_notice("EXPORT FAILED", result.error(), true);
    } else if (*result == InspectionReportWriteStatus::WouldOverwrite) {
        inspection_overwrite_armed_ = true;
        modal_.open_notice(
            "CONFIRM OVERWRITE",
            std::format("{} already exists. Close this dialog and press [x] again to replace it.",
                        destination),
            false);
    } else {
        inspection_overwrite_armed_ = false;
        modal_.open_notice(
            "INSPECTION EXPORTED",
            std::format("Wrote selected Hart {} state to {}.", selected_hart_, destination), false);
    }
    render(true);
}

void Tui::toggle_run_state() {
    if (paused_.load(std::memory_order_relaxed))
        unpause_loop();
    else
        pause_loop();
}

void Tui::toggle_execution_mode() {
    const bool was_cycle = machine_.runtime_profile.is_cycle_mode();
    const auto target_engine = was_cycle ? simrv::core::ExecutionEngine::InstructionObservable
                                         : simrv::core::ExecutionEngine::CycleObservable;
    machine_.switch_execution_engine(target_engine);
    set_status_override(was_cycle ? "Mode: Instruction-Accurate (IA)"
                                  : "Mode: Cycle-Accurate (CA)");
    render(true);
}

namespace {
constexpr auto ascii_to_linux_keycode_and_shift(uint8_t byte) -> std::pair<uint16_t, bool> {
    if (byte >= 'a' && byte <= 'z') {
        constexpr uint16_t kAlpha[26] = {
            30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,  // a-m
            49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44   // n-z
        };
        return {kAlpha[byte - 'a'], false};
    }
    if (byte >= 'A' && byte <= 'Z') {
        constexpr uint16_t kAlpha[26] = {
            30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,  // A-M
            49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44   // N-Z
        };
        return {kAlpha[byte - 'A'], true};
    }
    if (byte >= '1' && byte <= '9') {
        return {static_cast<uint16_t>(2 + (byte - '1')), false};
    }
    if (byte == '0') return {11, false};

    switch (byte) {
        case '!':
            return {2, true};
        case '@':
            return {3, true};
        case '#':
            return {4, true};
        case '$':
            return {5, true};
        case '%':
            return {6, true};
        case '^':
            return {7, true};
        case '&':
            return {8, true};
        case '*':
            return {9, true};
        case '(':
            return {10, true};
        case ')':
            return {11, true};
        case '\n':
        case '\r':
            return {28, false};  // KEY_ENTER
        case '\t':
            return {15, false};  // KEY_TAB
        case ' ':
            return {57, false};  // KEY_SPACE
        case '\b':
        case 127:
            return {14, false};  // KEY_BACKSPACE
        case 27:
            return {1, false};  // KEY_ESC
        case '-':
            return {12, false};  // KEY_MINUS
        case '_':
            return {12, true};
        case '=':
            return {13, false};  // KEY_EQUAL
        case '+':
            return {13, true};
        case '[':
            return {26, false};  // KEY_LEFTBRACE
        case '{':
            return {26, true};
        case ']':
            return {27, false};  // KEY_RIGHTBRACE
        case '}':
            return {27, true};
        case ';':
            return {39, false};  // KEY_SEMICOLON
        case ':':
            return {39, true};
        case '\'':
            return {40, false};  // KEY_APOSTROPHE
        case '"':
            return {40, true};
        case '`':
            return {41, false};  // KEY_GRAVE
        case '~':
            return {41, true};
        case '\\':
            return {43, false};  // KEY_BACKSLASH
        case '|':
            return {43, true};
        case ',':
            return {51, false};  // KEY_COMMA
        case '<':
            return {51, true};
        case '.':
            return {52, false};  // KEY_DOT
        case '>':
            return {52, true};
        case '/':
            return {53, false};  // KEY_SLASH
        case '?':
            return {53, true};
        default:
            return {0, false};
    }
}
}  // namespace

void Tui::write_guest_input(uint8_t byte) {
    const auto page = focused_page();
    if (page == TuiRegPage::CONSOLE) {
        if (machine_.uart_device()) {
            machine_.uart_device()->push_rx_byte(normalize_guest_terminal_byte(byte));
        }
        return;
    }
    if (page != TuiRegPage::DISPLAY) return;

    if (byte >= 1 && byte <= 26) {
        const auto [keycode, unused_shift] =
            ascii_to_linux_keycode_and_shift(static_cast<uint8_t>('a' + byte - 1));
        (void)unused_shift;
        if (keycode != 0) {
            machine_.send_input_key(29 /* KEY_LEFTCTRL */, true);
            machine_.send_input_key(keycode, true);
            machine_.send_input_key(keycode, false);
            machine_.send_input_key(29 /* KEY_LEFTCTRL */, false);
        }
        return;
    }

    {
        const auto [keycode, is_shift] = ascii_to_linux_keycode_and_shift(byte);
        if (keycode != 0) {
            if (is_shift) {
                machine_.send_input_key(42 /* KEY_LEFTSHIFT */, true);
            }
            machine_.send_input_key(keycode, true);
            machine_.send_input_key(keycode, false);
            if (is_shift) {
                machine_.send_input_key(42 /* KEY_LEFTSHIFT */, false);
            }
        }
    }
}

auto Tui::is_page_visible(TuiRegPage page) const noexcept -> bool {
    if (!workbench_slots_.empty()) {
        for (const auto& slot : workbench_slots_) {
            if (slot.page == page) return true;
        }
        return false;
    }
    return inspector_pane_ && inspector_pane_->get_page() == page;
}

void Tui::update_trace_active_cache() {
    const bool trace_visible = is_page_visible(TuiRegPage::TRACE);
    trace_or_livetrace_active_.store(trace_visible, std::memory_order_release);

    const bool detail_visible =
        trace_visible || is_page_visible(TuiRegPage::PIPELINE) ||
        is_page_visible(TuiRegPage::HAZARD) || is_page_visible(TuiRegPage::BPRED) ||
        is_page_visible(TuiRegPage::DISASM) || is_page_visible(TuiRegPage::EXPLAIN);
    pipeline_or_detail_visible_.store(detail_visible, std::memory_order_release);
}

void Tui::record_instruction(Register pc, simrv::isa::Opcode opcode, simrv::isa::OperationId op_id,
                             uint8_t rd, Register rd_val, uint8_t rs1, Register rs1_val,
                             uint8_t rs2, Register rs2_val, int64_t imm, uint8_t hart) {
    if (mission_.enabled()) {
        if (const auto symbol = machine_.symbol_table().lookup_symbol(pc);
            symbol && symbol->is_exact()) {
            mission_.observe_symbol(symbol->name);
        }
    }
    if (!trace_or_livetrace_active_.load(std::memory_order_relaxed)) {
        return;
    }
    auto& ring = flight_rings_.at(hart % kFlightRecorderHarts);
    const uint64_t slot = ring.write_sequence.load(std::memory_order_relaxed);
    const uint64_t seq = flight_sequence_.fetch_add(1, std::memory_order_relaxed);
    ring.records[slot % kTraceBufferSize] = TraceRecord{.pc = pc,
                                                        .opcode = opcode,
                                                        .op_id = op_id,
                                                        .rd = rd,
                                                        .rd_val = rd_val,
                                                        .rs1 = rs1,
                                                        .rs1_val = rs1_val,
                                                        .rs2 = rs2,
                                                        .rs2_val = rs2_val,
                                                        .imm = imm,
                                                        .sequence = seq,
                                                        .hart = hart,
                                                        .detailed = true};
    ring.write_sequence.store(slot + 1, std::memory_order_release);
}

void Tui::record_flight_instruction(Register pc, simrv::isa::Opcode opcode,
                                    simrv::isa::OperationId op_id, uint8_t hart) {
    auto& ring = flight_rings_.at(hart % kFlightRecorderHarts);
    const uint64_t slot = ring.write_sequence.load(std::memory_order_relaxed);
    const uint64_t seq = flight_sequence_.fetch_add(1, std::memory_order_relaxed);
    ring.records[slot % kTraceBufferSize] =
        TraceRecord{.pc = pc, .opcode = opcode, .op_id = op_id, .sequence = seq, .hart = hart};
    ring.write_sequence.store(slot + 1, std::memory_order_release);
}

void Tui::drain_trace_records() {
    std::vector<TraceRecord> pending;
    for (size_t hart = 0; hart < kFlightRecorderHarts; ++hart) {
        auto const current = flight_rings_[hart].write_sequence.load(std::memory_order_acquire);
        auto& rendered = rendered_flight_sequences_[hart];
        rendered = std::max(rendered, current > kTraceBufferSize ? current - kTraceBufferSize : 0);
        for (; rendered < current; ++rendered) {
            pending.push_back(flight_rings_[hart].records[rendered % kTraceBufferSize]);
        }
    }
    std::ranges::sort(pending, {}, &TraceRecord::sequence);
    for (const auto& record : pending) {
        trace_buffer_.push_back(format_trace_record(record));
    }
    if (trace_buffer_.size() > kTraceBufferSize) {
        trace_buffer_.erase(trace_buffer_.begin(),
                            trace_buffer_.begin() + static_cast<std::ptrdiff_t>(
                                                        trace_buffer_.size() - kTraceBufferSize));
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
    for (char c : simrv::pipeline::operation_name(rec.op_id)) {
        op_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    for (char& c : op_name) {
        if (c == '_') c = '.';
    }

    if (!rec.detailed) {
        return std::format("{:#x} [H{}] {}", rec.pc, rec.hart, op_name);
    }

    const bool rd_fp = isa::is_destination_fp(rec.opcode, rec.op_id);
    const bool rs1_fp = isa::is_rs1_fp(rec.opcode, rec.op_id);
    const bool rs2_fp = isa::is_rs2_fp(rec.opcode, rec.op_id);

    std::string inst_str;
    std::string side_effect;
    format_trace_inst(rec, op_name, rd_fp, rs1_fp, rs2_fp, inst_str, side_effect);

    std::string sym = machine_.symbol_table().lookup(rec.pc);
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
    frame_dirty_ = true;
    render(true);
}

void Tui::reset_scroll() {
    scroll_offset_ = 0;
    render();
    frame_dirty_ = true;
    render(true);
}

void Tui::scroll_inspector(int lines) {
    if (inspector_pane_) {
        auto const widths = column_widths(cached_term_width_ > 0 ? cached_term_width_ : 80);
        auto const focused =
            std::min(focused_slot_index_, static_cast<size_t>(std::max<int>(1, widths.count) - 1));
        int const pane_width = widths.count > 0 ? widths.widths[focused] : 0;
        inspector_pane_->set_page(focused_page());
        inspector_pane_->scroll(lines, pane_width, focused > 0);
        render();
        frame_dirty_ = true;
        render(true);
    }
}

void Tui::reset_scroll_inspector() {
    if (inspector_pane_) {
        inspector_pane_->set_page(focused_page());
        inspector_pane_->reset_scroll();
        render();
        frame_dirty_ = true;
        render(true);
    }
}

void Tui::update_cache() {
    if (inspector_pane_) {
        inspector_pane_->update_cache();
    }
}

void Tui::reset_speed_history() {
    last_speed_update_ = std::chrono::steady_clock::now();
    last_icount_ = machine_.primary_hart().e_icount;
    speed_ips_ = 0;
    kips_ = 0;
    max_kips_ = 0;
    kips_history_.clear();
}

auto Tui::column_widths(int terminal_width) const -> framework::ColumnWidths {
    return framework::multi_column_widths(terminal_width, layout_, user_inspector_width_,
                                          user_column_widths_);
}

void Tui::adjust_inspector_width(int delta) {
    struct winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);  // NOLINT(cppcoreguidelines-pro-type-vararg)
    int const term_width = (cached_term_width_ > 0) ? cached_term_width_ : w.ws_col;
    if (term_width <= 0) return;

    auto current = column_widths(term_width);
    if (current.count < 2 || layout_ == TuiLayout::FullLeft || layout_ == TuiLayout::FullRight)
        return;

    size_t const focused = std::min(focused_slot_index_, static_cast<size_t>(current.count - 1));
    int const minimum =
        (current.count == 2) ? framework::kBaseColumnUnitWidth : framework::kMultiColumnUnitWidth;
    int const proposed = current.widths[focused] + delta;
    if (proposed < minimum) return;

    // Reversing the immediately preceding resize restores the exact donor distribution.  This
    // matters when an expansion crossed several minimum-width panels; a fresh redistribution
    // would otherwise leave the columns shifted after the user presses the opposite key.
    if (!resize_history_.empty()) {
        auto const& previous = resize_history_.back();
        if (previous.focused == focused && previous.count == current.count &&
            previous.delta == -delta) {
            for (size_t i = 0; i < current.count; ++i) {
                current.widths[i] -= previous.changes[i];
            }
            resize_history_.pop_back();
            for (size_t i = 0; i < current.count; ++i) user_column_widths_[i] = current.widths[i];
            if (current.count == 2) user_inspector_width_ = current.widths[0];
            request_full_screen_redraw();
            render(true);
            return;
        }
    }

    // Keep the adjacent divider moving first, then continue through neighbouring columns when
    // one reaches its minimum.  This makes [/] useful in three- and four-panel layouts instead
    // of silently refusing a resize as soon as the immediate donor is exhausted.
    std::array<int, 4> donor_order{};
    size_t donor_count = 0;
    if (focused + 1 < current.count) {
        for (size_t i = focused + 1; i < current.count; ++i) donor_order[donor_count++] = i;
        for (size_t i = focused; i-- > 0;) donor_order[donor_count++] = i;
    } else {
        for (size_t i = focused; i-- > 0;) donor_order[donor_count++] = i;
    }

    int const need = std::max(0, delta);
    int available = 0;
    for (size_t i = 0; i < donor_count; ++i) {
        available += std::max(0, current.widths[donor_order[i]] - minimum);
    }
    if (need > available) return;

    current.widths[focused] = proposed;
    ColumnResizeRecord record{.focused = focused, .count = current.count, .delta = delta};
    record.changes[focused] = delta;
    int remaining = need;
    if (delta < 0 && donor_count > 0) {
        // Shrinking the focused panel gives its space to the nearest neighbour, preserving the
        // intuitive single-divider behaviour at the edge of a layout.
        current.widths[donor_order[0]] -= delta;
        record.changes[donor_order[0]] = -delta;
    } else {
        for (size_t i = 0; i < donor_count && remaining > 0; ++i) {
            int const donor = donor_order[i];
            int const take = std::min(remaining, std::max(0, current.widths[donor] - minimum));
            current.widths[donor] -= take;
            record.changes[donor] = -take;
            remaining -= take;
        }
    }
    resize_history_.push_back(record);
    for (size_t i = 0; i < current.count; ++i) user_column_widths_[i] = current.widths[i];
    if (current.count == 2) user_inspector_width_ = current.widths[0];
    request_full_screen_redraw();
    render(true);
}

auto Tui::poll_keyboard(uint8_t& byte_out) -> bool {
    if (input_pos_ == input_size_) {
        pollfd fd{STDIN_FILENO, POLLIN, 0};
        if (::poll(&fd, 1, 0) <= 0 || !(fd.revents & POLLIN)) return false;
        const auto count = ::read(STDIN_FILENO, input_bytes_.data(), input_bytes_.size());
        if (count <= 0) return false;
        input_pos_ = 0;
        input_size_ = static_cast<size_t>(count);
    }
    byte_out = input_bytes_[input_pos_++];
    return true;
}

void Tui::update() {
    uint8_t byte = 0;
    while (poll_keyboard(byte)) {
        const InputContext context{
            .modal_active = is_modal_active(),
            .paused = paused_.load(std::memory_order_relaxed),
        };
        const InputRoute route = route_input(byte, context);
        const auto key = static_cast<simrv::tui::TuiKey>(byte);
        switch (route) {
            case InputRoute::ControlSequence:
                if (!consume_control_sequence(byte)) {
                    (void)handle_modal_keyboard_input(byte, key);
                }
                break;
            case InputRoute::Modal:
                (void)handle_modal_keyboard_input(byte, key);
                break;
            case InputRoute::Navigation:
                handle_normal_keyboard_input(byte, key);
                break;
            case InputRoute::Pause:
                pause_loop();
                return;
            case InputRoute::Reboot:
                machine_.request_reboot();
                return;
            case InputRoute::Quit:
                machine_.request_exit();
                return;
            case InputRoute::Guest:
                // The integrated terminal is an attached UART endpoint. The external PTY slave
                // is another endpoint for the same UART and remains available to independent
                // tools.
                write_guest_input(byte);
                break;
        }
    }
}

auto Tui::handle_modal_settings(ModalType mtype, uint8_t byte, TuiKey key) -> bool {
    if (mtype == ModalType::Settings) {
        if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q') {
            close_modal();
        } else if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) {
            if (modal_.get_settings_draft().active_tab == 2 &&
                modal_.get_settings_draft().tab_cursor[2] == 13) {
                modal_.open_save_cpu_config();
                render(true);
                return true;
            }
            if (modal_.get_settings_draft().active_tab == 2 &&
                modal_.get_settings_draft().tab_cursor[2] == 14) {
                modal_.open_load_cpu_config();
                render(true);
                return true;
            }
            submit_modal();
        } else if (key == simrv::tui::TuiKey::Tab) {
            modal_.cycle_settings_tab(1);
            render(true);
        } else if (key == simrv::tui::TuiKey::BackTab) {
            modal_.cycle_settings_tab(-1);
            render(true);
        } else if (byte == '1') {
            modal_.set_settings_tab(0);
            render(true);
        } else if (byte == '2') {
            modal_.set_settings_tab(1);
            render(true);
        } else if (byte == '3') {
            modal_.set_settings_tab(2);
            render(true);
        } else if (byte == ' ') {
            if (modal_.get_settings_draft().active_tab == 2 &&
                modal_.get_settings_draft().tab_cursor[2] == 13) {
                modal_.open_save_cpu_config();
                render(true);
                return true;
            }
            if (modal_.get_settings_draft().active_tab == 2 &&
                modal_.get_settings_draft().tab_cursor[2] == 14) {
                modal_.open_load_cpu_config();
                render(true);
                return true;
            }
            modal_.toggle_setting_at_cursor();
            render(true);
        } else if ((byte == 's' || byte == 'S') && modal_.get_settings_draft().active_tab == 2) {
            modal_.open_save_cpu_config();
            render(true);
            return true;
        } else if ((byte == 'l' || byte == 'L') && modal_.get_settings_draft().active_tab == 2) {
            modal_.open_load_cpu_config();
            render(true);
            return true;
        } else if (byte == 'p' || byte == 'P') {
            modal_.apply_settings_isa_preset(0);
            render(true);
        } else if (byte == 'i' || byte == 'I') {
            modal_.apply_settings_isa_preset(1);
            render(true);
        } else if (byte == 'g' || byte == 'G') {
            modal_.apply_settings_isa_preset(2);
            render(true);
        } else if (byte >= '0' && byte <= '9') {
            modal_.push_settings_digit(static_cast<char>(byte));
            render(true);
        } else if (byte == 8 || byte == 127 || key == simrv::tui::TuiKey::Backspace) {
            modal_.pop_settings_digit();
            render(true);
        }
        return true;
    }
    return false;
}

auto Tui::handle_modal_breakpoint(ModalType mtype, uint8_t byte, TuiKey key) -> bool {
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
            machine_.breakpoint_manager().clear_pc_breakpoints();
            machine_.breakpoint_manager().clear_watchpoints();
            modal_.open_notice("BREAKPOINTS CLEARED", "Cleared all breakpoints and watchpoints.",
                               false);
            render(true);
        } else if (key == simrv::tui::TuiKey::Tab || byte == 'j' || byte == 'J') {
            modal_.move_bp_cursor(1);
            render(true);
        } else if (key == simrv::tui::TuiKey::BackTab || byte == 'k' || byte == 'K') {
            modal_.move_bp_cursor(-1);
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
    if (handle_modal_settings(mtype, byte, key) || handle_modal_breakpoint(mtype, byte, key)) {
        return true;
    }
    if (mtype == ModalType::Notice) {
        if (byte == 'q' || byte == 'Q') {
            close_modal();
            if (machine_.is_shutdown_) {
                machine_.request_exit();
            }
            return true;
        }
        if (key == simrv::tui::TuiKey::CtrlR || byte == 0x12) {
            close_modal();
            machine_.request_reboot();
            return true;
        }
        if (byte == 'o' || byte == 'O') {
            close_modal();
            open_modal(ModalType::LoadBinary);
            return true;
        }
        if (byte == 27 || key == simrv::tui::TuiKey::Esc || key == simrv::tui::TuiKey::Enter ||
            key == simrv::tui::TuiKey::Newline || byte == ' ') {
            close_modal();
            return true;
        }
        return true;
    }
    if (mtype == ModalType::Glossary) {
        if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q' ||
            key == simrv::tui::TuiKey::QuestionMark) {
            close_modal();
            return true;
        }
        if (key == simrv::tui::TuiKey::Tab || byte == 'l' || byte == 'L') {
            modal_.move_glossary_topic(1);
            render(true);
            return true;
        }
        if (key == simrv::tui::TuiKey::BackTab || byte == 'h' || byte == 'H') {
            modal_.move_glossary_topic(-1);
            render(true);
            return true;
        }
        if (byte >= '1' && byte <= '6') {
            modal_.set_glossary_topic(byte - '1');
            render(true);
            return true;
        }
        if (byte == ' ' || key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) {
            modal_.move_glossary_topic(1);
            render(true);
            return true;
        }
        return true;
    }
    if (mtype == ModalType::Help) {
        if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q' ||
            key == simrv::tui::TuiKey::QuestionMark || key == simrv::tui::TuiKey::F1) {
            close_modal();
            return true;
        }
        if (byte == 'j' || byte == 'J' || key == simrv::tui::TuiKey::Tab) {
            modal_.scroll_help(1);
            render(true);
            return true;
        }
        if (byte == 'k' || byte == 'K' || key == simrv::tui::TuiKey::BackTab) {
            modal_.scroll_help(-1);
            render(true);
            return true;
        }
        if (byte == ' ' || key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) {
            modal_.scroll_help(5);
            render(true);
            return true;
        }
        return true;
    }
    if (mtype == ModalType::PlatformChangeConfirm) {
        if (byte == 'r' || byte == 'R' || key == simrv::tui::TuiKey::Enter ||
            key == simrv::tui::TuiKey::Newline) {
            const auto& draft = modal_.get_pending_platform_draft();
            auto next = machine_.configuration();
            next.platform_profile =
                static_cast<simrv::core::PlatformProfile>(draft.platform_profile);
            next.network.mode = draft.net_mode;
            (void)machine_.stage_reconfiguration(std::move(next));
            close_modal();
            modal_.open_notice(
                "SIMULATOR RELOADED",
                "Platform profile applied and simulation restarted with new Device Tree.", false);
            render(true);
            return true;
        }
        if (byte == 'd' || byte == 'D' || byte == ' ') {
            close_modal();
            modal_.open_notice(
                "PLATFORM CHANGE DISCARDED",
                "Platform profile change was discarded and previous topology retained.", false);
            render(true);
            return true;
        }
        if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q') {
            open_modal(ModalType::Settings);
            render(true);
            return true;
        }
        return true;
    }
    if (mtype == ModalType::LayoutPresets) {
        if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q' ||
            key == simrv::tui::TuiKey::F4) {
            close_modal();
            return true;
        }
        if (byte >= '1' && byte <= '4') {
            modal_.set_preset_cursor(byte - '1');
            apply_layout_preset(modal_.get_selected_preset());
            close_modal();
            return true;
        }
        if (key == simrv::tui::TuiKey::Tab || byte == 'j' || byte == 'J') {
            modal_.move_preset_cursor(1);
            render(true);
            return true;
        }
        if (key == simrv::tui::TuiKey::BackTab || byte == 'k' || byte == 'K') {
            modal_.move_preset_cursor(-1);
            render(true);
            return true;
        }
        if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline || byte == ' ') {
            apply_layout_preset(modal_.get_selected_preset());
            close_modal();
            return true;
        }
        return true;
    }
    if (mtype == ModalType::ToolPicker) {
        if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q') {
            close_modal();
            return true;
        }
        if (key == simrv::tui::TuiKey::Tab) {
            modal_.cycle_tool_picker_slot(static_cast<int>(workbench_slots_.size()), 1);
            render(true);
            return true;
        }
        if (key == simrv::tui::TuiKey::BackTab) {
            modal_.cycle_tool_picker_slot(static_cast<int>(workbench_slots_.size()), -1);
            render(true);
            return true;
        }
        if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline || byte == ' ') {
            submit_modal();
            return true;
        }
        char acc = static_cast<char>(std::tolower(static_cast<unsigned char>(byte)));
        if (key == simrv::tui::TuiKey::y || key == simrv::tui::TuiKey::Y) acc = 'y';
        auto found_page = modals::ToolPickerModal::find_by_accelerator(acc);
        if (found_page.has_value()) {
            const auto& tools = modals::ToolPickerModal::all_tools();
            for (size_t i = 0; i < tools.size(); ++i) {
                if (tools[i].page == *found_page) {
                    modal_.set_tool_picker_cursor(static_cast<int>(i));
                    break;
                }
            }
            submit_modal();
            return true;
        }
        return true;
    }

    if (key == simrv::tui::TuiKey::F10) {
        machine_.request_exit();
        close_modal();
        return true;
    }

    if (byte == 27 || key == simrv::tui::TuiKey::Esc) {
        close_modal();
    } else if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline)
        submit_modal();
    else if (get_active_modal() == ModalType::LoadBinary &&
             (byte == 9 || key == simrv::tui::TuiKey::Tab || key == simrv::tui::TuiKey::BackTab)) {
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
        case simrv::tui::TuiKey::k:
        case simrv::tui::TuiKey::K: {
            Address pc = machine_.primary_hart().state().pc;
            if (machine_.breakpoint_manager().has_pc_breakpoint(pc)) {
                machine_.breakpoint_manager().remove_pc_breakpoint(pc);
                modal_.open_notice("BREAKPOINT REMOVED",
                                   std::format("Removed PC breakpoint at 0x{:08x}", pc), false);
            } else {
                machine_.breakpoint_manager().add_pc_breakpoint(pc);
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
            focus_next_slot();
            return true;
        case simrv::tui::TuiKey::BackTab:
            focus_prev_slot();
            return true;
        case simrv::tui::TuiKey::CtrlA:
        case simrv::tui::TuiKey::CtrlN:
            add_workbench_column();
            return true;
        case simrv::tui::TuiKey::CtrlX:
            close_focused_column();
            return true;
        case simrv::tui::TuiKey::F1:
            if (get_active_modal() == ModalType::Help)
                close_modal();
            else
                open_modal(ModalType::Help);
            return true;
        case simrv::tui::TuiKey::F2:
            if (get_active_modal() == ModalType::Settings)
                close_modal();
            else
                open_modal(ModalType::Settings);
            return true;
        case simrv::tui::TuiKey::F3:
            cycle_reg_page();
            return true;
        case simrv::tui::TuiKey::F4:
            if (get_active_modal() == ModalType::LayoutPresets)
                close_modal();
            else
                open_modal(ModalType::LayoutPresets);
            return true;
        case simrv::tui::TuiKey::F5:
            toggle_run_state();
            return true;
        case simrv::tui::TuiKey::F6:
            execute_footer_action(TuiFooterAction::Step);
            return true;
        case simrv::tui::TuiKey::F7:
            set_reg_page(TuiRegPage::TRACE);
            return true;
        case simrv::tui::TuiKey::F8:
            execute_footer_action(TuiFooterAction::TogglePcBreakpoint);
            return true;
        case simrv::tui::TuiKey::F9:
            if (get_active_modal() == ModalType::Glossary)
                close_modal();
            else
                open_modal(ModalType::Glossary);
            return true;
        case simrv::tui::TuiKey::F10:
            machine_.request_exit();
            return true;
        case simrv::tui::TuiKey::F11:
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
            if (inspector_pane_) {
                modal_.set_glossary_topic(
                    guidance_for_page(inspector_pane_->get_page(),
                                      machine_.runtime_profile.is_cycle_mode())
                        .glossary_topic);
            }
            open_modal(ModalType::Glossary);
            return true;
        case simrv::tui::TuiKey::h:
        case simrv::tui::TuiKey::H:
            open_modal(ModalType::Help);
            return true;
        case simrv::tui::TuiKey::LeftBracket:
            adjust_inspector_width(-2);
            return true;
        case simrv::tui::TuiKey::RightBracket:
            adjust_inspector_width(2);
            return true;
        case simrv::tui::TuiKey::t:
        case simrv::tui::TuiKey::T:
            cycle_theme_style();
            render(true);
            return true;
        case simrv::tui::TuiKey::v:
        case simrv::tui::TuiKey::V:
            toggle_execution_mode();
            return true;
        case simrv::tui::TuiKey::g:
        case simrv::tui::TuiKey::G:
            toggle_student_guide();
            return true;
        case simrv::tui::TuiKey::x:
        case simrv::tui::TuiKey::X:
            export_inspection_report();
            return true;
        case simrv::tui::TuiKey::u:
        case simrv::tui::TuiKey::U:
            if (focused_page() == TuiRegPage::CONSOLE) {
                scroll(5);
            } else if (inspector_pane_) {
                inspector_pane_->scroll_log(2);
                render(true);
            }
            return true;
        case simrv::tui::TuiKey::d:
        case simrv::tui::TuiKey::D:
            if (focused_page() == TuiRegPage::CONSOLE) {
                scroll(-5);
            } else if (inspector_pane_) {
                inspector_pane_->scroll_log(-2);
                render(true);
            }
            return true;
        case simrv::tui::TuiKey::o:
        case simrv::tui::TuiKey::O:
            open_modal(ModalType::LoadBinary);
            return true;
        case simrv::tui::TuiKey::n:
        case simrv::tui::TuiKey::N:
            select_next_hart();
            return true;
        case simrv::tui::TuiKey::CtrlW:
            open_tool_picker(focused_slot_index_);
            return true;
        case simrv::tui::TuiKey::Less:
            move_focused_column_left();
            return true;
        case simrv::tui::TuiKey::Greater:
            move_focused_column_right();
            return true;
        default:
            if (byte == '<') {
                move_focused_column_left();
                return true;
            }
            if (byte == '>') {
                move_focused_column_right();
                return true;
            }
            if (byte == 0x17) {
                open_tool_picker(focused_slot_index_);
                return true;
            }
            if (byte == 0x01 || byte == 0x0e) {
                add_workbench_column();
                return true;
            }
            if (byte == 0x18) {
                close_focused_column();
                return true;
            }
            if (byte == '1') {
                set_reg_page(TuiRegPage::GPR);
                return true;
            }
            if (byte == '2') {
                set_reg_page(TuiRegPage::STACK);
                return true;
            }
            if (byte == '3') {
                set_reg_page(TuiRegPage::PIPELINE);
                return true;
            }
            if (byte == '4') {
                set_reg_page(TuiRegPage::EXPLAIN);
                return true;
            }
            if (byte == ',' || key == simrv::tui::TuiKey::Comma) {
                open_modal(ModalType::Settings);
                return true;
            }
            return false;
    }
}

auto Tui::handle_normal_keyboard_input(uint8_t byte, TuiKey key) -> void {
    if (selection_.is_active && (byte == 3 || byte == 'y')) {
        copy_active_selection();
        clear_selection();
        return;
    }

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
        if (machine_.binary_path().empty() && machine_.primary_hart().state().pc == 0) {
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
    if (key == simrv::tui::TuiKey::CtrlR) {
        machine_.request_reboot();
        return;
    }
    if (byte == 'z') {
        dismiss_mission();
        return;
    }
    if (byte == 'Z') {
        restart_mission();
        return;
    }
    if ((key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) &&
        student_guide_enabled_ && is_paused()) {
        activate_student_guide_suggestion();
        return;
    }
    if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) {
        reset_scroll();
        return;
    }
    if (key == simrv::tui::TuiKey::CtrlC && focused_page() == TuiRegPage::DISPLAY) {
        write_guest_input(3);
        return;
    }
    if (key == simrv::tui::TuiKey::CtrlQ || key == simrv::tui::TuiKey::CtrlC ||
        key == simrv::tui::TuiKey::q || key == simrv::tui::TuiKey::Q) {
        machine_.request_exit();
        return;
    }

    if (handle_navigation_keyboard_input(byte, key) || handle_speed_keyboard_input(key)) return;

    // Memory inspection is an architectural teaching/observation tool, not a debug-diagnostics
    // feature. Keep its input semantics aligned with the canonical keybinding registry.
    if (key == simrv::tui::TuiKey::i || key == simrv::tui::TuiKey::I) {
        open_modal(ModalType::InspectAddress);
        return;
    }

    if (key == simrv::tui::TuiKey::Colon || key == simrv::tui::TuiKey::w ||
        key == simrv::tui::TuiKey::W || key == simrv::tui::TuiKey::m ||
        key == simrv::tui::TuiKey::M || key == simrv::tui::TuiKey::k ||
        key == simrv::tui::TuiKey::K) {
        if (handle_debug_keyboard_input(key)) return;
    }

    if (key == simrv::tui::TuiKey::s || key == simrv::tui::TuiKey::S) {
        if (machine_.binary_path().empty() && machine_.primary_hart().state().pc == 0) {
            modal_.open_notice(
                "NO PROGRAM LOADED",
                "Cannot step: PC is 0x0.\n\nPlease load a program binary image first [o].", false);
            render(true);
            return;
        }
        if (machine_.is_shutdown_) {
            modal_.open_notice("SYSTEM SHUTDOWN",
                               "Target system has shutdown.\n\nPlease reboot [Ctrl-R], load a "
                               "binary [o], or quit [q].",
                               false);
            render(true);
            return;
        } else {
            if (!is_paused()) pause_loop();
            machine_.step_sync();
            update_cache();
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

void Tui::toggle_student_guide() {
    student_guide_enabled_ = !student_guide_enabled_;
    set_status_override(
        std::format("Student Guide {}", student_guide_enabled_ ? "enabled" : "hidden"));
    render(true);
}

void Tui::dismiss_mission() {
    mission_.dismiss();
    set_status_override("Mission dismissed; the Student Guide remains available.");
    render(true);
}

void Tui::restart_mission() {
    mission_.restart();
    set_status_override("Mission restarted at objective 1.");
    render(true);
}

void Tui::activate_student_guide_suggestion() {
    if (!student_guide_enabled_ || !is_paused() || !inspector_pane_) return;

    if (const auto mission_guidance = mission_.guidance()) {
        set_reg_page(mission_guidance->destination);
        set_status_override(std::string(mission_guidance->prompt));
        return;
    }

    auto const guidance = inspector_pane_->current_student_guidance();
    switch (guidance.next_action) {
        case KeyAction::Step:
            handle_normal_keyboard_input('s', TuiKey::s);
            break;
        case KeyAction::LoadBinary:
            open_modal(ModalType::LoadBinary);
            break;
        case KeyAction::Reset:
            machine_.request_reboot();
            break;
        case KeyAction::CycleRegPage:
            cycle_reg_page();
            break;
        case KeyAction::CycleToolPage:
            cycle_tool_page();
            break;
        case KeyAction::ToggleExplain:
            toggle_explain();
            break;
        case KeyAction::InspectAddress:
            open_modal(ModalType::InspectAddress);
            break;
        case KeyAction::Settings:
            open_modal(ModalType::Settings);
            break;
        default:
            set_status_override(std::format("Use {} to {}",
                                            Keybindings::get(guidance.next_action).key_display,
                                            guidance.next_hint));
            render(true);
            break;
    }
}

void Tui::execute_header_action(HeaderHitResult hit) {
    switch (hit.action) {
        case HeaderAction::RunPause:
            if (machine_.is_shutdown_) {
                machine_.request_reboot();
            } else if (paused_) {
                unpause_loop();
            } else {
                pause_loop();
            }
            break;
        case HeaderAction::ToggleMode:
            toggle_execution_mode();
            break;
        case HeaderAction::SelectHart:
            if (machine_.num_harts() > 1) {
                selected_hart_ = hit.hart_index % machine_.num_harts();
                if (inspector_pane_) {
                    inspector_pane_->set_selected_hart(selected_hart_);
                    inspector_pane_->update_cache();
                }
                set_status_override(
                    std::format("Active telemetry switched to Hart {}", selected_hart_));
                render(true);
            }
            break;
        case HeaderAction::TogglePanelMode:
            open_tool_picker(focused_slot_index_);
            break;
        case HeaderAction::ToggleAttached:
            toggle_run_state();
            break;
        case HeaderAction::SetSpeed:
            open_modal(ModalType::SetSpeed);
            break;
        case HeaderAction::OpenSettings:
            open_modal(ModalType::Settings);
            break;
        case HeaderAction::OpenGlossary:
            open_modal(ModalType::Glossary);
            break;
        case HeaderAction::ToggleTheme:
            cycle_theme_style();
            render(true);
            break;
        case HeaderAction::Reboot:
            machine_.request_reboot();
            break;
        case HeaderAction::None:
        default:
            break;
    }
}

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
            } else if (machine_.primary_hart().state().pc == 0) {
                modal_.open_notice(
                    "NO PROGRAM LOADED",
                    "Cannot step: PC is 0x0.\n\nPlease load a program binary image first [o].",
                    false);
                render(true);
            } else {
                if (!is_paused()) pause_loop();
                machine_.step_sync();
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
            } else if (paused_ && machine_.primary_hart().state().pc == 0) {
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
            Address pc = machine_.primary_hart().state().pc;
            if (machine_.breakpoint_manager().has_pc_breakpoint(pc)) {
                machine_.breakpoint_manager().remove_pc_breakpoint(pc);
                modal_.open_notice("BREAKPOINT REMOVED",
                                   std::format("Removed PC breakpoint at 0x{:08x}", pc), false);
            } else {
                machine_.breakpoint_manager().add_pc_breakpoint(pc);
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
            machine_.request_exit();
            break;
        case TuiFooterAction::ToggleStudentGuide:
            toggle_student_guide();
            break;
        case TuiFooterAction::OpenSettings:
            open_modal(ModalType::Settings);
            break;
        case TuiFooterAction::ManageBreakpoints:
            open_modal(ModalType::ManageBreakpoints);
            break;
        case TuiFooterAction::SwitchHart:
            select_next_hart();
            break;
        case TuiFooterAction::ToggleTheme:
            cycle_theme_style();
            render(true);
            break;
        case TuiFooterAction::ToggleExecutionMode:
            toggle_execution_mode();
            break;
        case TuiFooterAction::OpenToolPicker:
            open_tool_picker(focused_slot_index_);
            break;
        case TuiFooterAction::MoveColumnLeft:
            move_focused_column_left();
            break;
        case TuiFooterAction::MoveColumnRight:
            move_focused_column_right();
            break;
        case TuiFooterAction::AddColumn:
            add_workbench_column();
            break;
        case TuiFooterAction::CloseColumn:
            close_focused_column();
            break;
        case TuiFooterAction::FocusNextPane:
            focus_next_slot();
            break;
        case TuiFooterAction::FocusPrevPane:
            focus_prev_slot();
            break;
    }
}

void Tui::select_next_hart() {
    if (machine_.num_harts() > 1) {
        selected_hart_ = (selected_hart_ + 1) % machine_.num_harts();
        if (inspector_pane_) {
            inspector_pane_->set_selected_hart(selected_hart_);
            inspector_pane_->update_cache();
        }
        set_status_override(std::format("Active telemetry switched to Hart {}", selected_hart_));
        render(true);
    }
}

auto Tui::handle_alt_key(char key, uint8_t byte) -> bool {
    switch (key) {
        case '1':
            apply_layout_preset(LayoutPreset::GeneralDebug);
            return true;
        case '2':
            apply_layout_preset(LayoutPreset::Microarchitecture);
            return true;
        case '3':
            apply_layout_preset(LayoutPreset::TraceExecution);
            return true;
        case '4':
            apply_layout_preset(LayoutPreset::MemoryInterconnect);
            return true;
        case '<':
            move_focused_column_left();
            return true;
        case '>':
            move_focused_column_right();
            return true;
        case 'n':
        case 'N':
        case 'a':
        case 'A':
            add_workbench_column();
            return true;
        case 'x':
        case 'X':
            close_focused_column();
            return true;
        case 'e':
        case 'E':
            toggle_explain();
            return true;
        case 'o':
        case 'O':
            open_modal(ModalType::LoadBinary);
            return true;
        case 't':
        case 'T':
            cycle_theme_style();
            render(true);
            return true;
        case 'u':
        case 'U':
            scroll(5);
            return true;
        case 'w':
        case 'W':
            scroll_inspector(-2);
            return true;
        case 's':
        case 'S':
            open_modal(ModalType::Settings);
            return true;
        case 'z':
        case 'Z':
            reset_scroll_inspector();
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
    if (esc_buf_ == "\033[1;3D" || esc_buf_ == "\033\033[D") {
        move_focused_column_left();
        return true;
    }
    if (esc_buf_ == "\033[1;3C" || esc_buf_ == "\033\033[C") {
        move_focused_column_right();
        return true;
    }
    if (esc_buf_ == "\033[1;5D") {
        focus_prev_slot();
        return true;
    }
    if (esc_buf_ == "\033[1;5C") {
        focus_next_slot();
        return true;
    }

    if (esc_buf_ == "\033[1;2C" || esc_buf_ == "\033[1;2D") {
        if (!is_modal_active() && inspector_pane_) {
            auto const widths = column_widths(cached_term_width_ > 0 ? cached_term_width_ : 80);
            auto const focused = std::min(focused_slot_index_,
                                          static_cast<size_t>(std::max<int>(1, widths.count) - 1));
            int const pane_width = widths.count > 0 ? widths.widths[focused] : 0;
            inspector_pane_->set_page(focused_page());
            if (inspector_pane_->supports_horizontal_scroll(pane_width)) {
                inspector_pane_->scroll_horizontal(esc_buf_.back() == 'C' ? 8 : -8, pane_width,
                                                   focused > 0);
                frame_dirty_ = true;
                render(true);
                return true;
            }
        }
        return true;
    }

    const bool up = esc_buf_ == "\033[A" || esc_buf_ == "\033OA";
    const bool down = esc_buf_ == "\033[B" || esc_buf_ == "\033OB";
    const bool right = esc_buf_ == "\033[C" || esc_buf_ == "\033OC";
    const bool left = esc_buf_ == "\033[D" || esc_buf_ == "\033OD";
    if (up || down) {
        const int direction = up ? -1 : 1;
        if (get_active_modal() == ModalType::Help) {
            modal_.scroll_help(direction);
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::ToolPicker) {
            modal_.move_tool_picker_cursor(direction);
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::Glossary) {
            modal_.scroll_glossary_content(2 * direction);
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::Settings) {
            modal_.move_settings_cursor(direction);
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::ManageBreakpoints) {
            modal_.move_bp_cursor(direction);
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::LayoutPresets) {
            modal_.move_preset_cursor(direction);
            render(true);
            return true;
        }
        if (paused_ && inspector_pane_) {
            auto page = focused_page();
            if (page == TuiRegPage::CACHE) {
                inspector_pane_->cycle_cache_way(direction);
                render(true);
                return true;
            }
            if (page == TuiRegPage::CONSOLE) {
                scroll(direction);
                return true;
            }
            scroll_inspector(direction);
            return true;
        }
    } else if (left || right) {
        const int direction = left ? -1 : 1;
        if (get_active_modal() == ModalType::ToolPicker) {
            modal_.cycle_tool_picker_slot(static_cast<int>(workbench_slots_.size()), direction);
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::Glossary) {
            modal_.move_glossary_topic(direction);
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::Settings) {
            modal_.adjust_setting_at_cursor(direction);
            render(true);
            return true;
        }
        if (paused_ && inspector_pane_) {
            auto page = focused_page();
            if (page == TuiRegPage::CACHE) {
                inspector_pane_->select_next_cache_set(direction);
                render(true);
                return true;
            }
            auto const widths = column_widths(cached_term_width_ > 0 ? cached_term_width_ : 80);
            auto const focused = std::min(focused_slot_index_,
                                          static_cast<size_t>(std::max<int>(1, widths.count) - 1));
            int const pane_width = widths.count > 0 ? widths.widths[focused] : 0;
            inspector_pane_->set_page(page);
            if (inspector_pane_->supports_horizontal_scroll(pane_width)) {
                inspector_pane_->scroll_horizontal(4 * direction, pane_width, focused > 0);
                frame_dirty_ = true;
                render(true);
                return true;
            }
        }
    } else if (esc_buf_ == "\033[5~") {
        if (get_active_modal() == ModalType::Help) {
            modal_.scroll_help(-10);
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::Glossary) {
            modal_.scroll_glossary_content(-5);
            render(true);
            return true;
        }
        if (focused_page() == TuiRegPage::CONSOLE) {
            scroll(-10);
        } else {
            scroll_inspector(-10);
        }
        return true;
    } else if (esc_buf_ == "\033[6~") {
        if (get_active_modal() == ModalType::Help) {
            modal_.scroll_help(10);
            render(true);
            return true;
        }
        if (get_active_modal() == ModalType::Glossary) {
            modal_.scroll_glossary_content(5);
            render(true);
            return true;
        }
        if (focused_page() == TuiRegPage::CONSOLE) {
            scroll(10);
        } else {
            scroll_inspector(10);
        }
        return true;
    } else if (esc_buf_ == "\033[H" || esc_buf_ == "\033[1~") {
        if (focused_page() == TuiRegPage::CONSOLE) {
            reset_scroll();
        } else {
            reset_scroll_inspector();
        }
        return true;
    } else if (esc_buf_ == "\033[Z" || esc_buf_ == "\033[1;2I") {
        if (is_modal_active()) {
            return handle_modal_keyboard_input(0, simrv::tui::TuiKey::BackTab);
        }
        return handle_navigation_keyboard_input(0, simrv::tui::TuiKey::BackTab);
    } else if (esc_buf_ == "\033OP" || esc_buf_ == "\033[11~" || esc_buf_ == "\033[1;2P" ||
               esc_buf_ == "\033[O1P" || esc_buf_ == "\033[[A") {
        return handle_navigation_keyboard_input(0, simrv::tui::TuiKey::F1);
    } else if (esc_buf_ == "\033OQ" || esc_buf_ == "\033[12~" || esc_buf_ == "\033[1;2Q" ||
               esc_buf_ == "\033[O1Q" || esc_buf_ == "\033[[B") {
        return handle_navigation_keyboard_input(0, simrv::tui::TuiKey::F2);
    } else if (esc_buf_ == "\033OR" || esc_buf_ == "\033[13~" || esc_buf_ == "\033[1;2R" ||
               esc_buf_ == "\033[O1R" || esc_buf_ == "\033[[C") {
        return handle_navigation_keyboard_input(0, simrv::tui::TuiKey::F3);
    } else if (esc_buf_ == "\033OS" || esc_buf_ == "\033[14~" || esc_buf_ == "\033[1;2S" ||
               esc_buf_ == "\033[O1S" || esc_buf_ == "\033[[D") {
        return handle_navigation_keyboard_input(0, simrv::tui::TuiKey::F4);
    } else if (esc_buf_ == "\033[15~" || esc_buf_ == "\033[[E") {
        return handle_navigation_keyboard_input(0, simrv::tui::TuiKey::F5);
    } else if (esc_buf_ == "\033[17~") {
        return handle_navigation_keyboard_input(0, simrv::tui::TuiKey::F6);
    } else if (esc_buf_ == "\033[18~") {
        return handle_navigation_keyboard_input(0, simrv::tui::TuiKey::F7);
    } else if (esc_buf_ == "\033[19~") {
        return handle_navigation_keyboard_input(0, simrv::tui::TuiKey::F8);
    } else if (esc_buf_ == "\033[20~") {
        return handle_navigation_keyboard_input(0, simrv::tui::TuiKey::F9);
    } else if (esc_buf_ == "\033[21~") {
        return handle_navigation_keyboard_input(0, simrv::tui::TuiKey::F10);
    } else if (esc_buf_ == "\033[23~") {
        return handle_navigation_keyboard_input(0, simrv::tui::TuiKey::F11);
    } else if (esc_buf_ == "\033[24~") {
        return handle_navigation_keyboard_input(0, simrv::tui::TuiKey::F12);
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
        if (esc_buf_.back() == 'm') {
            // Button release - finalize any active selection drag.
            if (selection_.is_selecting) {
                selection_.is_selecting = false;
                if (selection_.start_x != selection_.end_x ||
                    selection_.start_y != selection_.end_y) {
                    copy_active_selection();
                }
                render(true);
            }
            if (display_mouse_capture_.has_value()) {
                uint16_t btn = 0;
                if (button == 0)
                    btn = 0x110;  // BTN_LEFT
                else if (button == 1)
                    btn = 0x112;  // BTN_MIDDLE
                else if (button == 2)
                    btn = 0x111;  // BTN_RIGHT
                if (btn != 0) {
                    machine_.send_input_mouse_button(btn, false);
                }
                display_mouse_capture_.reset();
                display_mouse_last_fb_.reset();
            }
            return true;
        }

        // Motion events (button | 32) are generated by ?1003h (all-motion mode) and
        // must be consumed here - never forwarded to the guest UART.
        if (esc_buf_.back() == 'M' && (button & 32) != 0) {
            if (selection_.is_selecting) {
                int local_end_x = std::clamp(x - selection_.col_start_x, 0,
                                             std::max(0, selection_.pane_width - 1));
                local_end_x = selection_.bounds.clamp_x(local_end_x);
                selection_.end_x = local_end_x;
                selection_.end_y = selection_.bounds.clamp_y(y);
                selection_.is_active = true;
                // Paused frames are normally suppressed unless forced. Drag motion is an
                // explicit presentation update, so repaint immediately while the pointer moves.
                render(true);
            } else if (display_mouse_capture_.has_value() ||
                       focused_page() == TuiRegPage::DISPLAY) {
                struct winsize w{};
                ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
                int const tw =
                    (cached_term_width_ > 0) ? cached_term_width_ : (w.ws_col > 0 ? w.ws_col : 80);
                int const th = (cached_term_height_ > 0) ? cached_term_height_
                                                         : (w.ws_row > 0 ? w.ws_row : 24);
                const size_t display_col = display_mouse_capture_.value_or(focused_slot_index_);
                if (auto fb = display_coords_to_fb(x, y, display_col, tw, th)) {
                    machine_.send_input_mouse_motion(fb->first, fb->second);
                    display_mouse_last_fb_ = fb;
                }
            }
            return true;
        }

        if (is_modal_active()) {
            if (esc_buf_.back() == 'M') {
                if (button == 64 || button == 65) {
                    modal_.handle_wheel(button == 64 ? -1 : 1);
                    render(true);
                    return true;
                }
                if (button == 0) {
                    struct winsize w_m{};
                    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w_m);
                    int term_wm = (cached_term_width_ > 0) ? cached_term_width_
                                                           : (w_m.ws_col > 0 ? w_m.ws_col : 80);
                    int term_hm = (cached_term_height_ > 0) ? cached_term_height_
                                                            : (w_m.ws_row > 0 ? w_m.ws_row : 24);
                    auto res = modal_.handle_click(x, y, term_wm, term_hm);
                    if (res == TuiModal::ModalClickResult::Closed) {
                        close_modal();
                    } else if (res == TuiModal::ModalClickResult::ReloadRequested) {
                        close_modal();
                        auto draft = modal_.get_pending_platform_draft();
                        auto next = machine_.configuration();
                        next.platform_profile = draft.platform_profile == 0
                                                    ? simrv::core::PlatformProfile::Pcie
                                                    : simrv::core::PlatformProfile::Mmio;
                        (void)machine_.stage_reconfiguration(std::move(next));
                    } else if (res == TuiModal::ModalClickResult::DiscardRequested) {
                        close_modal();
                    } else if (res == TuiModal::ModalClickResult::Submit) {
                        submit_modal();
                    }
                    render(true);
                }
            }
            return true;
        }
        if (esc_buf_.back() == 'M' && button == 0 && y == 2) {
            struct winsize w{};
            ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
            int term_w =
                (cached_term_width_ > 0) ? cached_term_width_ : (w.ws_col > 0 ? w.ws_col : 80);
            if (status_bar_) {
                auto hit = status_bar_->get_header_action_at_col(x, term_w);
                if (hit.action != HeaderAction::None) {
                    execute_header_action(hit);
                    return true;
                }
                if (status_bar_->is_pos_on_status_badge(x, term_w)) {
                    if (machine_.is_shutdown_) {
                        machine_.request_reboot();
                    } else if (paused_) {
                        unpause_loop();
                    } else {
                        pause_loop();
                    }
                }
            }
            return true;
        }

        struct winsize w_footer{};
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w_footer);
        int term_w = (cached_term_width_ > 0) ? cached_term_width_
                                              : (w_footer.ws_col > 0 ? w_footer.ws_col : 80);
        int term_h = (cached_term_height_ > 0) ? cached_term_height_
                                               : (w_footer.ws_row > 0 ? w_footer.ws_row : 24);

        if (esc_buf_.back() == 'M' && button == 0 && (y == term_h - 2 || y == term_h - 1)) {
            int col = x - 2;
            int row_idx = (y == term_h - 2) ? 0 : 1;
            if (status_bar_) {
                auto act_opt = status_bar_->get_footer_action_at_col(col, row_idx, term_w);
                if (act_opt.has_value()) {
                    execute_footer_action(act_opt.value());
                    return true;
                }
            }
        }

        auto const mouse_columns = column_widths(term_w);
        bool const has_tab_bar = (mouse_columns.count == 1);
        if (esc_buf_.back() == 'M' && button == 0 && has_tab_bar && (y == 4 || y == 5)) {
            int pane_w = get_pane_width();
            if (x >= 2 && x <= pane_w + 1) {
                int col = x - 2;
                if (inspector_pane_) {
                    int tier = (y == 4) ? 0 : 1;
                    auto tab_opt = inspector_pane_->get_tab_at(tier, col);
                    if (tab_opt.has_value()) {
                        if (*tab_opt == TuiRegPage::CACHE &&
                            inspector_pane_->get_page() == TuiRegPage::CACHE) {
                            inspector_pane_->toggle_cache_inspect_type();
                            render(true);
                        } else {
                            set_reg_page(tab_opt.value());
                        }
                    } else if (tier == 0) {
                        cycle_reg_page();
                    }
                }
                return true;
            }
        }

        if (esc_buf_.back() == 'M' && button == 0 && y >= 5) {
            // Start a new selection drag on left-button press in the content area.
            struct winsize w_sel{};
            ioctl(STDOUT_FILENO, TIOCGWINSZ, &w_sel);
            int sel_w = (cached_term_width_ > 0) ? cached_term_width_
                                                 : (w_sel.ws_col > 0 ? w_sel.ws_col : 80);
            auto sel_cols = column_widths(sel_w);
            size_t sel_col_idx = 0;
            int cur_cx = 1;
            int col_local_x = 0;
            int col_w = (sel_cols.count > 0) ? sel_cols.widths[0] : sel_w;
            int col_start_x = 2;
            SelectionPane sel_pane = SelectionPane::None;
            for (size_t ci = 0; ci < sel_cols.count; ++ci) {
                int cw = sel_cols.widths[ci];
                if (x >= cur_cx && (x < cur_cx + cw + 1 || ci + 1 == sel_cols.count)) {
                    sel_col_idx = ci;
                    col_w = cw;
                    col_start_x = cur_cx + 1;
                    col_local_x = std::clamp(x - col_start_x, 0, std::max(0, cw - 1));
                    if (ci < workbench_slots_.size() &&
                        workbench_slots_[ci].page == TuiRegPage::CONSOLE) {
                        sel_pane = SelectionPane::TerminalPane;
                    } else if (ci < workbench_slots_.size() &&
                               workbench_slots_[ci].page == TuiRegPage::DISPLAY) {
                        // A running DISPLAY column is an interactive guest surface. Do not
                        // enter the paused inspector-selection path; route the click directly
                        // to the guest's virtio-input device instead.
                        focused_slot_index_ = ci;
                        display_mouse_capture_ = ci;
                        handle_display_mouse(x, y, button, ci, sel_w, term_h);
                        return true;
                    } else {
                        if (!paused_) return true;
                        sel_pane = SelectionPane::InspectorPane;
                    }
                    break;
                }
                cur_cx += cw + 1;
            }
            selection_ = SelectionState{};
            selection_.pane = sel_pane;
            selection_.col_idx = sel_col_idx;
            selection_.col_start_x = col_start_x;
            int const panel_content_start =
                (sel_cols.count >= 2) ? 5 : (sel_pane == SelectionPane::TerminalPane ? 4 : 6);
            int subpanel_start_y = panel_content_start;
            int subpanel_end_y = std::max(panel_content_start, inspector_content_end_row(term_h));
            if (sel_pane == SelectionPane::InspectorPane && sel_col_idx == 0 &&
                sel_col_idx < workbench_slots_.size()) {
                auto const page = workbench_slots_[sel_col_idx].page;
                int const num_rows = std::max(1, term_h - framework::kFrameChromeRows);
                bool const has_log =
                    num_rows >= 15 && page != TuiRegPage::EXPLAIN && page != TuiRegPage::TRACE;
                if (has_log) {
                    int const log_start_y = inspector_log_start_row(term_h);
                    if (y >= log_start_y) {
                        subpanel_start_y = log_start_y;
                    } else {
                        subpanel_end_y = log_start_y - 1;
                    }
                }
            }
            selection_.content_start_y = subpanel_start_y;
            selection_.content_end_y = std::max(subpanel_start_y, subpanel_end_y);
            if (y < selection_.content_start_y || y > selection_.content_end_y) {
                selection_ = SelectionState{};
                return true;
            }
            selection_.pane_width = col_w;
            selection_.bounds = {
                .x = 0,
                .y = selection_.content_start_y,
                .width = col_w,
                .height = selection_.content_end_y - selection_.content_start_y + 1};
            selection_.start_x = col_local_x;
            selection_.start_y = y;
            selection_.end_x = col_local_x;
            selection_.end_y = y;
            selection_.is_selecting = true;
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

    // 3. Arrow and function keys
    if (handle_arrow_key_sequence()) return true;

    if (esc_buf_.size() == 1) {
        if (is_modal_active()) {
            close_modal();
            return true;
        }
    }

    if (!paused_.load(std::memory_order_relaxed)) {
        if (focused_page() == TuiRegPage::DISPLAY) {
            if (esc_buf_ == "\033[A" || esc_buf_ == "\033OA") {
                machine_.send_input_key(103 /* KEY_UP */, true);
                machine_.send_input_key(103, false);
                return true;
            } else if (esc_buf_ == "\033[B" || esc_buf_ == "\033OB") {
                machine_.send_input_key(108 /* KEY_DOWN */, true);
                machine_.send_input_key(108, false);
                return true;
            } else if (esc_buf_ == "\033[C" || esc_buf_ == "\033OC") {
                machine_.send_input_key(106 /* KEY_RIGHT */, true);
                machine_.send_input_key(106, false);
                return true;
            } else if (esc_buf_ == "\033[D" || esc_buf_ == "\033OD") {
                machine_.send_input_key(105 /* KEY_LEFT */, true);
                machine_.send_input_key(105, false);
                return true;
            }
        }
        for (char c : esc_buf_) {
            write_guest_input(static_cast<uint8_t>(c));
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
