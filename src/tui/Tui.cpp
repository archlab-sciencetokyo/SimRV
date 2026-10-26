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
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <thread>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/debug/BreakpointManager.hpp"
#include "simrv/debug/SymbolTable.hpp"
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
#include "simrv/tui/modals/HartModal.hpp"
#include "simrv/tui/modals/LoadModal.hpp"
#include "simrv/tui/modals/LogModal.hpp"
#include "simrv/tui/modals/MemoryModal.hpp"
#include "simrv/tui/modals/ToolPickerModal.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"
#include "simrv/tui/panels/StatusBar.hpp"
#include "simrv/tui/panels/TerminalPane.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::tui {

namespace {
constexpr uint32_t kTuiTargetFps = 60;
}

static struct termios
    g_saved_termios;                  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_termios_saved = false;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static bool g_tui_active = false;     // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static volatile std::sig_atomic_t g_osc22_pointer_enabled =
    0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

namespace {

constexpr int kInspectorContentStartRow = 4;
constexpr int kMinDisplayWidth = 24;
constexpr int kMinDisplayBodyRows = 8;

struct PanelColumnHit {
    size_t index;
    int local_x;
    int screen_start_x;
};

[[nodiscard]] auto locate_panel_column(int x, const framework::ColumnWidths& columns)
    -> std::optional<PanelColumnHit> {
    int screen_start_x = 2;
    for (size_t i = 0; i < columns.count; ++i) {
        const int width = columns.widths[i];
        if (x >= screen_start_x && x < screen_start_x + width) {
            return PanelColumnHit{
                .index = i, .local_x = x - screen_start_x, .screen_start_x = screen_start_x};
        }
        screen_start_x += width + 1;  // one divider separates adjacent content panels
    }
    return std::nullopt;
}

[[nodiscard]] auto center_pane_text(std::string_view text, int width) -> std::string {
    const int text_w = get_display_width(text);
    if (text_w >= width) return format_to_width(std::string(text), width);
    const int left_pad = (width - text_w) / 2;
    const int right_pad = width - text_w - left_pad;
    return std::string(static_cast<size_t>(left_pad), ' ') + std::string(text) +
           std::string(static_cast<size_t>(right_pad), ' ');
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
        if (g_osc22_pointer_enabled != 0) {
            constexpr std::string_view reset_pointer = "\033]22;default\033\\";
            (void)::write(STDOUT_FILENO, reset_pointer.data(), reset_pointer.size());
            g_osc22_pointer_enabled = 0;
        }
        const char* shutdown_seq =
            "\033[0m\033[?2004l\033[?1016l\033[?1006l\033[?1003l\033[?1000l\033[?25h\033[2J\033["
            "H\033[?"
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
        if (g_osc22_pointer_enabled != 0) {
            auto constexpr reset_pointer = "\033]22;default\033\\"sv;
            (void)::write(STDOUT_FILENO, reset_pointer.data(), reset_pointer.size());
            g_osc22_pointer_enabled = 0;
        }
        auto constexpr shutdown_seq =
            "\033[0m\033[?2004l\033[?1016l\033[?1006l\033[?1003l\033[?1000l\033[?25h\033[2J\033[H\033[?1049l\n"sv;
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

auto Tui::compute_sixel_dimensions(int panel_cols, int body_rows, int cell_width_px,
                                   int cell_height_px, int source_width, int source_height) noexcept
    -> std::pair<int, int> {
    const int src_w = std::max(1, source_width);
    const int src_h = std::max(1, source_height);
    const int cell_width = std::max(1, cell_width_px);
    const int cell_height = std::max(1, cell_height_px);
    const int max_width = std::max(1, panel_cols * cell_width);
    const int max_height = std::max(1, body_rows * cell_height);

    if (max_width >= src_w && max_height >= src_h) {
        // Integer upscaling: 1x, 2x, 3x, etc. Every source pixel is duplicated
        // into an exact NxN square of terminal pixels without fractional jitter.
        const int scale = std::max(1, std::min(max_width / src_w, max_height / src_h));
        return {scale * src_w, scale * src_h};
    }

    // Integer downscaling: 1/2x, 1/3x, 1/4x, etc. Pixels are sub-sampled at
    // uniform integer strides, preserving clean pixel alignment.
    const int divisor = std::max(std::max(2, (src_w + max_width - 1) / max_width),
                                 (src_h + max_height - 1) / max_height);
    const int width = std::max(1, src_w / divisor);
    const int height = std::max(1, src_h / divisor);
    return {width, height};
}

Tui::Tui(simrv::core::Machine& machine) : machine_(machine), modal_(machine) {
    modal_.set_log_buffer(&log_buffer_);
    preferences_path_ = TuiPreferences::default_path();
    restore_preferences();
    main_thread_id_ = std::this_thread::get_id();
    last_speed_update_ = std::chrono::steady_clock::now();
    mission_.configure(machine_.mission_id(), machine_.binary_path());
    apply_mission_speed();
    sync_mission_checkpoint();
    apply_mission_guidance_layout();
    modal_.set_mission_progress(&mission_);
    mission_revision_seen_ = mission_.revision();
    if (mission_.guidance()) modal_.open_mission_objective();
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

Tui::~Tui() {
    shutdown();
    if (mission_breakpoint_addr_)
        machine_.breakpoint_manager().remove_pc_breakpoint(*mission_breakpoint_addr_,
                                                           simrv::debug::BreakpointOwner::Mission);
}

void Tui::sync_mission_checkpoint() {
    if (mission_breakpoint_addr_) {
        machine_.breakpoint_manager().remove_pc_breakpoint(*mission_breakpoint_addr_,
                                                           simrv::debug::BreakpointOwner::Mission);
        mission_breakpoint_addr_.reset();
    }
    const auto progress = mission_.checkpoint_progress();
    if (!progress || progress->reached) return;
    const auto checkpoint = std::string_view(progress->symbol);
    const auto address = machine_.symbol_table().lookup_name(std::string(checkpoint));
    if (!address) {
        set_status_override(
            "Lesson checkpoint is missing from the loaded program. Ask your "
            "instructor to load the matching program file.");
        return;
    }
    machine_.breakpoint_manager().add_pc_breakpoint(*address,
                                                    simrv::debug::BreakpointOwner::Mission);
    mission_breakpoint_addr_ = *address;
}

void Tui::apply_mission_guidance_layout() {
    const auto guidance = mission_.guidance();
    if (!guidance || workbench_slots_.empty()) return;
    if (!guidance->panels.empty()) {
        workbench_slots_.clear();
        for (const auto page : guidance->panels)
            workbench_slots_.push_back({.page = page, .scroll_offset = 0});
        layout_ = layout_for_column_count(workbench_slots_.size(),
                                          workbench_slots_.front().page == TuiRegPage::CONSOLE);
        focused_slot_index_ = 0;
    } else {
        const auto found = std::ranges::find_if(
            workbench_slots_, [&](const auto& slot) { return slot.page == guidance->destination; });
        if (found != workbench_slots_.end()) {
            focused_slot_index_ = static_cast<size_t>(found - workbench_slots_.begin());
        } else {
            if (focused_slot_index_ >= workbench_slots_.size()) focused_slot_index_ = 0;
            workbench_slots_[focused_slot_index_].page = guidance->destination;
        }
    }
    if (inspector_pane_ && focused_slot_index_ < workbench_slots_.size())
        inspector_pane_->set_page(workbench_slots_[focused_slot_index_].page);
    mission_layout_fits_.reset();
}

void Tui::refresh_mission_layout_notice() {
    const auto guidance = mission_.guidance();
    if (!guidance || guidance->panels.empty() || cached_term_width_ <= 0 ||
        cached_term_height_ <= 0) {
        return;
    }
    const auto panel_count = static_cast<uint8_t>(guidance->panels.size());
    const int required_width = framework::min_width_for_columns(panel_count);
    const bool fits = cached_term_width_ >= required_width &&
                      cached_term_height_ >= framework::kMinimumTerminalHeight;
    const auto supported_by_width =
        std::min<uint8_t>(panel_count, framework::max_supported_columns(cached_term_width_));
    const auto visible_panels =
        cached_term_height_ >= framework::kMinimumTerminalHeight ? supported_by_width : uint8_t{0};
    const auto prior = mission_layout_fits_;
    if (prior && *prior == fits) return;
    mission_layout_fits_ = fits;
    if (!fits) {
        set_status_override(
            std::format("Mission needs {}x{}; window is {}x{}, showing {}/{} panels. Resize or "
                        "reduce this step's panels.",
                        required_width, framework::kMinimumTerminalHeight, cached_term_width_,
                        cached_term_height_, visible_panels, panel_count));
    } else if (prior && !*prior) {
        set_status_override("Mission panel layout now fits the current window.");
    }
}

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
            last_icount_ = total_snapshot_instruction_count();
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
    terminal_pane_ = std::make_unique<TerminalPane>();
    status_bar_ = std::make_unique<StatusBar>(machine_, this);

    if (workbench_slots_.empty())
        workbench_slots_ = {{.page = TuiRegPage::GPR, .scroll_offset = 0}};
    if (focused_slot_index_ >= workbench_slots_.size()) focused_slot_index_ = 0;
    apply_mission_guidance_layout();
    inspector_pane_->set_page(workbench_slots_[focused_slot_index_].page);

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

    // Query OSC 22 pointer-shape support, cell pixel size, and primary device attributes.
    // Keep mouse reports in terminal cells: pixel mode depends on terminal-specific pixel
    // metrics and can shift every TUI hit target when those metrics differ from the renderer.
    constexpr std::string_view terminal_queries =
        "\033]22;?pointer\033\\\033]22;?__current__\033\\\033[14t\033[16t\033[c";
    write_all(STDOUT_FILENO, terminal_queries);

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
                const auto cell_reply = resp.find("\033[6;");
                const auto da1_reply = resp.find("\033[?");
                if (cell_reply != std::string::npos &&
                    resp.find('t', cell_reply) != std::string::npos &&
                    da1_reply != std::string::npos &&
                    resp.find('c', da1_reply) != std::string::npos) {
                    break;
                }
            }
        }
    }

    // 0. Window size query \033[4;<H>;<W>t fallback
    size_t win_start = resp.find("\033[4;");
    size_t win_t_pos =
        win_start == std::string::npos ? std::string::npos : resp.find('t', win_start);
    if (win_t_pos != std::string::npos) {
        size_t seq_start = resp.rfind("\033[4;", win_t_pos);
        if (seq_start != std::string::npos) {
            std::string_view payload(resp.data() + seq_start + 4, win_t_pos - (seq_start + 4));
            size_t semi = payload.find(';');
            if (semi != std::string_view::npos) {
                int q_h = 0;
                int q_w = 0;
                auto res_h = std::from_chars(payload.data(), payload.data() + semi, q_h);
                auto res_w = std::from_chars(payload.data() + semi + 1,
                                             payload.data() + payload.size(), q_w);
                if (res_h.ec == std::errc{} && res_w.ec == std::errc{} && q_h > 0 && q_w > 0) {
                    struct winsize ws{};
                    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
                        ws.ws_row > 0) {
                        cell_width_px_ = std::max(1, q_w / ws.ws_col);
                        cell_height_px_ = std::max(1, q_h / ws.ws_row);
                    }
                }
            }
        }
    }

    // Initialize from TIOCGWINSZ pixel dimensions if available
    struct winsize ws_init{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws_init) == 0 && ws_init.ws_col > 0 &&
        ws_init.ws_row > 0 && ws_init.ws_xpixel > 0 && ws_init.ws_ypixel > 0) {
        cell_width_px_ = std::max(
            1,
            static_cast<int>(std::round(static_cast<double>(ws_init.ws_xpixel) / ws_init.ws_col)));
        cell_height_px_ = std::max(
            1,
            static_cast<int>(std::round(static_cast<double>(ws_init.ws_ypixel) / ws_init.ws_row)));
    }

    // 1. Exact cell size response \033[6;<H>;<W>t overrides window division
    size_t cell_start = resp.find("\033[6;");
    size_t t_pos = cell_start == std::string::npos ? std::string::npos : resp.find('t', cell_start);
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
    size_t da_start_pos = resp.find("\033[?");
    size_t c_pos =
        da_start_pos == std::string::npos ? std::string::npos : resp.find('c', da_start_pos);
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

    // OSC 22 is optional. Only send shape changes after an explicit positive reply.
    osc22_pointer_supported_ = resp.find("\033]22;1\033\\") != std::string::npos ||
                               resp.find("\033]22;1\a") != std::string::npos;
    const auto current_query = resp.find("?__current__");
    const auto current = current_query == std::string::npos ? std::string::npos
                                                            : resp.find("\033]22;", current_query);
    for (std::string_view terminator : {std::string_view("\033\\"), std::string_view("\a")}) {
        if (current == std::string::npos) break;
        const auto value_start = current + 5;
        const auto value_end = resp.find(terminator, value_start);
        if (value_end == std::string::npos) continue;
        const auto value = std::string_view(resp).substr(value_start, value_end - value_start);
        if (value == "0" || value.empty()) {
            prior_pointer_shape_ = "default";
        } else if (std::ranges::all_of(value, [](unsigned char ch) {
                       return std::isalnum(ch) != 0 || ch == '_' || ch == '-';
                   })) {
            prior_pointer_shape_ = value;
        }
        break;
    }
    g_osc22_pointer_enabled = osc22_pointer_supported_ ? 1 : 0;

    g_tui_active = true;

    std::string init_seq = "\033[?1049h\033[2J\033[H\033[?1000h\033[?1003h\033[?1006h";
    init_seq += "\033[?2004h\033[?25l";
    write_all(STDOUT_FILENO, init_seq);

    struct sigaction sa{};
    sa.sa_handler = handle_sigwinch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);

    simrv::log::set_tui_structured_callback(
        [this](const simrv::log::LogRecord& rec) -> void { print_log(rec); });

    start_ui_thread();

    if (machine_.binary_path().empty()) {
        open_modal(ModalType::LoadBinary);
    }
}

