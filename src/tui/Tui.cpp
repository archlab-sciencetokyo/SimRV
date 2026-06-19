#include "simrv/util/FormatUtil.hpp"
/**
 * @file Tui.cpp
 * @brief Interactive TUI console dashboard implementation with premium double-line borders and
 * resolved registers.
 */
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cctype>
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
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/RegisterPane.hpp"
#include "simrv/tui/ConsolePane.hpp"
#include "simrv/tui/StatusBar.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"
#include "simrv/pipeline/Decoder.hpp"

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

}  // namespace

Tui::Tui(simrv::core::Machine& machine) : machine_(machine) {
    last_speed_update_ = std::chrono::steady_clock::now();
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
    initialize();
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
    reg_pane_ = std::make_unique<RegisterPane>(machine_);
    console_pane_ = std::make_unique<ConsolePane>();
    status_bar_ = std::make_unique<StatusBar>(machine_);

    set_high_contrast(machine_.s_high_contrast);

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
    vt_log_.write_string(msg);
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
    if (layout_ == TuiLayout::FullConsole) {
        left_pane_width = 0;
        right_pane_width = term_width - 2;
    } else if (layout_ == TuiLayout::FullRegister) {
        left_pane_width = term_width - 2;
        right_pane_width = 0;
    }

    // StatusBar renders 3 header lines + 2 footer lines, and we add 1 separator before footer.
    int num_rows = term_height - 6;

    // Limit scrollback to the existing rows to prevent clearing the screen completely
    {
        int total = 0;
        if (right_panel_mode_ == TuiRightPanelMode::Terminal) {
            total = vt_.get_lines_count();
        } else if (right_panel_mode_ == TuiRightPanelMode::Log) {
            total = vt_log_.get_lines_count();
        } else {
            total = static_cast<int>(trace_buffer_.size());
        }
        int max_scroll = std::max(0, total - num_rows);
        if (scroll_offset_ > max_scroll) {
            scroll_offset_ = max_scroll;
        }
    }

    // Rebuild visible console/log/trace rows depending on panel mode.
    lines_to_draw_.clear();
    if (right_pane_width > 0 && num_rows > 0) {
        if (right_panel_mode_ == TuiRightPanelMode::Terminal || right_panel_mode_ == TuiRightPanelMode::Log) {
            VirtualTerminal& current_vt = (right_panel_mode_ == TuiRightPanelMode::Terminal) ? vt_ : vt_log_;
            current_vt.resize(right_pane_width, num_rows);

            int total = current_vt.get_lines_count();
            int end_exclusive = total - scroll_offset_;
            if (end_exclusive < 0) {
                end_exclusive = 0;
            }
            int start = end_exclusive - num_rows;
            if (start < 0) {
                start = 0;
            }

            int cursor_abs_line = current_vt.get_scrollback_size() + current_vt.get_cursor_y();
            bool is_live = (scroll_offset_ == 0);

            for (int i = start; i < end_exclusive; ++i) {
                bool draw_cursor = (right_panel_mode_ == TuiRightPanelMode::Terminal) && is_live && (i == cursor_abs_line) && current_vt.is_cursor_visible();
                lines_to_draw_.push_back(current_vt.get_line_as_string(i, right_pane_width, draw_cursor));
            }
        } else {
            // LiveTrace
            int total = static_cast<int>(trace_buffer_.size());
            int end_exclusive = total - scroll_offset_;
            if (end_exclusive < 0) {
                end_exclusive = 0;
            }
            int start = end_exclusive - num_rows;
            if (start < 0) {
                start = 0;
            }

            for (int i = start; i < end_exclusive; ++i) {
                std::string s = trace_buffer_.at(static_cast<std::size_t>(i));
                if (static_cast<int>(s.length()) > right_pane_width) {
                    s = s.substr(0, static_cast<std::size_t>(right_pane_width));
                } else {
                    s += std::string(static_cast<std::size_t>(right_pane_width - s.length()), ' ');
                }
                lines_to_draw_.push_back(s);
            }
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
    status_bar_->set_right_panel_mode(right_panel_mode_);
    status_bar_->set_trace_enabled(trace_enabled_);

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
    } else if (rp == TuiRegPage::PIPELINE) {
        rp = TuiRegPage::EXPLAIN;
    } else {
        rp = TuiRegPage::GPR;
    }
    reg_pane_->set_page(rp);
    render();
}

void Tui::set_reg_page(TuiRegPage page) {
    if (reg_pane_) {
        reg_pane_->set_page(page);
        render();
    }
}

void Tui::toggle_explain() {
    if (reg_pane_) {
        if (reg_pane_->get_page() == TuiRegPage::EXPLAIN) {
            reg_pane_->set_page(TuiRegPage::GPR);
        } else {
            reg_pane_->set_page(TuiRegPage::EXPLAIN);
        }
        render();
    }
}

void Tui::toggle_high_contrast() {
    machine_.s_high_contrast = !machine_.s_high_contrast;
    set_high_contrast(machine_.s_high_contrast);
    render();
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
    render();
}

void Tui::cycle_right_panel_mode() {
    if (right_panel_mode_ == TuiRightPanelMode::Terminal) {
        right_panel_mode_ = TuiRightPanelMode::Log;
    } else if (right_panel_mode_ == TuiRightPanelMode::Log) {
        right_panel_mode_ = TuiRightPanelMode::LiveTrace;
    } else {
        right_panel_mode_ = TuiRightPanelMode::Terminal;
    }
    scroll_offset_ = 0;
    render();
}

void Tui::toggle_trace_enabled() {
    trace_enabled_ = !trace_enabled_;
    render();
}

void Tui::record_instruction(Register pc, uint8_t op_id, uint8_t rd, Register rd_val,
                              uint8_t rs1, Register rs1_val, uint8_t rs2, Register rs2_val,
                              int64_t imm) {
    std::string op_name;
    if (static_cast<std::size_t>(op_id) < simrv::pipeline::OPERATION_NAME.size()) {
        std::string_view name_sv = simrv::pipeline::OPERATION_NAME.at(op_id);
        for (char c : name_sv) {
            op_name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    } else {
        op_name = "unknown";
    }

    for (char& c : op_name) {
        if (c == '_') c = '.';
    }

    bool is_fp = op_name.starts_with("f") && !op_name.starts_with("fence");

    auto get_reg_name = [is_fp](uint8_t reg, bool fp_override = false) -> std::string {
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
        return (is_fp || fp_override) ? fp_names[reg] : abi_names[reg];
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

    bool rd_fp = is_fp;
    bool rs1_fp = is_fp;
    bool rs2_fp = is_fp;

    if (op_name == "fcvt.w.s" || op_name == "fcvt.wu.s" || op_name == "fcvt.w.d" || op_name == "fcvt.wu.d" ||
        op_name == "fcvt.l.s" || op_name == "fcvt.lu.s" || op_name == "fcvt.l.d" || op_name == "fcvt.lu.d" ||
        op_name == "fmv.x.w" || op_name == "fmv.x.d" || op_name.starts_with("feq") ||
        op_name.starts_with("flt") || op_name.starts_with("fle") || op_name.starts_with("fclass")) {
        rd_fp = false;
        rs1_fp = true;
        rs2_fp = true;
    } else if (op_name == "fcvt.s.w" || op_name == "fcvt.s.wu" || op_name == "fcvt.d.w" || op_name == "fcvt.d.wu" ||
               op_name == "fcvt.s.l" || op_name == "fcvt.s.lu" || op_name == "fcvt.d.l" || op_name == "fcvt.d.lu" ||
               op_name == "fmv.w.x" || op_name == "fmv.d.x") {
        rd_fp = true;
        rs1_fp = false;
        rs2_fp = false;
    } else if (is_load) {
        rd_fp = is_fp;
        rs1_fp = false;
    } else if (is_store) {
        rs2_fp = is_fp;
        rs1_fp = false;
    }

    if (is_lui || is_auipc) {
        inst_str = std::format("{} {}, {:#x}", op_name, get_reg_name(rd, rd_fp), static_cast<uint32_t>(imm) >> 12);
        side_effect = std::format("{} = {:#x}", get_reg_name(rd, rd_fp), rd_val);
    } else if (is_jal) {
        if (rd == 0) {
            inst_str = std::format("j {:#x}", pc + imm);
        } else {
            inst_str = std::format("{} {}, {:#x}", op_name, get_reg_name(rd, rd_fp), pc + imm);
            side_effect = std::format("{} = {:#x}", get_reg_name(rd, rd_fp), rd_val);
        }
    } else if (is_jalr || is_load) {
        inst_str = std::format("{} {}, {}({})", op_name, get_reg_name(rd, rd_fp), imm, get_reg_name(rs1, rs1_fp));
        side_effect = std::format("{} = {:#x}", get_reg_name(rd, rd_fp), rd_val);
    } else if (is_branch) {
        inst_str = std::format("{} {}, {}, {:#x}", op_name, get_reg_name(rs1, rs1_fp), get_reg_name(rs2, rs2_fp), pc + imm);
    } else if (is_store) {
        inst_str = std::format("{} {}, {}({})", op_name, get_reg_name(rs2, rs2_fp), imm, get_reg_name(rs1, rs1_fp));
        side_effect = std::format("mem[{:#x}] = {:#x}", rs1_val + imm, rs2_val);
    } else if (is_csr) {
        if (op_name.ends_with("i")) {
            inst_str = std::format("{} {}, {:#x}, {}", op_name, get_reg_name(rd, rd_fp), imm & 0xFFF, rs1);
        } else {
            inst_str = std::format("{} {}, {:#x}, {}", op_name, get_reg_name(rd, rd_fp), imm & 0xFFF, get_reg_name(rs1, rs1_fp));
        }
        side_effect = std::format("{} = {:#x}", get_reg_name(rd, rd_fp), rd_val);
    } else if (is_system) {
        inst_str = op_name;
    } else if (op_name.starts_with("amo")) {
        inst_str = std::format("{} {}, {}, ({})", op_name, get_reg_name(rd, rd_fp), get_reg_name(rs2, rs2_fp), get_reg_name(rs1, rs1_fp));
        side_effect = std::format("{} = {:#x}", get_reg_name(rd, rd_fp), rd_val);
    } else if (op_name.ends_with("i") || op_name.ends_with("iw")) {
        inst_str = std::format("{} {}, {}, {}", op_name, get_reg_name(rd, rd_fp), get_reg_name(rs1, rs1_fp), imm);
        side_effect = std::format("{} = {:#x}", get_reg_name(rd, rd_fp), rd_val);
    } else {
        inst_str = std::format("{} {}, {}, {}", op_name, get_reg_name(rd, rd_fp), get_reg_name(rs1, rs1_fp), get_reg_name(rs2, rs2_fp));
        if (rd != 0) {
            side_effect = std::format("{} = {:#x}", get_reg_name(rd, rd_fp), rd_val);
        }
    }

    std::string sym = machine_.symbols.lookup(pc);
    std::string line;
    if (sym.empty()) {
        if (side_effect.empty()) {
            line = std::format("{:#x}: {}", pc, inst_str);
        } else {
            line = std::format("{:#x}: {} [{}]", pc, inst_str, side_effect);
        }
    } else {
        if (side_effect.empty()) {
            line = std::format("{:#x} <{}>: {}", pc, sym, inst_str);
        } else {
            line = std::format("{:#x} <{}>: {} [{}]", pc, sym, inst_str, side_effect);
        }
    }

    trace_buffer_.push_back(line);
    if (trace_buffer_.size() > 200) {
        trace_buffer_.erase(trace_buffer_.begin());
    }
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

void Tui::adjust_left_pane_width(int delta) {
    struct winsize w{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w); // NOLINT(cppcoreguidelines-pro-type-vararg)
    int term_width = w.ws_col;

    int current = user_left_pane_width_ > 0 ? user_left_pane_width_ : ((term_width > 120) ? 75 : 62);
    current += delta;
    if (current < 40) current = 40;
    if (current > term_width - 10) current = std::max(40, term_width - 10);
    user_left_pane_width_ = current;
    render();
}

}  // namespace simrv::tui
