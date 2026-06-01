#include "simrv/tui/Ansi.hpp"
/**
 * @file Tui.cpp
 * @brief Interactive TUI console dashboard implementation with premium double-line borders and
 * resolved registers.
 */
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <print>

#include "simrv/core/Machine.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/tui/TuiComponents.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::tui {

volatile std::sig_atomic_t g_resized = 0;

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

auto get_display_width(const std::string& s) -> int {
    int len = 0;
    bool in_esc = false;
    for (std::size_t i = 0; i < s.length(); ++i) {
        if (s[i] == '\033') {
            in_esc = true;
        } else if (in_esc) {
            if (s[i] == 'm') {
                in_esc = false;
            }
        } else {
            unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80 || c >= 0xC0) {
                len++;
            }
        }
    }
    return len;
}

auto format_to_width(const std::string& colored_str, int target_width) -> std::string {
    int current_width = 0;
    std::string result;
    bool in_esc = false;
    bool skipping = false;

    for (std::size_t i = 0; i < colored_str.length(); ++i) {
        if (colored_str[i] == '\033') {
            in_esc = true;
            result += colored_str[i];
        } else if (in_esc) {
            result += colored_str[i];
            if (colored_str[i] == 'm') {
                in_esc = false;
            }
        } else {
            unsigned char c = static_cast<unsigned char>(colored_str[i]);
            bool is_lead = (c < 0x80 || c >= 0xC0);
            if (is_lead) {
                if (current_width >= target_width) {
                    skipping = true;
                } else {
                    skipping = false;
                    current_width++;
                }
            }
            if (!skipping) {
                result += colored_str[i];
            }
        }
    }

    if (current_width < target_width) {
        result += std::string(static_cast<std::size_t>(target_width - current_width), ' ');
    }

    return result + "\033[0m";
}

static constexpr std::array<const char*, 32> kRegNames = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0/fp", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3",    "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5",    "t6"};

static constexpr std::array<const char*, 32> kFpRegNames = {
    "ft0", "ft1", "ft2", "ft3", "ft4",  "ft5",  "ft6", "ft7", "fs0",  "fs1", "fa0",
    "fa1", "fa2", "fa3", "fa4", "fa5",  "fa6",  "fa7", "fs2", "fs3",  "fs4", "fs5",
    "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};

auto format_with_commas(uint64_t val) -> std::string {
    std::string s = std::to_string(val);
    int n = static_cast<int>(s.length()) - 3;
    while (n > 0) {
        s.insert(static_cast<std::size_t>(n), ",");
        n -= 3;
    }
    return s;
}

auto make_progress_bar(double ratio, int width, const std::string& color_code) -> std::string {
    int filled = static_cast<int>(ratio * width);
    if (filled < 0) filled = 0;
    if (filled > width) filled = width;
    std::string bar;
    bar += color_code;
    for (int i = 0; i < filled; ++i) {
        bar += "█";
    }
    bar += "\033[90m";  // Dark gray
    for (int i = filled; i < width; ++i) {
        bar += "░";
    }
    bar += "\033[0m";
    return bar;
}

auto format_compact(uint64_t val) -> std::string {
    if (val >= 1000000000ULL) {
        return std::format("{:.1f}G", static_cast<double>(val) / 1000000000.0);
    }
    if (val >= 1000000ULL) {
        return std::format("{:.1f}M", static_cast<double>(val) / 1000000.0);
    }
    if (val >= 1000ULL) {
        return std::format("{:.1f}K", static_cast<double>(val) / 1000.0);
    }
    return std::to_string(val);
}
auto wrap_line(const std::string& line, int max_width) -> std::vector<std::string> {
    std::vector<std::string> chunks;
    if (line.empty()) {
        chunks.push_back("");
        return chunks;
    }
    if (max_width <= 0) {
        chunks.push_back(line);
        return chunks;
    }
    std::size_t pos = 0;
    while (pos < line.length()) {
        std::size_t len = std::min(static_cast<std::size_t>(max_width), line.length() - pos);
        chunks.push_back(line.substr(pos, len));
        pos += len;
    }
    return chunks;
}

}  // namespace

Tui::Tui(simrv::core::Machine& machine) : machine_(machine) {
    last_speed_update_ = std::chrono::steady_clock::now();
    initialize();
}

Tui::~Tui() { shutdown(); }

void Tui::initialize() {
    reg_pane_ = std::make_unique<RegisterPane>(machine_);
    console_pane_ = std::make_unique<ConsolePane>();
    status_bar_ = std::make_unique<StatusBar>(machine_);

    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag &= ~ICANON;
    term.c_lflag &= ~ECHO;
    term.c_cc[VMIN] = 1;
    term.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &term);

    const char* init_seq = "\033[?1049h\033[2J\033[H\033[?1000h\033[?1006h\033[?25l";
    (void)(::write(STDOUT_FILENO, init_seq, strlen(init_seq)) == 0);

    struct sigaction sa{};
    sa.sa_handler = handle_sigwinch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);

    print_log("SimRV Simulator Debug Dashboard Init...\n");
    print_log("Please press [Ctrl+P] to start/pause, [Space/s/S] to step, [Ctrl+Q] to quit.\n");
}