void Tui::shutdown() {
    save_preferences();
    stop_ui_thread();
    if (backend_) backend_->detach();
    simrv::log::set_tui_structured_callback(nullptr);
    simrv::log::set_tui_callback(nullptr);
    if (osc22_pointer_supported_) {
        std::string sequence = "\033]22;";
        sequence += prior_pointer_shape_;
        sequence += "\033\\";
        write_all(STDOUT_FILENO, sequence);
        g_osc22_pointer_enabled = 0;
        pointer_is_clickable_ = false;
    }
    emergency_terminal_restore();

    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);
}

void Tui::restore_preferences() {
    if (preferences_path_.empty()) return;
    TuiPreferences preferences;
    if (!preferences.load(preferences_path_)) return;
    layout_ = preferences.layout;
    density_mode_ = preferences.density_mode;
    modal_.set_density_mode(density_mode_);
    workbench_slots_ = std::move(preferences.slots);
    if (workbench_slots_.empty())
        workbench_slots_ = {{.page = TuiRegPage::GPR, .scroll_offset = 0}};
    if (inspector_pane_) {
        for (const auto& slot : workbench_slots_) {
            if (slot.page != TuiRegPage::CONSOLE && slot.page != TuiRegPage::DISPLAY) {
                inspector_pane_->set_page(slot.page);
                inspector_pane_->set_scroll_offset(slot.scroll_offset);
            }
        }
    }
    focused_slot_index_ = preferences.focused_slot;
    user_inspector_width_ = preferences.inspector_width;
    user_column_widths_ = preferences.column_widths;
    set_theme_style(preferences.theme);
    machine_.set_high_contrast_enabled(preferences.high_contrast);
}

void Tui::save_preferences() {
    if (preferences_path_.empty()) return;
    TuiPreferences preferences;
    preferences.layout = layout_;
    preferences.slots = workbench_slots_;
    preferences.focused_slot = focused_slot_index_;
    preferences.inspector_width = user_inspector_width_;
    preferences.column_widths = user_column_widths_;
    preferences.theme = get_active_theme_style();
    preferences.high_contrast = machine_.high_contrast_enabled();
    preferences.density_mode = density_mode_;
    if (!preferences.save(preferences_path_)) {
        simrv::log::warn("Unable to save TUI preferences to {}", preferences_path_.string());
    }
}

auto Tui::total_snapshot_instruction_count() const -> uint64_t {
    uint64_t total = 0;
    for (size_t hart = 0; hart < machine_.num_harts(); ++hart) {
        total += machine_.tui_execution_snapshot(hart).instruction_count;
    }
    return total;
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
            const auto interval = std::chrono::milliseconds(1000 / kTuiTargetFps);
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
            pollfd fds[] = {{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0},
                            {.fd = ui_wake_.get(), .events = POLLIN, .revents = 0}};
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
    std::string text = msg;
    if (!text.empty() && text.back() == '\n') text.pop_back();
    {
        std::scoped_lock lock(io_mutex_);
        log_fifo_.push(LogEntry{.timestamp = std::chrono::steady_clock::now(),
                                .level = simrv::log::Level::Info,
                                .text = std::move(text)});
    }
    trigger_immediate_render();
}

void Tui::print_log(const simrv::log::LogRecord& rec) {
    std::string text = simrv::log::format_tui_message(rec.level, rec.message, rec.timestamp);
    if (!text.empty() && text.back() == '\n') text.pop_back();
    {
        std::scoped_lock lock(io_mutex_);
        log_fifo_.push(
            LogEntry{.timestamp = rec.timestamp, .level = rec.level, .text = std::move(text)});
    }
    trigger_immediate_render();
}

void Tui::render_update_speed(std::chrono::steady_clock::time_point now) {
    const uint64_t current_icount = total_snapshot_instruction_count();
    if (!is_paused()) {
        if (last_runtime_tick_ != std::chrono::steady_clock::time_point{}) {
            runtime_duration_ +=
                std::chrono::duration_cast<std::chrono::microseconds>(now - last_runtime_tick_);
        }
        last_runtime_tick_ = now;
        auto diff =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_speed_update_).count();
        if (diff >= 50) {
            uint64_t insns_since_last =
                current_icount >= last_icount_ ? current_icount - last_icount_ : 0;
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
        if (current_icount >= last_icount_ && current_icount > last_icount_) {
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
        } else if (current_icount < last_icount_) {
            last_icount_ = current_icount;
        }
    }
}

auto Tui::get_terminal_pane_start_line(int num_rows) const -> int {
    int total_lines = vt_.get_lines_count();
    int end_exclusive = std::max(0, total_lines - scroll_offset_);
    return std::max(0, end_exclusive - num_rows);
}