void Tui::shutdown() {
    const char* shutdown_seq = "\033[?1006l\033[?1000l\033[?25h\033[?1049l\n";
    (void)(::write(STDOUT_FILENO, shutdown_seq, strlen(shutdown_seq)) == 0);

    struct termios term;
    tcgetattr(STDIN_FILENO, &term);
    term.c_lflag |= ICANON;
    term.c_lflag |= ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &term);

    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);
}

void Tui::handle_char_write(char ch) {
    if (ch == '\n') {
        raw_lines_.push_back(raw_current_line_);
        if (raw_lines_.size() > 500) {
            raw_lines_.erase(raw_lines_.begin());
        }
        raw_current_line_.clear();
    } else if (ch == '\r') {
        // Ignore carriage return
    } else if (ch == '\t') {
        raw_current_line_ += "    ";
    } else if (ch == '\b' || ch == 127) {
        if (!raw_current_line_.empty()) {
            raw_current_line_.pop_back();
        }
    } else if (ch >= 32 && ch <= 126) {
        raw_current_line_ += ch;
        if (raw_current_line_.length() >= 1024) {
            raw_lines_.push_back(raw_current_line_);
            if (raw_lines_.size() > 500) {
                raw_lines_.erase(raw_lines_.begin());
            }
            raw_current_line_.clear();
        }
    }
}

void Tui::print_log(const std::string& msg) {
    for (char ch : msg) {
        if (ch == '\n') {
            raw_lines_.push_back(raw_current_line_);
            if (raw_lines_.size() > 500) {
                raw_lines_.erase(raw_lines_.begin());
            }
            raw_current_line_.clear();
        } else if (ch != '\r') {
            raw_current_line_ += ch;
        }
    }
}

void Tui::render() {
    if (!reg_pane_ || !console_pane_ || !status_bar_) {
        return;
    }

    if (g_resized) {
        g_resized = 0;
    }

    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    int term_width = w.ws_col;
    int term_height = w.ws_row;

    if (term_width < 40 || term_height < 10) return;

    // Track KIPS and notify RegisterPane
    auto now = std::chrono::steady_clock::now();
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

    // Rebuild visible console rows from raw log state.
    lines_to_draw_.clear();
    if (right_pane_width > 0 && num_rows > 0) {
        std::vector<std::string> all_lines = raw_lines_;
        if (!raw_current_line_.empty()) {
            all_lines.push_back(raw_current_line_);
        }

        std::vector<std::string> wrapped_lines;
        wrapped_lines.reserve(all_lines.size());
        for (const auto& line : all_lines) {
            auto chunks = wrap_line(line, right_pane_width);
            wrapped_lines.insert(wrapped_lines.end(), chunks.begin(), chunks.end());
        }
        if (wrapped_lines.empty()) {
            wrapped_lines.emplace_back("");
        }

        const int total = static_cast<int>(wrapped_lines.size());
        int end_exclusive = total - scroll_offset_;
        if (end_exclusive < 0) {
            end_exclusive = 0;
        }
        if (end_exclusive > total) {
            end_exclusive = total;
        }
        int start = end_exclusive - num_rows;
        if (start < 0) {
            start = 0;
        }

        lines_to_draw_.insert(lines_to_draw_.end(), wrapped_lines.begin() + start,
                              wrapped_lines.begin() + end_exclusive);
        while (static_cast<int>(lines_to_draw_.size()) < num_rows) {
            lines_to_draw_.emplace_back("");
        }
    }

    // Update panes state
    reg_pane_->set_kips(kips_);
    reg_pane_->set_kips_history(kips_history_);
    reg_pane_->set_paused(paused_);
    if (!paused_) {
        reg_pane_->update_cache();
    }
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
    std::string screen = "[H";
    screen += status_bar_->render_row(0, term_width);

    for (int i = 0; i < num_rows; ++i) {
        if (layout_ == TuiLayout::Split) {
            std::string left = reg_pane_->render_row(i, left_pane_width);
            std::string right = console_pane_->render_row(i, right_pane_width);
            screen +=
                "\033[1;94m║\033[0m" + left + "\033[1;94m│\033[0m" + right + "\033[1;94m║\033[0m\n";
        } else if (layout_ == TuiLayout::FullConsole) {
            std::string right = console_pane_->render_row(i, right_pane_width);
            screen += "\033[1;94m║\033[0m" + right + "\033[1;94m║\033[0m\n";
        } else {
            std::string left = reg_pane_->render_row(i, left_pane_width);
            screen += "\033[1;94m║\033[0m" + left + "\033[1;94m║\033[0m\n";
        }
    }

    if (layout_ == TuiLayout::Split) {
        screen += "\033[1;94m╠" + make_repeated_string("═", left_pane_width) + "╧" +
                  make_repeated_string("═", right_pane_width) + "╣\033[0m\n";
    } else {
        screen += "\033[1;94m╠" + make_repeated_string("═", term_width - 2) + "╣\033[0m\n";
    }

    screen += status_bar_->render_row(1, term_width);

    (void)(::write(STDOUT_FILENO, screen.data(), screen.size()) == 0);
    ::fflush(stdout);
}

void Tui::handle_mouse(int x, int y, int b) {
    if (!console_pane_ || !reg_pane_) {
        return;
    }

    (void)x;
    (void)y;
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
    } else if (b == 0) {
        if (layout_ == TuiLayout::Split && x < 70) cycle_reg_page();
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

}  // namespace simrv::tui