void Tui::render_build_lines(int inspector_width, int terminal_width, int terminal_rows,
                             int frame_content_rows, TuiRightPanelMode panel_mode) {
    if (!status_override_.empty() &&
        status_override_expires_at_ != std::chrono::steady_clock::time_point::max() &&
        std::chrono::steady_clock::now() >= status_override_expires_at_) {
        clear_status_override();
    }
    if (!inspector_pane_ || !terminal_pane_ || !status_bar_) return;
    cached_num_rows_ = terminal_rows;
    lines_to_draw_.clear();
    if (terminal_width > 0 && terminal_rows > 0) {
        vt_.resize(terminal_width, terminal_rows);
        int total = vt_.get_lines_count();
        int start = get_terminal_pane_start_line(terminal_rows);
        int end_exclusive = std::min(total, start + terminal_rows);

        // Terminal output is parsed in guest-sized chunks. Reuse complete ANSI rows when a
        // frame observes the same chunk and geometry; selections intentionally bypass this
        // cache because they add presentation-only attributes.
        const uint64_t terminal_generation = vt_.generation();
        const bool reuse_terminal_rows =
            !selection_.is_active && terminal_rows_generation_ == terminal_generation &&
            terminal_rows_width_ == terminal_width && terminal_rows_count_ == terminal_rows &&
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
                terminal_rows_count_ = terminal_rows;
                terminal_rows_start_ = start;
            }
        }
        while (lines_to_draw_.size() < static_cast<std::size_t>(terminal_rows)) {
            lines_to_draw_.emplace_back(static_cast<std::size_t>(terminal_width), ' ');
        }
    }

    bool has_log_page = false;
    for (const auto& slot : workbench_slots_) {
        if (slot.page == TuiRegPage::LOG) {
            has_log_page = true;
            break;
        }
    }
    bool const log_visible =
        has_log_page ||
        (inspector_pane_->is_log_drawer_open() && frame_content_rows >= 15 &&
         !workbench_slots_.empty() && workbench_slots_[0].page != TuiRegPage::EXPLAIN &&
         workbench_slots_[0].page != TuiRegPage::TRACE &&
         workbench_slots_[0].page != TuiRegPage::LOG);
    if (log_visible) {
        int const log_width = std::max(10, inspector_width - 2);
        inspector_pane_->set_log_lines(log_buffer_.get_wrapped_lines(log_width, 1000));
    } else {
        inspector_pane_->set_log_lines({});
    }

    if (focused_slot_index_ < workbench_slots_.size()) {
        inspector_pane_->set_page(workbench_slots_[focused_slot_index_].page);
    }
    inspector_pane_->set_paused(paused_);
    inspector_pane_->set_selected_hart(selected_hart_);
    modal_.set_selected_hart(selected_hart_);
    inspector_pane_->set_kips(kips_);
    inspector_pane_->set_max_kips(max_kips_);
    inspector_pane_->set_kips_history(kips_history_);
    inspector_pane_->set_visible_rows(frame_content_rows);
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
    const int body_rows = std::max(1, display_rows - 1);
    if (modal_.is_active() || display_width < kMinDisplayWidth || body_rows < kMinDisplayBodyRows)
        return;

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

    const int source_width = static_cast<int>(machine_.framebuffer_width());
    const int source_height = static_cast<int>(machine_.framebuffer_height());
    constexpr int palette_side = 6;
    constexpr int palette_size = palette_side * palette_side * palette_side;
    const int cell_width = std::max(1, cell_width_px_);
    const int cell_height = std::max(1, cell_height_px_);
    // Fit the framebuffer to the panel's pixel rectangle, adapting to the terminal
    // size while preserving the aspect ratio.
    const auto [width, height] = compute_sixel_dimensions(display_width, body_rows, cell_width,
                                                          cell_height, source_width, source_height);
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
    if (!same_dims) {
        if (sixel_src_x_offset_.size() != static_cast<size_t>(width) ||
            sixel_cached_source_width_ != source_width) {
            sixel_cached_source_width_ = source_width;
            sixel_src_x_offset_.resize(width);
            for (int x = 0; x < width; ++x) {
                sixel_src_x_offset_[x] = std::min(source_width - 1, x * source_width / width) * 4;
            }
        }
    }

    const auto total_strips = static_cast<size_t>((height + 5) / 6);
    if (sixel_cached_width_ != width || sixel_cached_height_ != height) {
        sixel_cached_width_ = width;
        sixel_cached_height_ = height;
        sixel_strip_signatures_.clear();
        sixel_cached_strips_.clear();
        sixel_strip_masks_.assign(static_cast<size_t>(palette_size * width), 0);
    }
    if (sixel_strip_signatures_.size() != total_strips) {
        sixel_strip_signatures_.assign(total_strips, 0);
        sixel_cached_strips_.resize(total_strips);
    }

    auto& strip_masks = sixel_strip_masks_;
    std::array<int16_t, palette_size> last_x_for_color;
    last_x_for_color.fill(-1);
    auto& present_colors = sixel_present_colors_;
    present_colors.clear();

    auto append_uint = [](std::string& out, unsigned val) {
        char buf[16];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), val);
        out.append(buf, static_cast<size_t>(ptr - buf));
    };

    for (int y = 0; y < height; y += 6) {
        const auto strip_idx = static_cast<size_t>(y / 6);
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
                    const auto c = static_cast<uint8_t>((kColorLut[p[2]] * 36) +
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
                const int x_off = sixel_src_x_offset_[x];
                for (int bit = 0; bit < max_bits; ++bit) {
                    const auto* p = row_ptrs[bit] + x_off;
                    const auto c = static_cast<uint8_t>((kColorLut[p[2]] * 36) +
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
    const int source_width = static_cast<int>(machine_.framebuffer_width());
    const int source_height = static_cast<int>(machine_.framebuffer_height());
    if (fb.data() == nullptr || width <= 0 || rows <= 0)
        return std::string(std::max(0, width), ' ');

    const int pixel_width = std::max(1, width);
    const int pixel_height = std::max(2, rows * 2);
    const int top = std::clamp(row * 2, 0, pixel_height - 1);
    const int bottom = std::min(pixel_height - 1, top + 1);
    const int source_top = std::min(source_height - 1, top * source_height / pixel_height);
    const int source_bottom = std::min(source_height - 1, bottom * source_height / pixel_height);
    const auto* row_top = fb.unchecked_ptr(
        fb.base() + static_cast<simrv::xlen::Address>(source_top * source_width * 4));
    const auto* row_bottom = fb.unchecked_ptr(
        fb.base() + static_cast<simrv::xlen::Address>(source_bottom * source_width * 4));

    std::string result;
    result.reserve(static_cast<size_t>(width) * 32);
    for (int x = 0; x < width; ++x) {
        const int source_x = std::min(source_width - 1, x * source_width / pixel_width);
        const auto* p_top = row_top + static_cast<size_t>(source_x * 4);
        const auto* p_bottom = row_bottom + static_cast<size_t>(source_x * 4);
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
    if (col_width < kMinDisplayWidth || body_rows < kMinDisplayBodyRows) return std::nullopt;
    const int cell_width = std::max(1, cell_width_px_);
    const int cell_height = std::max(1, cell_height_px_);
    const int source_width = static_cast<int>(machine_.framebuffer_width());
    const int source_height = static_cast<int>(machine_.framebuffer_height());

    const auto [width, height] = compute_sixel_dimensions(col_width, body_rows, cell_width,
                                                          cell_height, source_width, source_height);
    const int image_cols = std::max(1, (width + cell_width - 1) / cell_width);
    const int image_rows = std::max(1, (height + cell_height - 1) / cell_height);
    const int x_offset = std::max(0, (col_width - image_cols) / 2);
    const int y_offset = std::max(0, (body_rows - image_rows) / 2);
    const int display_x = display_panel_x + x_offset;
    const int display_y = 5 + y_offset;

    const int rel_col = x - display_x;
    const int rel_row = y - display_y;
    if (rel_col >= 0 && rel_col < image_cols && rel_row >= 0 && rel_row < image_rows) {
        // Mouse reports identify terminal cells, while sixel raster attributes identify pixels.
        // Center the hit target within the cell, bounded by the actual raster extent.
        const int px = std::clamp(rel_col * cell_width + cell_width / 2, 0, width - 1);
        const int py = std::clamp(rel_row * cell_height + cell_height / 2, 0, height - 1);
        const int fb_x = std::clamp(px * source_width / width, 0, source_width - 1);
        const int fb_y = std::clamp(py * source_height / height, 0, source_height - 1);
        return std::make_pair(fb_x, fb_y);
    }
    return std::nullopt;
}

auto Tui::display_pixels_to_fb(int x, int y, size_t col_idx, int term_width, int term_height,
                               int term_pixel_width, int term_pixel_height) const
    -> std::optional<std::pair<int, int>> {
    auto const col_widths = column_widths(term_width);
    if (col_idx >= col_widths.count || x <= 0 || y <= 0) return std::nullopt;

    int panel_x = 2;
    for (size_t c = 0; c < col_idx; ++c) panel_x += col_widths.widths[c] + 1;
    const int panel_width = col_widths.widths[col_idx];
    const int rows = std::max(1, term_height - framework::kFrameChromeRows);
    const int body_rows = std::max(1, rows - 1);
    if (panel_width < kMinDisplayWidth || body_rows < kMinDisplayBodyRows) return std::nullopt;

    const int cell_width = std::max(1, cell_width_px_);
    const int cell_height = std::max(1, cell_height_px_);
    const int source_width = static_cast<int>(machine_.framebuffer_width());
    const int source_height = static_cast<int>(machine_.framebuffer_height());
    const auto [width, height] = compute_sixel_dimensions(panel_width, body_rows, cell_width,
                                                          cell_height, source_width, source_height);
    const int image_cols = std::max(1, (width + cell_width - 1) / cell_width);
    const int image_rows = std::max(1, (height + cell_height - 1) / cell_height);
    const int image_col = panel_x + std::max(0, (panel_width - image_cols) / 2);
    const int image_row = 5 + std::max(0, (body_rows - image_rows) / 2);
    // Pixel reports are absolute terminal pixels. Sixel placement starts at a character
    // cell, whose physical pitch can differ slightly from the cell-size query used to
    // choose the raster dimensions. Anchor the raster from the terminal's full pixel
    // geometry so that any pitch discrepancy does not accumulate across the image.
    if (term_pixel_width <= 0 || term_pixel_height <= 0) {
        struct winsize w{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
            term_pixel_width = w.ws_xpixel;
            term_pixel_height = w.ws_ypixel;
        }
    }
    const auto pixel_geometry = [](int screen_pixels, int cells, int cell_pixels, int image_cell) {
        double pitch = static_cast<double>(cell_pixels);
        double leading_padding = 0.0;
        if (screen_pixels > 0 && cells > 0) {
            const double measured_pitch = static_cast<double>(screen_pixels) / cells;
            const double integral_pitch = std::round(measured_pitch);
            // Some terminals have fractional effective cell pitches (for example under
            // fractional display scaling). Rounding that pitch to an integer introduces a
            // small error per cell which grows into visible pointer drift across the Sixel
            // raster. Keep fractional pitches unless the average is close enough to an
            // integer that the difference is plausibly just a few pixels of window padding.
            const double padding = static_cast<double>(screen_pixels) - integral_pitch * cells;
            const bool integral_grid = std::abs(measured_pitch - integral_pitch) <= 0.02;
            if (integral_grid && padding >= 0.0 && padding <= 2.0 * integral_pitch) {
                pitch = integral_pitch;
                leading_padding = padding / 2.0;
            } else {
                // The full window geometry is the best available scale estimate when the
                // queried cell size or integer-pitch assumption does not match the renderer.
                pitch = measured_pitch;
            }
        }
        return leading_padding + static_cast<double>(image_cell - 1) * pitch;
    };
    const double raster_origin_x =
        pixel_geometry(term_pixel_width, term_width, cell_width, image_col);
    const double raster_origin_y =
        pixel_geometry(term_pixel_height, term_height, cell_height, image_row);
    // Raster dimensions are exact screen pixels for the sixel graphic. Map the distance
    // from the image origin directly into raster pixels without artificial scale drift.
    const int raster_x = static_cast<int>(std::floor((x - 1) - raster_origin_x));
    const int raster_y = static_cast<int>(std::floor((y - 1) - raster_origin_y));
    if (raster_x < 0 || raster_x >= width || raster_y < 0 || raster_y >= height)
        return std::nullopt;
    const int fb_x = std::clamp(raster_x * source_width / width, 0, source_width - 1);
    const int fb_y = std::clamp(raster_y * source_height / height, 0, source_height - 1);
    return std::make_pair(fb_x, fb_y);
}

void Tui::handle_display_mouse(int x, int y, int b, size_t col_idx, int term_width, int term_height,
                               int raw_x, int raw_y) {
    if (b == 64 || b == 65) {
        machine_.send_input_mouse_wheel(b == 64 ? 1 : -1);
        return;
    }
    auto const fb_pos = display_mouse_pixel_mode_ && raw_x > 0 && raw_y > 0
                            ? display_pixels_to_fb(raw_x, raw_y, col_idx, term_width, term_height)
                            : display_coords_to_fb(x, y, col_idx, term_width, term_height);
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

void Tui::enable_display_pixel_mouse() {
    if (display_mouse_pixel_mode_ || !sixel_supported_ || !sixel_rendered_) return;
    write_all(STDOUT_FILENO, "\033[?1016h");
    display_mouse_pixel_mode_ = true;
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

    const auto mission_revision = mission_.revision();
    if (mission_revision != mission_revision_seen_) {
        mission_revision_seen_ = mission_revision;
        if (get_active_modal() == ModalType::None && mission_.enabled() &&
            mission_.checkpoint_reached()) {
            const auto progress = mission_.checkpoint_progress();
            const auto symbol = progress ? std::string_view(progress->symbol) : std::string_view{};
            const auto msg = progress ? std::string_view(progress->message) : std::string_view{};
            if (!msg.empty()) {
                set_status_override(std::format(
                    "Checkpoint '{}' after step {} reached: {}  [n] Next step  [F12] Objective",
                    symbol, progress->after_step, msg));
            } else if (!symbol.empty()) {
                set_status_override(
                    std::format("Checkpoint '{}' after step {} reached. Inspect workbench, then "
                                "press [n] Next step or [F12] Objective.",
                                symbol, progress ? progress->after_step : mission_.step() + 1));
            }
        }
    }

    std::string local_tx;
    std::queue<simrv::tui::LogEntry> local_log;
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
    const auto min_interval_us = 1'000'000U / kTuiTargetFps;
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
    refresh_mission_layout_notice();
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
    const bool display_too_small =
        has_display && (display_col < col_widths.count) &&
        (col_widths.widths[display_col] < kMinDisplayWidth || num_rows - 1 < kMinDisplayBodyRows);
    const bool draw_sixel =
        sixel_supported_ && has_display && !display_too_small && !modal_.is_active();
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

    render_build_lines(inspector_width, console_width, term_content_rows, num_rows, panel_mode);
    lock.unlock();

    framework::ScopedRenderContext status_context(
        {.width = term_width,
         .height = term_height,
         .density = framework::resolve_density(density_mode_, term_width, term_height)});
    std::vector<std::string> new_lines = compose_multi_frame_lines(
        frame, term_width, col_widths, status_bar_->render_row(0, term_width),
        status_bar_->render_row(1, term_width),
        [this, total_cols = col_widths.count, multi_headers, num_rows, term_content_rows](
            size_t col_idx, int row, int width) -> std::string {
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
                    if (multi_headers && row == 0) {
                        return inspector_pane_->render_column_header(static_cast<int>(col_idx),
                                                                     "Display", is_focused, width,
                                                                     "", true, total_cols > 1);
                    }
                    const int total_content_rows =
                        multi_headers ? std::max(1, num_rows - 1) : num_rows;
                    const int body_row = multi_headers ? row - 1 : row;
                    if (width < kMinDisplayWidth || total_content_rows < kMinDisplayBodyRows) {
                        const std::string_view title_text =
                            (width >= 27) ? "[ Display space too small ]" : "[ Too small ]";
                        const std::string_view hint_text =
                            (width >= 30) ? "(expand panel: ']' or resize)" : "(resize panel)";
                        const int mid_row = total_content_rows / 2;
                        if (total_content_rows == 1) {
                            if (body_row == 0) {
                                return center_pane_text(
                                    std::format("\033[33m{}\033[0m", title_text), width);
                            }
                        } else {
                            if (body_row == mid_row - 1) {
                                return center_pane_text(
                                    std::format("\033[33m{}\033[0m", title_text), width);
                            }
                            if (body_row == mid_row) {
                                return center_pane_text(std::format("\033[90m{}\033[0m", hint_text),
                                                        width);
                            }
                        }
                        return std::string(static_cast<size_t>(std::max(0, width)), ' ');
                    }
                    if (!sixel_supported_) {
                        const std::string_view unsupp_text =
                            (width >= 30)   ? "[ Sixel graphics unsupported ]"
                            : (width >= 21) ? "[ Sixel unsupported ]"
                                            : "[ No Sixel ]";
                        const int mid_row = total_content_rows / 2;
                        if (body_row == mid_row) {
                            return center_pane_text(std::format("\033[33m{}\033[0m", unsupp_text),
                                                    width);
                        }
                        return std::string(static_cast<size_t>(std::max(0, width)), ' ');
                    }
                    return std::string(static_cast<size_t>(std::max(0, width)), ' ');
                }
                inspector_pane_->set_page(page);
                framework::ScopedRenderContext panel_context(
                    {.width = width,
                     .height = term_content_rows,
                     .density =
                         framework::resolve_density(density_mode_, width, term_content_rows)});
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
    int const target_width = (col_width > 0) ? col_width : pane_width_cached_;
    auto const drawer_hit = inspector_pane_ ? inspector_pane_->drawer_at_terminal_row(
                                                  y, target_width, num_rows, is_secondary)
                                            : std::nullopt;

    if (b == 0) {
        if (drawer_hit.has_value()) {
            if (drawer_hit->id == TuiDrawerId::Mission) {
                modal_.open_mission_objective();
                render(true);
            } else if (drawer_hit->id == TuiDrawerId::Log &&
                       drawer_hit->is_header_row(y - kInspectorContentStartRow)) {
                inspector_pane_->reset_log_scroll();
                render(true);
            }
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
                simrv::xlen::Register clicked_pc =
                    inspector_pane_->get_pipeline_pc_at_row(logical_row);
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
    } else if (b == 64 || b == 65) {
        if (drawer_hit.has_value()) {
            if (drawer_hit->id == TuiDrawerId::Log) {
                inspector_pane_->scroll_log(b == 64 ? 2 : -2);
                render(true);
            }
        } else {
            scroll_inspector(b == 64 ? -2 : 2);
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

void Tui::handle_mouse(int x, int y, int b, int raw_x, int raw_y) {
    if (!terminal_pane_ || !inspector_pane_) return;

    struct winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int const term_width =
        (cached_term_width_ > 0) ? cached_term_width_ : (w.ws_col > 0 ? w.ws_col : 80);
    int const term_height =
        (cached_term_height_ > 0) ? cached_term_height_ : (w.ws_row > 0 ? w.ws_row : 24);

    auto const col_widths = column_widths(term_width);
    if (col_widths.count == 0) return;

    if (y < 4) {
        // Header clicks: row 2 is the actual header text row; rows 1 and 3 are top border and
        // divider
        if (b == 0 && status_bar_ && y == 2) {
            auto hit = status_bar_->get_header_action_at_col(x, term_width);
            execute_header_action(hit);
        }
        return;
    }

    const auto column_hit = locate_panel_column(x, col_widths);
    if (!column_hit) return;
    const size_t clicked_col = column_hit->index;
    const int col_local_x = column_hit->local_x;
    const size_t previous_focused_slot = focused_slot_index_;

    if (b == 0 && clicked_col < workbench_slots_.size() && y != 4) {
        focused_slot_index_ = clicked_col;
        update_trace_active_cache();
    }

    // Body clicks (y >= 4)
    if (clicked_col < workbench_slots_.size()) {
        auto page = workbench_slots_[clicked_col].page;
        if (b == 64 || b == 65) {
            focused_slot_index_ = clicked_col;
            if (page == TuiRegPage::CONSOLE) {
                scroll(b == 64 ? 5 : -5);
            } else if (page == TuiRegPage::DISPLAY) {
                machine_.send_input_mouse_wheel(b == 64 ? 1 : -1);
                return;
            } else {
                inspector_pane_->set_page(page);
                int const num_rows = std::max(1, term_height - framework::kFrameChromeRows);
                int const col_w = col_widths.widths[clicked_col];
                bool const is_secondary = clicked_col > 0;
                auto const drawer_hit =
                    inspector_pane_->drawer_at_terminal_row(y, col_w, num_rows, is_secondary);
                if (drawer_hit.has_value()) {
                    if (drawer_hit->id == TuiDrawerId::Log) {
                        inspector_pane_->scroll_log(b == 64 ? 2 : -2);
                        render(true);
                    }
                } else {
                    inspector_pane_->scroll(b == 64 ? -framework::kDefaultWheelVerticalStep
                                                    : framework::kDefaultWheelVerticalStep,
                                            col_w, is_secondary);
                    if (clicked_col < workbench_slots_.size()) {
                        workbench_slots_[clicked_col].scroll_offset =
                            inspector_pane_->get_scroll_offset();
                    }
                    render(true);
                }
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
            handle_display_mouse(x, y, b, clicked_col, term_width, term_height, raw_x, raw_y);
            if (b == 0 || b == 1 || b == 2) enable_display_pixel_mouse();
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

auto Tui::is_clickable_at(int x, int y, int term_width, int term_height) const -> bool {
    if (is_modal_active()) return modal_.is_control_at(x, y);
    if (y == 2 && status_bar_) {
        return status_bar_->get_header_action_at_col(x, term_width).action != HeaderAction::None ||
               status_bar_->is_pos_on_status_badge(x, term_width);
    }
    if (status_bar_ && (y == term_height - 2 || y == term_height - 1)) {
        const int row = y == term_height - 2 ? 0 : 1;
        if (status_bar_->get_footer_action_at_col(x - 2, row, term_width).has_value()) return true;
    }

    const auto columns = column_widths(term_width);
    const auto column_hit = locate_panel_column(x, columns);
    if (!column_hit || column_hit->index >= workbench_slots_.size()) return false;
    const size_t column = column_hit->index;
    if (columns.count > 1 && y == 4) return true;
    if (columns.count == 1 && inspector_pane_ && (y == 4 || y == 5)) {
        return inspector_pane_->get_tab_at(y == 4 ? 0 : 1, x - 2).has_value() || y == 4;
    }
    if (!inspector_pane_ || workbench_slots_[column].page == TuiRegPage::DISPLAY ||
        workbench_slots_[column].page == TuiRegPage::CONSOLE) {
        return false;
    }
    const int content_start_y = columns.count > 1 ? 5 : 6;
    if (y < content_start_y) return false;
    inspector_pane_->set_page(workbench_slots_[column].page);
    int const num_rows = std::max(1, term_height - framework::kFrameChromeRows);
    if (auto const drawer_hit = inspector_pane_->drawer_at_terminal_row(y, columns.widths[column],
                                                                        num_rows, column > 0)) {
        return drawer_hit->id == TuiDrawerId::Mission ||
               (drawer_hit->id == TuiDrawerId::Log &&
                drawer_hit->is_header_row(y - kInspectorContentStartRow));
    }
    if (!paused_) return false;
    const int logical_row = y - content_start_y + inspector_pane_->get_scroll_offset();
    switch (workbench_slots_[column].page) {
        case TuiRegPage::CACHE:
            return logical_row == 0 || logical_row == 4;
        case TuiRegPage::PIPELINE:
            return inspector_pane_->get_pipeline_pc_at_row(logical_row) != 0;
        case TuiRegPage::EXPLAIN:
            return logical_row <= 1;
        case TuiRegPage::GPR:
        case TuiRegPage::FPR:
            return inspector_pane_
                ->get_register_value_at_row(logical_row, x, columns.widths[column])
                .has_value();
        case TuiRegPage::STACK:
            return inspector_pane_->get_stack_addr_at_row(logical_row).has_value();
        default:
            return false;
    }
}

void Tui::update_pointer_shape(bool clickable) {
    if (!osc22_pointer_supported_ || pointer_is_clickable_ == clickable) return;
    pointer_is_clickable_ = clickable;
    std::string sequence = clickable ? "\033]22;pointer\033\\" : "\033]22;default\033\\";
    write_all(STDOUT_FILENO, sequence);
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
        workbench_slots_.push_back({.page = TuiRegPage::GPR, .scroll_offset = 0});
    }

    if (desired == 1) {
        if (layout_ == TuiLayout::FullRight) {
            workbench_slots_ = {{.page = TuiRegPage::CONSOLE, .scroll_offset = 0}};
        } else {
            workbench_slots_ = {{.page = TuiRegPage::GPR, .scroll_offset = 0}};
        }
    } else if (desired == 2) {
        if (workbench_slots_.size() < 2) {
            workbench_slots_.push_back({.page = TuiRegPage::CONSOLE, .scroll_offset = 0});
        } else if (workbench_slots_.size() > 2) {
            workbench_slots_.resize(2);
        }
    } else if (desired == 3) {
        if (workbench_slots_.size() < 3) {
            if (workbench_slots_.size() == 1) {
                workbench_slots_.push_back({.page = TuiRegPage::STACK, .scroll_offset = 0});
                workbench_slots_.push_back({.page = TuiRegPage::CONSOLE, .scroll_offset = 0});
            } else {
                workbench_slots_.insert(workbench_slots_.begin() + 1,
                                        {.page = TuiRegPage::STACK, .scroll_offset = 0});
            }
        } else if (workbench_slots_.size() > 3) {
            workbench_slots_.resize(3);
        }
    } else if (desired == 4) {
        if (workbench_slots_.size() == 1) {
            workbench_slots_ = {{.page = TuiRegPage::GPR, .scroll_offset = 0},
                                {.page = TuiRegPage::STACK, .scroll_offset = 0},
                                {.page = TuiRegPage::TRACE, .scroll_offset = 0},
                                {.page = TuiRegPage::CONSOLE, .scroll_offset = 0}};
        } else if (workbench_slots_.size() == 2) {
            auto last = workbench_slots_.back();
            workbench_slots_.pop_back();
            workbench_slots_.push_back({.page = TuiRegPage::STACK, .scroll_offset = 0});
            workbench_slots_.push_back({.page = TuiRegPage::TRACE, .scroll_offset = 0});
            workbench_slots_.push_back(last);
        } else if (workbench_slots_.size() == 3) {
            auto last = workbench_slots_.back();
            workbench_slots_.pop_back();
            workbench_slots_.push_back({.page = TuiRegPage::TRACE, .scroll_offset = 0});
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
    uint8_t const max_cols = framework::max_supported_columns(term_width);
    const bool is_cycle = machine_.runtime_profile.is_cycle_mode();
    user_inspector_width_ = -1;
    user_column_widths_ = framework::kNoColumnWidthOverrides;
    resize_history_.clear();

    const auto& info = get_layout_preset_info(preset);
    const auto [pages, count] = info.slots_for(max_cols, is_cycle);
    workbench_slots_.clear();
    workbench_slots_.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        workbench_slots_.push_back({.page = pages[i], .scroll_offset = 0});
    }
    layout_ = layout_for_column_count(count, workbench_slots_.front().page == TuiRegPage::CONSOLE);
    set_status_override(std::string(info.status_notice));

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
    workbench_.focus_next();
    update_trace_active_cache();
    set_status_override(std::format("Focused Column {}: {}", focused_slot_index_ + 1,
                                    get_page_name(workbench_slots_[focused_slot_index_].page)));
    render(true);
}

void Tui::focus_prev_slot() {
    workbench_.focus_prev();
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
    workbench_.focus(slot_idx);
    auto const next = inspector_pane_->next_page_for_slot(workbench_slots_[slot_idx].page,
                                                          machine_.runtime_profile.is_cycle_mode());
    set_workbench_slot_page(slot_idx, next);
}

void Tui::open_tool_picker(size_t slot_idx) {
    if (workbench_slots_.empty()) return;
    if (slot_idx >= workbench_slots_.size()) slot_idx = 0;
    workbench_.focus(slot_idx);
    modal_.open_tool_picker(static_cast<int>(slot_idx), workbench_slots_[slot_idx].page,
                            static_cast<int>(workbench_slots_.size()));
    render(true);
}

void Tui::swap_workbench_slots(size_t slot_a, size_t slot_b) {
    if (!workbench_.swap(slot_a, slot_b)) {
        return;
    }
    if (inspector_pane_) {
        inspector_pane_->set_page(workbench_slots_[focused_slot_index_].page);
    }
    resize_history_.clear();
    update_trace_active_cache();
    set_status_override(std::format("Swapped Column {} and Column {}", slot_a + 1, slot_b + 1));
    render(true);
}

void Tui::move_focused_column_left() {
    if (!workbench_.move_left(workbench_.focused_index())) return;
    if (inspector_pane_) {
        inspector_pane_->set_page(workbench_slots_[focused_slot_index_].page);
    }
    resize_history_.clear();
    update_trace_active_cache();
    set_status_override(std::format("Moved Column to Position {}", focused_slot_index_ + 1));
    render(true);
}

void Tui::move_focused_column_right() {
    if (!workbench_.move_right(workbench_.focused_index())) return;
    if (inspector_pane_) {
        inspector_pane_->set_page(workbench_slots_[focused_slot_index_].page);
    }
    resize_history_.clear();
    update_trace_active_cache();
    set_status_override(std::format("Moved Column to Position {}", focused_slot_index_ + 1));
    render(true);
}

auto Tui::add_workbench_column() -> bool {
    if (!workbench_.can_add()) {
        set_status_override("Maximum columns reached (4 columns max)");
        render(true);
        return false;
    }

    const size_t target_count = workbench_.size() + 1;
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

    const std::array<TuiRegPage, 6> candidates = {TuiRegPage::CONSOLE, TuiRegPage::STACK,
                                                  TuiRegPage::TRACE,   TuiRegPage::PIPELINE,
                                                  TuiRegPage::CACHE,   TuiRegPage::GPR};
    TuiRegPage new_page = TuiRegPage::CONSOLE;
    for (auto c : candidates) {
        bool already_used = false;
        for (const auto& slot : workbench_.slots()) {
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

    size_t const insert_at = std::min(workbench_.focused_index() + 1, workbench_.size());
    workbench_.insert(insert_at, {.page = new_page, .scroll_offset = 0});
    layout_ = layout_for_column_count(target_count);
    user_inspector_width_ = -1;
    user_column_widths_ = framework::kNoColumnWidthOverrides;
    resize_history_.clear();

    if (inspector_pane_ && new_page != TuiRegPage::CONSOLE) {
        inspector_pane_->set_page(new_page);
    }
    update_trace_active_cache();
    set_status_override(
        std::format("Added Column {}: {}", workbench_.size(), get_page_name(new_page)));
    request_full_screen_redraw();
    render(true);
    return true;
}

auto Tui::close_column(size_t slot_idx) -> bool {
    if (!workbench_.can_close()) {
        set_status_override("Cannot close the last remaining column");
        render(true);
        return false;
    }
    if (slot_idx >= workbench_.size()) {
        return false;
    }

    const auto closed_page = workbench_[slot_idx].page;
    workbench_.close(slot_idx);

    layout_ = layout_for_column_count(workbench_.size(), workbench_[0].page == TuiRegPage::CONSOLE);
    user_inspector_width_ = -1;
    user_column_widths_ = framework::kNoColumnWidthOverrides;
    resize_history_.clear();

    if (inspector_pane_ && workbench_.focused_slot().page != TuiRegPage::CONSOLE) {
        inspector_pane_->set_page(workbench_.focused_slot().page);
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
    const auto misa = machine_.primary_hart().state().misa;
    const bool has_fp = (misa & ((1ULL << ('f' - 'a')) | (1ULL << ('d' - 'a')))) != 0;
    const bool has_v = (misa & (1ULL << ('v' - 'a'))) != 0;
    set_reg_page(cycle_page_in_group(focused_page(), machine_.runtime_profile.is_cycle_mode(),
                                     has_fp, has_v, reverse,
                                     /*use_reg_hotkey_cycle=*/true));
}

void Tui::cycle_tool_page(bool reverse) {
    const TuiCategoryGroup next_grp =
        cycle_category_group(get_category_group(focused_page()), reverse);
    set_reg_page(get_default_page_for_group(next_grp, machine_.runtime_profile.is_cycle_mode()));
}

void Tui::set_reg_page(TuiRegPage page) {
    if (page == TuiRegPage::LIVE) page = TuiRegPage::GPR;
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
    std::optional<MissionEvidence> mission_evidence;
    if (!mission_.lesson_id().empty()) {
        auto reached = mission_.reached_checkpoint_events();
        auto checkpoint_states = mission_.checkpoints_progress();
        std::vector<MissionEvidence::CheckpointDetail> details;
        details.reserve(reached.size());
        for (const auto& symbol : reached) {
            const auto checkpoint =
                std::ranges::find(checkpoint_states, symbol, &MissionCheckpointProgress::symbol);
            details.push_back({symbol, checkpoint == checkpoint_states.end()
                                           ? std::string{}
                                           : checkpoint->message});
        }
        const auto step_count = mission_.step_count();
        const auto current_step =
            mission_.completed() ? step_count : std::min(mission_.step() + 1, step_count);
        mission_evidence = MissionEvidence{.lesson_id = std::string(mission_.lesson_id()),
                                           .completed = mission_.completed(),
                                           .step = current_step,
                                           .step_count = step_count,
                                           .checkpoint_count = mission_.checkpoint_count(),
                                           .reached_checkpoints = std::move(reached),
                                           .checkpoint_details = std::move(details)};
    }
    const std::string report = make_inspection_report(
        machine_, selected_hart_, trace_buffer_, mission_evidence ? &*mission_evidence : nullptr);
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
    // The display inspector is the explicit keyboard target for VGA guests. Every other focused
    // panel keeps the running guest's UART attached, so selecting a debugger view does not
    // silently discard terminal input.
    if (page != TuiRegPage::DISPLAY) {
        if (machine_.uart_device()) {
            machine_.uart_device()->push_rx_byte(normalize_guest_terminal_byte(byte));
        }
        return;
    }

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

    bool detail_visible = false;
    for (const auto& page : kTuiPages) {
        if (page.captures_execution_detail && is_page_visible(page.page)) {
            detail_visible = true;
            break;
        }
    }
    pipeline_or_detail_visible_.store(detail_visible, std::memory_order_release);
}

void Tui::record_instruction(simrv::xlen::Register pc, simrv::isa::Opcode opcode,
                             simrv::isa::OperationId op_id, uint8_t rd,
                             simrv::xlen::Register rd_val, uint8_t rs1,
                             simrv::xlen::Register rs1_val, uint8_t rs2,
                             simrv::xlen::Register rs2_val, int64_t imm, uint8_t hart) {
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

void Tui::record_flight_instruction(simrv::xlen::Register pc, simrv::isa::Opcode opcode,
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
        if (focused_slot_index_ < workbench_slots_.size()) {
            workbench_slots_[focused_slot_index_].scroll_offset =
                inspector_pane_->get_scroll_offset();
        }
        render();
        frame_dirty_ = true;
        render(true);
    }
}

void Tui::reset_scroll_inspector() {
    if (inspector_pane_) {
        inspector_pane_->set_page(focused_page());
        inspector_pane_->reset_scroll();
        if (focused_slot_index_ < workbench_slots_.size()) {
            workbench_slots_[focused_slot_index_].scroll_offset = 0;
        }
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

void Tui::apply_mission_speed() {
    if (!mission_.enabled()) return;
    const uint64_t speed_hz = mission_.speed_hz();
    const uint64_t delay_us = speed_hz == 0 ? 0 : 1 + (999'999 / speed_hz);
    step_delay_us_.store(delay_us, std::memory_order_relaxed);
    reset_speed_history();
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
        pollfd fd{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
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

auto Tui::handle_modal_keyboard_input(uint8_t byte, TuiKey key) -> bool {
    if (!is_modal_active()) return false;

    if (key == simrv::tui::TuiKey::F10) {
        machine_.request_exit();
        close_modal();
        return true;
    }

    if (get_active_modal() == ModalType::MissionObjective) {
        if (byte == 'y' || byte == 'Y') {
            modal_.cycle_mission_hint();
            render(true);
            return true;
        }
        if (key == TuiKey::F9 || key == TuiKey::QuestionMark) {
            if (const auto guidance = mission_.guidance())
                modal_.set_glossary_topic(guidance->glossary_topic);
            open_modal(ModalType::Glossary);
            return true;
        }
        if (key == TuiKey::Enter || key == TuiKey::Newline) {
            close_modal();
            return true;
        }
        if (key == TuiKey::F5) {
            if (mission_.completed()) return true;
            if (mission_.checkpoint_reached()) {
                set_status_override(
                    "Checkpoint reached. Press Space or n for the next instruction.");
                render(true);
                return true;
            }
            if (const auto checkpoint = mission_.checkpoint_progress();
                checkpoint && !machine_.binary_path().empty() &&
                !machine_.symbol_table().lookup_name(checkpoint->symbol)) {
                set_status_override(
                    "Cannot run: this lesson checkpoint is missing from the "
                    "loaded program. Ask your instructor to load the matching "
                    "program file.");
                render(true);
                return true;
            }
            close_modal();
            execute_footer_action(TuiFooterAction::RunPause);
            return true;
        }
        if (key == TuiKey::F6) {
            if (mission_.completed()) return true;
            close_modal();
            execute_footer_action(TuiFooterAction::Step);
            return true;
        }
        if (byte == ' ' || byte == 'n' || byte == 'N') {
            if (mission_.completed()) return true;
            (void)advance_mission_step();
            render(true);
            return true;
        }
        if (byte == 'p' || byte == 'P' || byte == 'b' || byte == 'B' || key == TuiKey::Backspace ||
            byte == 8 || byte == 127) {
            (void)previous_mission_step();
            render(true);
            return true;
        }
        if (key == TuiKey::F12) {
            return true;
        }
        if (byte == 'r' || byte == 'R') {
            restart_mission();
            return true;
        }
        if (!mission_.completed() && (byte == 'd' || byte == 'D')) {
            dismiss_mission();
            return true;
        }
        if (mission_.completed() && (byte == 'q' || byte == 'Q' || key == TuiKey::F10)) {
            close_modal();
            machine_.request_exit();
            return true;
        }
        if (key == TuiKey::Esc || byte == 27) {
            close_modal();
            return true;
        }
        return true;
    }

    if (auto* impl = modal_.active_modal_impl()) {
        const auto active_type = get_active_modal();
        auto action = impl->handle_key(byte, key);
        if (action == framework::ModalAction::Close) {
            close_modal();
        } else if (action == framework::ModalAction::Submit) {
            if (active_type == ModalType::LayoutPresets) {
                apply_layout_preset(modal_.get_selected_preset());
                close_modal();
            } else {
                submit_modal();
            }
        } else if (action == framework::ModalAction::ReloadRequested) {
            close_modal();
            if (active_type == ModalType::ConfigurationChangeConfirm) {
                auto staged = modal_.apply_pending_reconfiguration();
                if (!staged) {
                    modal_.open_notice("CONFIGURATION RELOAD FAILED", staged.error(), true);
                }
            } else {
                const auto& draft = modal_.get_pending_platform_draft();
                auto next = machine_.configuration();
                next.platform_profile =
                    static_cast<simrv::core::PlatformProfile>(draft.platform_profile);
                next.network.mode = draft.net_mode;
                (void)machine_.stage_reconfiguration(std::move(next));
                modal_.open_notice(
                    "SIMULATOR RELOADED",
                    "Platform profile applied and simulation restarted with new Device Tree.",
                    false);
            }
        } else if (action == framework::ModalAction::DiscardRequested) {
            close_modal();
            if (active_type == ModalType::ConfigurationChangeConfirm) {
                modal_.open_notice("CONFIGURATION CHANGE DISCARDED",
                                   "The active simulator configuration remains unchanged.", false);
            } else {
                modal_.open_notice(
                    "PLATFORM CHANGE DISCARDED",
                    "Platform profile change was discarded and previous topology retained.", false);
            }
        }
        render(true);
        return true;
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

auto Tui::handle_normal_keyboard_input(uint8_t byte, TuiKey key) -> void {
    if (terminal_search_active_) {
        if (byte == 27 || key == TuiKey::Esc) {
            terminal_search_active_ = false;
            terminal_search_query_.clear();
            clear_status_override();
        } else if (key == TuiKey::Enter || key == TuiKey::Newline) {
            terminal_search_active_ = false;
            terminal_search_cursor_ = vt_.get_lines_count();
            if (!search_terminal_scrollback(true)) {
                set_status_override("NO MATCH IN UART SCROLLBACK");
            }
        } else if (byte == 8 || byte == 127 || key == TuiKey::Backspace) {
            if (!terminal_search_query_.empty()) terminal_search_query_.pop_back();
        } else if (byte >= 32 && byte <= 126) {
            terminal_search_query_.push_back(static_cast<char>(byte));
        }
        if (terminal_search_active_) {
            set_status_override(
                std::format("SEARCH UART: {}  [Enter] Find [Esc] Cancel", terminal_search_query_));
        }
        render(true);
        return;
    }
    if ((byte == 0x06 || key == TuiKey::CtrlF || byte == '/') &&
        focused_page() == TuiRegPage::CONSOLE) {
        terminal_search_active_ = true;
        terminal_search_query_.clear();
        set_status_override("SEARCH UART: type text, [Enter] Find, [Esc] Cancel");
        render(true);
        return;
    }
    if ((byte == 'n' || byte == 'N') && !terminal_search_query_.empty() &&
        focused_page() == TuiRegPage::CONSOLE) {
        if (!search_terminal_scrollback(byte == 'N')) {
            set_status_override("NO MORE UART MATCHES");
            render(true);
        }
        return;
    }
    if (selection_.is_active && (byte == 3 || byte == 'y')) {
        copy_active_selection();
        clear_selection();
        return;
    }

    if (mission_.enabled() || mission_.completed()) {
        if ((byte == 'n' || byte == 'N') && machine_.num_harts() <= 1) {
            if (advance_mission_step() && mission_.guidance()) {
                modal_.open_mission_objective();
            }
            render(true);
            return;
        }
        if ((byte == 'p' || byte == 'P') && mission_.can_previous_step()) {
            if (previous_mission_step() && mission_.guidance()) {
                modal_.open_mission_objective();
            }
            render(true);
            return;
        }
        if (mission_.completed() && (byte == 'r' || byte == 'R')) {
            restart_mission();
            return;
        }
    }

    if (const auto action = Keybindings::match(key)) {
        const auto checkpoint = mission_.checkpoint_progress();
        const bool checkpoint_symbol_missing =
            checkpoint && !checkpoint->reached && !machine_.binary_path().empty() &&
            !machine_.symbol_table().lookup_name(checkpoint->symbol);
        if (*action == KeyAction::RunPause && checkpoint_symbol_missing) {
            set_status_override(
                "Cannot run: this lesson checkpoint is missing from the loaded "
                "program. Ask your instructor to load the matching program file.");
            render(true);
            return;
        }
        if (*action == KeyAction::RunPause &&
            (mission_.completed() || mission_.checkpoint_reached())) {
            set_status_override(
                mission_.completed()
                    ? "Mission complete. Review, restart with r, or exit with q."
                    : "Checkpoint reached. Press n for the next instruction before running again.");
            render(true);
            return;
        }
        execute_key_action(*action);
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
    if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) {
        reset_scroll();
        return;
    }
    if (key == simrv::tui::TuiKey::CtrlC && focused_page() == TuiRegPage::DISPLAY) {
        write_guest_input(3);
        return;
    }
    if (key == simrv::tui::TuiKey::CtrlC) {
        machine_.request_exit();
        return;
    }

    if (handle_speed_keyboard_input(key)) return;

    if (key == simrv::tui::TuiKey::CtrlL) {
        if (inspector_pane_) {
            inspector_pane_->toggle_log_drawer();
            render(true);
        }
        return;
    }
}

auto Tui::search_terminal_scrollback(bool reverse) -> bool {
    if (terminal_search_query_.empty()) return false;
    const int line_count = vt_.get_lines_count();
    if (line_count <= 0) return false;
    int line = terminal_search_cursor_;
    if (line < 0 || line >= line_count)
        line = reverse ? line_count - 1 : 0;
    else if (reverse)
        --line;
    else
        ++line;
    const int width = std::max(1, cached_term_width_);
    while (reverse ? line >= 0 : line < line_count) {
        const auto text = vt_.get_text_in_range(line, 0, line, width - 1);
        if (text.find(terminal_search_query_) != std::string::npos) {
            terminal_search_cursor_ = line;
            scroll_offset_ = std::max(0, line_count - line - 1);
            set_status_override(
                std::format("UART MATCH at scrollback line {}  [n] Next [N] Previous", line + 1));
            render(true);
            return true;
        }
        line += reverse ? -1 : 1;
    }
    return false;
}

void Tui::observe_current_pc_symbol() {
    if (!mission_.enabled()) return;
    const auto pc = machine_.primary_hart().state().pc;
    // The address installed for the mission checkpoint is authoritative. Symbol lookup by
    // address can return a different alias at the same PC, which would otherwise lose evidence.
    if (mission_breakpoint_addr_ && pc == *mission_breakpoint_addr_) {
        if (const auto progress = mission_.checkpoint_progress())
            mission_.observe_symbol(progress->symbol);
    }
    if (const auto symbol = machine_.symbol_table().lookup_symbol(pc);
        symbol && symbol->is_exact()) {
        mission_.observe_symbol(symbol->name);
    }
    if (mission_.revision() != mission_revision_seen_) {
        mission_revision_seen_ = mission_.revision();
        if (get_active_modal() == ModalType::None && mission_.enabled() &&
            mission_.checkpoint_reached()) {
            const auto progress = mission_.checkpoint_progress();
            const auto symbol = progress ? std::string_view(progress->symbol) : std::string_view{};
            const auto msg = progress ? std::string_view(progress->message) : std::string_view{};
            if (!msg.empty()) {
                set_status_override(std::format(
                    "Checkpoint '{}' after step {} reached: {}  [n] Next step  [F12] Objective",
                    symbol, progress->after_step, msg));
            } else if (!symbol.empty()) {
                set_status_override(
                    std::format("Checkpoint '{}' after step {} reached. Inspect workbench, then "
                                "press [n] Next step or [F12] Objective.",
                                symbol, progress ? progress->after_step : mission_.step() + 1));
            }
        }
    }
}

auto Tui::advance_mission_step() -> bool {
    if (mission_.completed()) return false;
    if (mission_.advance_step()) {
        apply_mission_speed();
        sync_mission_checkpoint();
        observe_current_pc_symbol();
        mission_revision_seen_ = mission_.revision();
        if (mission_.completed()) {
            apply_mission_guidance_layout();
            set_status_override("Mission complete. Review the result, or restart with r.");
        } else if (mission_.guidance()) {
            apply_mission_guidance_layout();
            set_status_override("Next instruction. Checkpoint status follows the guest program.");
        }
        return true;
    }
    set_status_override(mission_.checkpoint_symbol().empty()
                            ? "No further instruction is available."
                            : "Checkpoint not reached. Follow the instruction, then continue.");
    return false;
}

auto Tui::previous_mission_step() -> bool {
    if (!mission_.previous_step()) return false;
    apply_mission_speed();
    sync_mission_checkpoint();
    observe_current_pc_symbol();
    mission_revision_seen_ = mission_.revision();
    if (mission_.guidance()) {
        apply_mission_guidance_layout();
        set_status_override(
            std::format("Returned to step {} of {}.", mission_.step() + 1, mission_.step_count()));
    }
    return true;
}

void Tui::clear_trace_buffers() {
    for (auto& ring : flight_rings_) {
        ring.write_sequence.store(0, std::memory_order_relaxed);
    }
    flight_sequence_.store(0, std::memory_order_relaxed);
    rendered_flight_sequences_.fill(0);
    trace_buffer_.clear();
}

void Tui::pause_loop() {
    observe_current_pc_symbol();
    set_paused(true);
    update_cache();
    render(true);
}

void Tui::unpause_loop() { set_paused(false); }

void Tui::dismiss_mission() {
    mission_.dismiss();
    sync_mission_checkpoint();
    if (get_active_modal() == ModalType::MissionObjective) close_modal();
    set_status_override("Mission dismissed; free exploration is available.");
    render(true);
}

void Tui::restart_mission() {
    if (!is_paused()) pause_loop();
    if (machine_.primary_hart().state().pc != 0 || machine_.resolved_start_pc() != 0 ||
        machine_.is_shutdown_) {
        machine_.reset_state();
        if (machine_.resolved_start_pc() != 0) {
            machine_.primary_hart().state().pc = machine_.resolved_start_pc();
        }
        clear_trace_buffers();
        update_cache();
        if (ui_running_.load(std::memory_order_acquire)) {
            machine_.request_reboot();
        }
    }
    mission_.restart();
    apply_mission_speed();
    sync_mission_checkpoint();
    observe_current_pc_symbol();
    apply_mission_guidance_layout();
    mission_revision_seen_ = mission_.revision();
    if (mission_.guidance()) modal_.open_mission_objective();
    set_status_override("Mission restarted at objective 1.");
    render(true);
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
                observe_current_pc_symbol();
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
            simrv::xlen::Address pc = machine_.primary_hart().state().pc;
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
            open_modal(ModalType::MemoryDump);
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
        case TuiFooterAction::OpenSettings:
            open_modal(ModalType::Settings);
            break;
        case TuiFooterAction::ManageBreakpoints:
            open_modal(ModalType::ManageBreakpoints);
            break;
        case TuiFooterAction::SwitchHart:
            if (machine_.num_harts() > 1) {
                open_modal(ModalType::SwitchHart);
            } else {
                select_next_hart();
            }
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

void Tui::execute_key_action(KeyAction action) {
    switch (action) {
        case KeyAction::Step:
            execute_footer_action(TuiFooterAction::Step);
            break;
        case KeyAction::RunPause:
            execute_footer_action(TuiFooterAction::RunPause);
            break;
        case KeyAction::Reset:
            execute_footer_action(TuiFooterAction::Reboot);
            break;
        case KeyAction::SetBreakpoint:
            execute_footer_action(TuiFooterAction::SetBreakpoint);
            break;
        case KeyAction::SetWatchpoint:
            execute_footer_action(TuiFooterAction::SetWatchpoint);
            break;
        case KeyAction::ManageBreakpoints:
            execute_footer_action(TuiFooterAction::ManageBreakpoints);
            break;
        case KeyAction::TogglePcBreakpoint:
            execute_footer_action(TuiFooterAction::TogglePcBreakpoint);
            break;
        case KeyAction::InspectAddress:
            open_modal(ModalType::InspectAddress);
            break;
        case KeyAction::SetSpeed:
            execute_footer_action(TuiFooterAction::SetSpeed);
            break;
        case KeyAction::LoadBinary:
            execute_footer_action(TuiFooterAction::LoadBinary);
            break;
        case KeyAction::Settings:
            execute_footer_action(TuiFooterAction::OpenSettings);
            break;
        case KeyAction::Help:
            execute_footer_action(TuiFooterAction::ToggleHelp);
            break;
        case KeyAction::Quit:
            execute_footer_action(TuiFooterAction::Quit);
            break;
        case KeyAction::FocusNextPane:
            execute_footer_action(TuiFooterAction::FocusNextPane);
            break;
        case KeyAction::FocusPrevPane:
            execute_footer_action(TuiFooterAction::FocusPrevPane);
            break;
        case KeyAction::CycleRegPage:
            execute_footer_action(TuiFooterAction::CycleRegs);
            break;
        case KeyAction::CycleToolPage:
            execute_footer_action(TuiFooterAction::CycleTools);
            break;
        case KeyAction::ToggleExplain:
            toggle_explain();
            break;
        case KeyAction::ExportInspection:
            export_inspection_report();
            break;
        case KeyAction::SwitchHart:
            execute_footer_action(TuiFooterAction::SwitchHart);
            break;
        case KeyAction::OpenGlossary:
            if (const auto guidance = mission_.guidance()) {
                modal_.set_glossary_topic(guidance->glossary_topic);
            } else if (inspector_pane_) {
                modal_.set_glossary_topic(
                    guidance_for_page(inspector_pane_->get_page(),
                                      machine_.runtime_profile.is_cycle_mode())
                        .glossary_topic);
            }
            open_modal(ModalType::Glossary);
            break;
        case KeyAction::ToggleTheme:
            execute_footer_action(TuiFooterAction::ToggleTheme);
            break;
        case KeyAction::SelectPreset1:
            apply_layout_preset(LayoutPreset::GeneralDebug);
            break;
        case KeyAction::SelectPreset2:
            apply_layout_preset(LayoutPreset::Microarchitecture);
            break;
        case KeyAction::SelectPreset3:
            apply_layout_preset(LayoutPreset::TraceExecution);
            break;
        case KeyAction::SelectPreset4:
            apply_layout_preset(LayoutPreset::MemoryInterconnect);
            break;
        case KeyAction::OpenLayoutPresets:
            open_modal(ModalType::LayoutPresets);
            break;
        case KeyAction::ToggleExecutionMode:
            execute_footer_action(TuiFooterAction::ToggleExecutionMode);
            break;
        case KeyAction::OpenToolPicker:
            execute_footer_action(TuiFooterAction::OpenToolPicker);
            break;
        case KeyAction::MoveColumnLeft:
            execute_footer_action(TuiFooterAction::MoveColumnLeft);
            break;
        case KeyAction::MoveColumnRight:
            execute_footer_action(TuiFooterAction::MoveColumnRight);
            break;
        case KeyAction::AddColumn:
            execute_footer_action(TuiFooterAction::AddColumn);
            break;
        case KeyAction::CloseColumn:
            execute_footer_action(TuiFooterAction::CloseColumn);
            break;
        case KeyAction::DumpMemory:
            execute_footer_action(TuiFooterAction::InspectMem);
            break;
        case KeyAction::OpenLogModal:
            open_modal(ModalType::LogViewer);
            break;
        case KeyAction::CheckMissionObjective:
            if (mission_.guidance()) open_modal(ModalType::MissionObjective);
            break;
        case KeyAction::OpenTracePage:
            set_reg_page(TuiRegPage::TRACE);
            break;
        case KeyAction::SelectPage1:
            set_reg_page(TuiRegPage::GPR);
            break;
        case KeyAction::SelectPage2:
            set_reg_page(TuiRegPage::STACK);
            break;
        case KeyAction::SelectPage3:
            set_reg_page(TuiRegPage::PIPELINE);
            break;
        case KeyAction::SelectPage4:
            set_reg_page(TuiRegPage::EXPLAIN);
            break;
        case KeyAction::NarrowFocusedColumn:
            adjust_inspector_width(-2);
            break;
        case KeyAction::WidenFocusedColumn:
            adjust_inspector_width(2);
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
    if (const auto action = Keybindings::match_alt(key)) {
        execute_key_action(*action);
        return true;
    }
    switch (key) {
        case 'w':
        case 'W':
            scroll_inspector(-framework::kDefaultWheelVerticalStep);
            return true;
        case 's':
        case 'S':
            scroll_inspector(framework::kDefaultWheelVerticalStep);
            return true;
        case 'u':
        case 'U':
            scroll(framework::kDefaultConsoleWheelStep);
            return true;
        case 'd':
        case 'D':
            scroll(-framework::kDefaultConsoleWheelStep);
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
        if (is_modal_active()) {
            modal_.handle_wheel(direction);
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
        if (is_modal_active() && modal_.handle_horizontal(direction)) {
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
        if (is_modal_active()) {
            modal_.handle_wheel(-5);
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
        if (is_modal_active()) {
            modal_.handle_wheel(5);
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
        if (is_modal_active()) {
            modal_.scroll_to_top();
            render(true);
            return true;
        }
        if (focused_page() == TuiRegPage::CONSOLE) {
            scroll(vt_.get_lines_count());
        } else {
            reset_scroll_inspector();
        }
        return true;
    } else if (esc_buf_ == "\033[F" || esc_buf_ == "\033[4~" || esc_buf_ == "\033[8~" ||
               esc_buf_ == "\033OF") {
        if (is_modal_active()) {
            modal_.scroll_to_bottom();
            render(true);
            return true;
        }
        if (focused_page() == TuiRegPage::CONSOLE) {
            reset_scroll();
        } else if (inspector_pane_) {
            auto const widths = column_widths(cached_term_width_ > 0 ? cached_term_width_ : 80);
            auto const focused = std::min(focused_slot_index_,
                                          static_cast<size_t>(std::max<int>(1, widths.count) - 1));
            int const pane_width = widths.count > 0 ? widths.widths[focused] : 0;
            inspector_pane_->set_page(focused_page());
            inspector_pane_->scroll_to_bottom(pane_width, focused > 0);
            if (focused_slot_index_ < workbench_slots_.size()) {
                workbench_slots_[focused_slot_index_].scroll_offset =
                    inspector_pane_->get_scroll_offset();
            }
            render();
            frame_dirty_ = true;
            render(true);
        }
        return true;
    } else if (esc_buf_ == "\033[Z" || esc_buf_ == "\033[1;2I") {
        if (is_modal_active()) {
            return handle_modal_keyboard_input(0, simrv::tui::TuiKey::BackTab);
        }
        // BackTab is registered as FocusPrevPane; pass it through the common action matcher so
        // dispatch, help, and footer metadata all describe the same behavior.
        handle_normal_keyboard_input(0, simrv::tui::TuiKey::BackTab);
        return true;
    } else if (esc_buf_ == "\033OP" || esc_buf_ == "\033[11~" || esc_buf_ == "\033[1;2P" ||
               esc_buf_ == "\033[O1P" || esc_buf_ == "\033[[A") {
        if (is_modal_active())
            (void)handle_modal_keyboard_input(0, simrv::tui::TuiKey::F1);
        else
            handle_normal_keyboard_input(0, simrv::tui::TuiKey::F1);
        return true;
    } else if (esc_buf_ == "\033OQ" || esc_buf_ == "\033[12~" || esc_buf_ == "\033[1;2Q" ||
               esc_buf_ == "\033[O1Q" || esc_buf_ == "\033[[B") {
        if (is_modal_active())
            (void)handle_modal_keyboard_input(0, simrv::tui::TuiKey::F2);
        else
            handle_normal_keyboard_input(0, simrv::tui::TuiKey::F2);
        return true;
    } else if (esc_buf_ == "\033OR" || esc_buf_ == "\033[13~" || esc_buf_ == "\033[1;2R" ||
               esc_buf_ == "\033[O1R" || esc_buf_ == "\033[[C") {
        if (is_modal_active())
            (void)handle_modal_keyboard_input(0, simrv::tui::TuiKey::F3);
        else
            handle_normal_keyboard_input(0, simrv::tui::TuiKey::F3);
        return true;
    } else if (esc_buf_ == "\033OS" || esc_buf_ == "\033[14~" || esc_buf_ == "\033[1;2S" ||
               esc_buf_ == "\033[O1S" || esc_buf_ == "\033[[D") {
        if (is_modal_active())
            (void)handle_modal_keyboard_input(0, simrv::tui::TuiKey::F4);
        else
            handle_normal_keyboard_input(0, simrv::tui::TuiKey::F4);
        return true;
    } else if (esc_buf_ == "\033[15~" || esc_buf_ == "\033[[E") {
        if (is_modal_active())
            (void)handle_modal_keyboard_input(0, simrv::tui::TuiKey::F5);
        else
            handle_normal_keyboard_input(0, simrv::tui::TuiKey::F5);
        return true;
    } else if (esc_buf_ == "\033[17~") {
        if (is_modal_active())
            (void)handle_modal_keyboard_input(0, simrv::tui::TuiKey::F6);
        else
            handle_normal_keyboard_input(0, simrv::tui::TuiKey::F6);
        return true;
    } else if (esc_buf_ == "\033[18~") {
        if (is_modal_active())
            (void)handle_modal_keyboard_input(0, simrv::tui::TuiKey::F7);
        else
            handle_normal_keyboard_input(0, simrv::tui::TuiKey::F7);
        return true;
    } else if (esc_buf_ == "\033[19~") {
        if (is_modal_active())
            (void)handle_modal_keyboard_input(0, simrv::tui::TuiKey::F8);
        else
            handle_normal_keyboard_input(0, simrv::tui::TuiKey::F8);
        return true;
    } else if (esc_buf_ == "\033[20~") {
        if (is_modal_active())
            (void)handle_modal_keyboard_input(0, simrv::tui::TuiKey::F9);
        else
            handle_normal_keyboard_input(0, simrv::tui::TuiKey::F9);
        return true;
    } else if (esc_buf_ == "\033[21~") {
        if (is_modal_active())
            (void)handle_modal_keyboard_input(0, simrv::tui::TuiKey::F10);
        else
            handle_normal_keyboard_input(0, simrv::tui::TuiKey::F10);
        return true;
    } else if (esc_buf_ == "\033[23~") {
        if (is_modal_active())
            (void)handle_modal_keyboard_input(0, simrv::tui::TuiKey::F11);
        else
            handle_normal_keyboard_input(0, simrv::tui::TuiKey::F11);
        return true;
    } else if (esc_buf_ == "\033[24~") {
        if (is_modal_active())
            (void)handle_modal_keyboard_input(0, simrv::tui::TuiKey::F12);
        else
            handle_normal_keyboard_input(0, simrv::tui::TuiKey::F12);
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

    if (esc_buf_ == "\033[200~") {
        constexpr std::string_view end_marker = "\033[201~";
        std::string pasted;
        pasted.reserve(4096);
        uint8_t pasted_byte = 0;
        while (pasted.size() < 8 * 1024 * 1024) {
            if (poll_keyboard(pasted_byte)) {
                pasted.push_back(static_cast<char>(pasted_byte));
                if (pasted.ends_with(end_marker)) {
                    pasted.resize(pasted.size() - end_marker.size());
                    break;
                }
                continue;
            }
            pollfd fd{.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
            if (::poll(&fd, 1, 1000) <= 0) break;
        }
        if (!is_paused() && !is_modal_active()) {
            for (const unsigned char pasted_char : pasted) {
                write_guest_input(normalize_guest_terminal_byte(pasted_char));
            }
        } else {
            set_status_override("RESUME THE GUEST TO PASTE UART INPUT");
        }
        render(true);
        return true;
    }

    struct winsize mouse_size{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &mouse_size);
    const int mouse_width = cached_term_width_ > 0
                                ? cached_term_width_
                                : (mouse_size.ws_col > 0 ? mouse_size.ws_col : 80);
    const int mouse_height = cached_term_height_ > 0
                                 ? cached_term_height_
                                 : (mouse_size.ws_row > 0 ? mouse_size.ws_row : 24);
    framework::ScopedRenderContext mouse_render_context(
        {.width = mouse_width,
         .height = mouse_height,
         .density = framework::resolve_density(density_mode_, mouse_width, mouse_height)});

    // 1. Mouse reporting
    int button = 0;
    int x = 0;
    int y = 0;
    if (parse_sgr_mouse(esc_buf_, button, x, y)) {
        const int raw_x = x;
        const int raw_y = y;
        // Pixel reports are enabled only while dragging on the sixel guest display. Convert them
        // back to cells for ordinary TUI hit testing; the display mapper consumes raw pixels.
        if (display_mouse_pixel_mode_) {
            const int cell_width = std::max(1, cell_width_px_);
            const int cell_height = std::max(1, cell_height_px_);
            x = (raw_x - 1) / cell_width + 1;
            y = (raw_y - 1) / cell_height + 1;
        }
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
                if (display_mouse_pixel_mode_) {
                    write_all(STDOUT_FILENO, "\033[?1016l");
                    display_mouse_pixel_mode_ = false;
                }
            }
            return true;
        }

        // Motion events (button | 32) are generated by ?1003h (all-motion mode) and
        // must be consumed here - never forwarded to the guest UART.
        if (esc_buf_.back() == 'M' && (button & 32) != 0) {
            struct winsize pointer_w{};
            ioctl(STDOUT_FILENO, TIOCGWINSZ, &pointer_w);
            const int pointer_width = cached_term_width_ > 0
                                          ? cached_term_width_
                                          : (pointer_w.ws_col > 0 ? pointer_w.ws_col : 80);
            const int pointer_height = cached_term_height_ > 0
                                           ? cached_term_height_
                                           : (pointer_w.ws_row > 0 ? pointer_w.ws_row : 24);
            update_pointer_shape(is_clickable_at(x, y, pointer_width, pointer_height));
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
                auto fb = display_mouse_pixel_mode_
                              ? display_pixels_to_fb(raw_x, raw_y, display_col, tw, th)
                              : display_coords_to_fb(x, y, display_col, tw, th);
                if (fb) {
                    machine_.send_input_mouse_motion(fb->first, fb->second);
                    display_mouse_last_fb_ = fb;
                    // Cell reports can introduce a growing scale error over a Sixel image
                    // when the terminal's physical cell pitch differs from its size query.
                    // Switch to pixel reports as soon as the pointer enters the image, so
                    // ordinary hover motion uses the same raster mapping as a drag.
                    if (!display_mouse_pixel_mode_) enable_display_pixel_mouse();
                } else if (display_mouse_pixel_mode_ && !display_mouse_capture_.has_value()) {
                    // Pixel reporting is only needed over the Sixel surface. Restore normal
                    // cell reports when the pointer leaves it to keep other TUI hit testing
                    // independent of terminal pixel metrics.
                    write_all(STDOUT_FILENO, "\033[?1016l");
                    display_mouse_pixel_mode_ = false;
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
                    const auto clicked_modal = get_active_modal();
                    auto res = modal_.handle_click(x, y, term_wm, term_hm);
                    if (res == TuiModal::ModalClickResult::Closed) {
                        close_modal();
                    } else if (res == TuiModal::ModalClickResult::ReloadRequested) {
                        close_modal();
                        if (clicked_modal == ModalType::ConfigurationChangeConfirm) {
                            auto staged = modal_.apply_pending_reconfiguration();
                            if (!staged)
                                modal_.open_notice("CONFIGURATION RELOAD FAILED", staged.error(),
                                                   true);
                        } else {
                            auto draft = modal_.get_pending_platform_draft();
                            auto next = machine_.configuration();
                            next.platform_profile = draft.platform_profile == 0
                                                        ? simrv::core::PlatformProfile::Pcie
                                                        : simrv::core::PlatformProfile::Mmio;
                            (void)machine_.stage_reconfiguration(std::move(next));
                        }
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
            int col_local_x = 0;
            int col_w = 0;
            int col_start_x = 0;
            SelectionPane sel_pane = SelectionPane::None;
            const auto selection_column = locate_panel_column(x, sel_cols);
            if (!selection_column) return true;
            sel_col_idx = selection_column->index;
            col_w = sel_cols.widths[sel_col_idx];
            col_start_x = selection_column->screen_start_x;
            col_local_x = selection_column->local_x;
            if (sel_col_idx < workbench_slots_.size() &&
                workbench_slots_[sel_col_idx].page == TuiRegPage::CONSOLE) {
                sel_pane = SelectionPane::TerminalPane;
            } else if (sel_col_idx < workbench_slots_.size() &&
                       workbench_slots_[sel_col_idx].page == TuiRegPage::DISPLAY) {
                // A running DISPLAY column is an interactive guest surface. Do not
                // enter the paused inspector-selection path; route the click directly
                // to the guest's virtio-input device instead.
                focused_slot_index_ = sel_col_idx;
                display_mouse_capture_ = sel_col_idx;
                handle_display_mouse(x, y, button, sel_col_idx, sel_w, term_h, raw_x, raw_y);
                enable_display_pixel_mouse();
                return true;
            } else {
                if (!paused_) return true;
                sel_pane = SelectionPane::InspectorPane;
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
                sel_col_idx < workbench_slots_.size() && inspector_pane_) {
                inspector_pane_->set_page(workbench_slots_[sel_col_idx].page);
                int const num_rows = std::max(1, term_h - framework::kFrameChromeRows);
                auto const drawer = inspector_pane_->drawer_at_terminal_row(y, col_w, num_rows,
                                                                            /*is_secondary=*/false);
                if (drawer.has_value()) {
                    if (drawer->id == TuiDrawerId::Log) {
                        subpanel_start_y = drawer->start_terminal_row();
                        subpanel_end_y = drawer->end_terminal_row();
                    } else {
                        // Mission drawer is an interactive control, not a text-selection region.
                        subpanel_start_y = y + 1;
                        subpanel_end_y = y - 1;
                    }
                } else {
                    subpanel_end_y =
                        std::max(panel_content_start,
                                 inspector_pane_->content_end_terminal_row(col_w, num_rows,
                                                                           /*is_secondary=*/false));
                }
            }
            selection_.content_start_y = subpanel_start_y;
            selection_.content_end_y = subpanel_end_y;
            if (y < selection_.content_start_y || y > selection_.content_end_y) {
                selection_ = SelectionState{};
                if (esc_buf_.back() == 'M') {
                    handle_mouse(x, y, button, raw_x, raw_y);
                }
                return true;
            }
            selection_.pane_width = col_w;
            selection_.bounds = {
                .x = 0,
                .y = selection_.content_start_y,
                .width = col_w,
                .height = selection_.content_end_y - selection_.content_start_y + 1};

            auto const now = std::chrono::steady_clock::now();
            auto const elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - last_click_time_)
                    .count();
            if (elapsed_ms < 400 && last_click_x_ == x && last_click_y_ == y) {
                click_count_ = (click_count_ % 3) + 1;
            } else {
                click_count_ = 1;
            }
            last_click_time_ = now;
            last_click_x_ = x;
            last_click_y_ = y;

            if (click_count_ == 2) {
                // Double click: word selection
                std::string line;
                if (sel_pane == SelectionPane::TerminalPane) {
                    int start_line = get_terminal_pane_start_line(cached_num_rows_);
                    int r = start_line + (y - selection_.content_start_y);
                    line = vt_.get_text_in_range(r, 0, r, col_w);
                } else if (sel_pane == SelectionPane::InspectorPane && inspector_pane_) {
                    int r = y - selection_.content_start_y;
                    line = inspector_pane_->get_text_in_range(r, 0, r, col_w, col_w);
                }
                auto is_word_char = [](char ch) {
                    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '$' ||
                           ch == '%' || ch == '@' || ch == '.' || ch == '-' || ch == '+' ||
                           ch == 'x' || ch == 'X';
                };
                int word_start = col_local_x;
                int word_end = col_local_x;
                if (!line.empty() && col_local_x < static_cast<int>(line.size())) {
                    while (word_start > 0 &&
                           is_word_char(line[static_cast<size_t>(word_start - 1)])) {
                        --word_start;
                    }
                    while (word_end + 1 < static_cast<int>(line.size()) &&
                           is_word_char(line[static_cast<size_t>(word_end + 1)])) {
                        ++word_end;
                    }
                }
                selection_.start_x = word_start;
                selection_.end_x = word_end;
                selection_.start_y = y;
                selection_.end_y = y;
                selection_.is_active = true;
                selection_.is_selecting = false;
                copy_active_selection();
                render(true);
                return true;
            } else if (click_count_ == 3) {
                // Triple click: entire row selection
                selection_.start_x = 0;
                selection_.end_x = std::max(0, col_w - 1);
                selection_.start_y = y;
                selection_.end_y = y;
                selection_.is_active = true;
                selection_.is_selecting = false;
                copy_active_selection();
                render(true);
                return true;
            } else {
                selection_.start_x = col_local_x;
                selection_.start_y = y;
                selection_.end_x = col_local_x;
                selection_.end_y = y;
                selection_.is_selecting = true;
            }
        }

        if (esc_buf_.back() == 'M') {
            handle_mouse(x, y, button, raw_x, raw_y);
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
        if (terminal_search_active_) {
            handle_normal_keyboard_input(27, simrv::tui::TuiKey::Esc);
            return true;
        }
        if (is_modal_active()) {
            if (auto* impl = modal_.active_modal_impl()) {
                auto act = impl->handle_key(27, simrv::tui::TuiKey::Esc);
                if (act == framework::ModalAction::Close) {
                    close_modal();
                }
                render(true);
                return true;
            }
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
