/**
 * @file Tui.cpp
 * @brief Interactive TUI console dashboard implementation with premium double-line borders and resolved registers.
 */
#include "simrv/device/Tui.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <iostream>
#include <utility>

#include "simrv/core/Machine.hpp"
#include "simrv/xlen/Types.hpp"
#include "simrv/xlen/Helpers.hpp"

namespace simrv::device {

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
                    continue;
                }
                current_width++;
            }
            result += colored_str[i];
        }
    }
    
    if (current_width < target_width) {
        result += std::string(static_cast<std::size_t>(target_width - current_width), ' ');
    }
    
    return result;
}

static constexpr std::array<const char*, 32> kRegNames = {
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0/fp", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

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
    bar += "\033[90m"; // Dark gray
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

Tui::~Tui() {
    shutdown();
}

void Tui::initialize() {
    // Hide cursor & clear screen
    (void)(::write(STDOUT_FILENO, "\033[?25l\033[2J", 10) == 0);

    // Register SIGWINCH handler for dynamic terminal resizing
    struct sigaction sa{};
    sa.sa_handler = handle_sigwinch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);

    // Print boot diagnostics directly to TUI Console scrollback!
    print_log("SimRV Simulator Debug Dashboard Init...\n");
    print_log("Please press [Ctrl+P] to start/pause, [Space/s/S] to step, [Ctrl+Q] to quit.\n");
}

void Tui::shutdown() {
    // Show cursor & reset text styles
    (void)(::write(STDOUT_FILENO, "\033[?25h\033[0m\n", 9) == 0);

    // Restore default SIGWINCH behavior
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
    render();
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
    render();
}

void Tui::render() {
    // 1. Clear screen if resized
    if (g_resized) {
        g_resized = 0;
        (void)(::write(STDOUT_FILENO, "\033[2J", 4) == 0);
    }

    // 2. Get terminal size dynamically using ioctl
    int term_width = 127;
    int term_height = 36;
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        if (w.ws_col > 0) term_width = w.ws_col;
        if (w.ws_row > 0) term_height = w.ws_row;
    }

    // Adjust to terminal capabilities safely
    if (term_width < 80) term_width = 80;
    if (term_height < 24) term_height = 24;

    // Calculate dynamic pane widths to sum to EXACTLY term_width
    int left_pane_width = term_width - 2;
    int right_pane_width = term_width - 2;
    if (layout_ == TuiLayout::Split) {
        left_pane_width = (term_width - 3) / 2;
        right_pane_width = term_width - 3 - left_pane_width;
    }
    
    // Cache for wrapping in handle_char_write if layout changes
    pane_width_cached_ = right_pane_width;

    int num_rows = term_height - 7; // Leave 1 line buffer at bottom to prevent terminal scroll/flicker
    if (num_rows < 10) num_rows = 10;

    // 3. Calculate speed KIPS
    auto now = std::chrono::steady_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - last_speed_update_).count();
    if (elapsed_us >= 250000) {
        uint64_t diff = machine_.cpu.e_icount - last_icount_;
        speed_ips_ = (elapsed_us == 0) ? 0 : (diff * 1000000ULL / static_cast<uint64_t>(elapsed_us));
        kips_ = speed_ips_ / 1000ULL;
        last_icount_ = machine_.cpu.e_icount;
        last_speed_update_ = now;

        kips_history_.push_back(kips_);
        if (kips_history_.size() > 30) {
            kips_history_.erase(kips_history_.begin());
        }
    }

    // 4. Dynamic wrapping of raw console lines to the CURRENT pane width
    std::vector<std::string> wrapped_lines;
    for (const auto& rl : raw_lines_) {
        auto wrapped = wrap_line(rl, right_pane_width);
        wrapped_lines.insert(wrapped_lines.end(), wrapped.begin(), wrapped.end());
    }
    auto wrapped_curr = wrap_line(raw_current_line_, right_pane_width);
    wrapped_lines.insert(wrapped_lines.end(), wrapped_curr.begin(), wrapped_curr.end());

    // Align to dynamic num_rows
    lines_to_draw_.clear();
    if (wrapped_lines.size() < static_cast<std::size_t>(num_rows)) {
        lines_to_draw_.insert(lines_to_draw_.end(), num_rows - wrapped_lines.size(), "");
        lines_to_draw_.insert(lines_to_draw_.end(), wrapped_lines.begin(), wrapped_lines.end());
    } else {
        lines_to_draw_.insert(lines_to_draw_.end(), wrapped_lines.end() - num_rows, wrapped_lines.end());
    }

    // 5. Build whole screen buffer to avoid flickering
    std::string screen = "\033[H\033[?25l";

    // Top border (Double border ═, ╤, ╔, ╗)
    if (layout_ == TuiLayout::Split) {
        screen += "\033[1;94m╔" + make_repeated_string("═", left_pane_width) + "╤" + make_repeated_string("═", right_pane_width) + "╗\033[0m\n";
    } else {
        screen += "\033[1;94m╔" + make_repeated_string("═", term_width - 2) + "╗\033[0m\n";
    }

    // Header info (row 2)
    std::string status_badge = paused_ ? "\033[1;43;30m PAUSED \033[0m" : "\033[1;42;30m RUNNING \033[0m";
    std::string binary_name = machine_.s_fn_memimg;
    auto last_slash = binary_name.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        binary_name = binary_name.substr(last_slash + 1);
    }
    std::string left_text = std::format(" SimRV Monitor [{}] | Status: ", binary_name);
    std::string left_render = left_text + status_badge;
    int left_printed_len = static_cast<int>(left_text.length()) + (paused_ ? 8 : 9);
    int pad_left = (layout_ == TuiLayout::Split ? left_pane_width : term_width - 2) - left_printed_len;
    if (pad_left > 0) {
        left_render += std::string(static_cast<std::size_t>(pad_left), ' ');
    } else {
        left_render = format_to_width(left_render, layout_ == TuiLayout::Split ? left_pane_width : term_width - 2);
    }

    std::string right_text = std::format("Cycles: {} | Insns: {} | KIPS: {}", 
                                         format_with_commas(machine_.cpu.clint_mmio.mtime),
                                         format_with_commas(machine_.cpu.e_icount),
                                         format_with_commas(kips_));
    int pad_right = (layout_ == TuiLayout::Split ? right_pane_width : term_width - 2) - static_cast<int>(right_text.length());
    std::string right_render = right_text;
    if (pad_right > 0) {
        right_render = std::string(static_cast<std::size_t>(pad_right), ' ') + right_render;
    } else {
        right_render = format_to_width(right_render, layout_ == TuiLayout::Split ? right_pane_width : term_width - 2);
    }

    if (layout_ == TuiLayout::Split) {
        screen += "\033[1;94m║\033[0m" + left_render + "\033[1;94m│\033[0m" + right_render + "\033[1;94m║\033[0m\n";
        screen += "\033[1;94m╠" + make_repeated_string("═", left_pane_width) + "╪" + make_repeated_string("═", right_pane_width) + "╣\033[0m\n";
    } else if (layout_ == TuiLayout::FullConsole) {
        screen += "\033[1;94m║\033[0m" + right_render + "\033[1;94m║\033[0m\n";
        screen += "\033[1;94m╠" + make_repeated_string("═", term_width - 2) + "╣\033[0m\n";
    } else {
        screen += "\033[1;94m║\033[0m" + left_render + "\033[1;94m║\033[0m\n";
        screen += "\033[1;94m╠" + make_repeated_string("═", term_width - 2) + "╣\033[0m\n";
    }

    // Rows 4 to end (num_rows)
    for (int i = 0; i < num_rows; ++i) {
        if (layout_ == TuiLayout::Split) {
            std::string left = get_left_pane_row(i, left_pane_width);
            std::string right = get_right_pane_row(i, right_pane_width);
            screen += "\033[1;94m║\033[0m" + left + "\033[1;94m│\033[0m" + right + "\033[1;94m║\033[0m\n";
        } else if (layout_ == TuiLayout::FullConsole) {
            std::string right = get_right_pane_row(i, term_width - 2);
            screen += "\033[1;94m║\033[0m" + right + "\033[1;94m║\033[0m\n";
        } else {
            std::string left = get_left_pane_row(i, term_width - 2);
            screen += "\033[1;94m║\033[0m" + left + "\033[1;94m║\033[0m\n";
        }
    }

    // Split border
    if (layout_ == TuiLayout::Split) {
        screen += "\033[1;94m╠" + make_repeated_string("═", left_pane_width) + "╧" + make_repeated_string("═", right_pane_width) + "╣\033[0m\n";
    } else {
        screen += "\033[1;94m╠" + make_repeated_string("═", term_width - 2) + "╣\033[0m\n";
    }

    // Footer info
    std::string footer_text = " [Ctrl+Q] Quit | [Ctrl+P] Pause/Step | [Space/S] Step Cycle (when paused) | [Tab] Cycle Panel Layout ";
    int pad_foot = (term_width - 2) - static_cast<int>(footer_text.length());
    std::string footer_render = footer_text;
    if (pad_foot > 0) {
        footer_render += std::string(static_cast<std::size_t>(pad_foot), ' ');
    } else {
        footer_render = format_to_width(footer_render, term_width - 2);
    }
    screen += "\033[1;94m║\033[0m" + footer_render + "\033[1;94m║\033[0m\n";
    screen += "\033[1;94m╚" + make_repeated_string("═", term_width - 2) + "╝\033[0m";

    (void)(::write(STDOUT_FILENO, screen.data(), screen.size()) == 0);
    ::fflush(stdout);
}

auto Tui::get_sparkline_string(int width) -> std::string {
    if (kips_history_.empty()) {
        return std::string(static_cast<std::size_t>(width), ' ');
    }
    uint64_t max_val = 1;
    for (auto val : kips_history_) {
        if (val > max_val) max_val = val;
    }

    std::string s;
    int history_size = static_cast<int>(kips_history_.size());
    if (history_size < width) {
        s += std::string(static_cast<std::size_t>(width - history_size), ' ');
    }

    static constexpr std::array<const char*, 9> kBlocks = {
        " ", " ", "▂", "▃", "▄", "▅", "▆", "▇", "█"
    };

    int start_idx = (history_size > width) ? (history_size - width) : 0;
    for (int i = start_idx; i < history_size; ++i) {
        uint64_t val = kips_history_[static_cast<std::size_t>(i)];
        double ratio = static_cast<double>(val) / static_cast<double>(max_val);
        int idx = static_cast<int>(ratio * 8.0);
        if (idx < 0) idx = 0;
        if (idx > 8) idx = 8;
        s += kBlocks[static_cast<std::size_t>(idx)];
    }
    return s;
}

auto Tui::get_left_pane_row(int row_idx, int pane_width) -> std::string {
    auto& cpu = machine_.cpu;
    auto& st = cpu.state();

    int col_width = pane_width / 2;

    if (row_idx >= 0 && row_idx <= 15) {
        int reg1 = row_idx;
        int reg2 = row_idx + 16;

        auto val1 = st.regs.read(static_cast<RegId>(reg1));
        auto val2 = st.regs.read(static_cast<RegId>(reg2));

        std::string name1 = kRegNames[static_cast<std::size_t>(reg1)];
        std::string name2 = kRegNames[static_cast<std::size_t>(reg2)];

        std::string col1_color = std::format(" \033[97mx{:<2}\033[0m/\033[36m{:<5}\033[0m: \033[92m0x{:0{}x}\033[0m", 
                                             reg1, name1, val1, simrv::xlen::kXLenHexDigits);
        std::string col2_color = std::format(" \033[97mx{:<2}\033[0m/\033[36m{:<5}\033[0m: \033[92m0x{:0{}x}\033[0m", 
                                             reg2, name2, val2, simrv::xlen::kXLenHexDigits);

        return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
    }

    if (row_idx == 16) {
        std::string title = " ── CPU Status & CSRs ";
        std::string dashes = make_repeated_string("─", std::max(0, pane_width - static_cast<int>(title.length())));
        return "\033[1;90m" + format_to_width(title + dashes, pane_width) + "\033[0m";
    }

    if (row_idx == 17) {
        std::string priv_str = (st.priv == kPrivMachine) ? "MACHINE" :
                               (st.priv == kPrivSupervisor) ? "SUPERVISOR" : "USER";
        std::string col1_color = std::format(" \033[97mpc\033[0m     : \033[93m0x{:0{}x}\033[0m", st.pc, simrv::xlen::kXLenHexDigits);
        std::string col2_color = std::format(" \033[97mpriv\033[0m   : \033[1;35m{}\033[0m", priv_str);

        return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
    }

    if (row_idx == 18) {
        std::string mstatus_str = simrv::xlen::resolve_mstatus_short_string(st.mstatus);
        std::string misa_str = simrv::xlen::resolve_misa_string(st.misa);

        std::string col1_color = std::format(" \033[97mmstatus\033[0m: \033[36m0x{:0{}x}\033[0m \033[90m{}\033[0m", st.mstatus, simrv::xlen::kXLenHexDigits, mstatus_str);
        std::string col2_color = std::format(" \033[97mmisa\033[0m   : \033[36m{}\033[0m", misa_str);

        // Adjust split so mstatus doesn't get truncated (needs ~32 chars)
        int custom_col1 = std::min(pane_width - 10, std::max(col_width, 32));
        return format_to_width(col1_color, custom_col1) + format_to_width(col2_color, pane_width - custom_col1);
    }

    if (row_idx == 19) {
        std::string col1_color = std::format(" \033[97mmie\033[0m    : \033[36m0x{:0{}x}\033[0m", st.mie, simrv::xlen::kXLenHexDigits);
        std::string col2_color = std::format(" \033[97mmip\033[0m    : \033[36m0x{:0{}x}\033[0m", st.mip, simrv::xlen::kXLenHexDigits);

        return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
    }

    if (row_idx == 20) {
        std::string col1_color = std::format(" \033[97mmtvec\033[0m  : \033[36m0x{:0{}x}\033[0m", st.mtvec, simrv::xlen::kXLenHexDigits);
        std::string col2_color = std::format(" \033[97mmscr\033[0m   : \033[36m0x{:0{}x}\033[0m", st.mscratch, simrv::xlen::kXLenHexDigits);

        return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
    }

    if (row_idx == 21) {
        std::string mcause_str = simrv::xlen::resolve_cause_string(st.mcause);
        std::string col1_color = std::format(" \033[97mmepc\033[0m   : \033[36m0x{:0{}x}\033[0m", st.mepc, simrv::xlen::kXLenHexDigits);
        std::string col2_color = std::format(" \033[97mmcause\033[0m : \033[36m{}\033[0m", mcause_str);

        return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
    }

    if (row_idx == 22) {
        std::string satp_mode_str = simrv::xlen::resolve_satp_string(st.satp);
        std::string col1_color = std::format(" \033[97mmtval\033[0m  : \033[36m0x{:0{}x}\033[0m", st.mtval, simrv::xlen::kXLenHexDigits);
        std::string col2_color = std::format(" \033[97msatp\033[0m   : \033[36m0x{:0{}x}\033[0m \033[90m({})\033[0m", st.satp, simrv::xlen::kXLenHexDigits, satp_mode_str);

        // Adjust split so satp doesn't get truncated (needs ~33 chars in col2)
        int custom_col1 = std::max(10, std::min(col_width, pane_width - 33));
        return format_to_width(col1_color, custom_col1) + format_to_width(col2_color, pane_width - custom_col1);
    }

    if (row_idx == 23) {
        std::string scause_str = simrv::xlen::resolve_cause_string(st.scause);
        std::string col1_color = std::format(" \033[97msepc\033[0m   : \033[36m0x{:0{}x}\033[0m", st.sepc, simrv::xlen::kXLenHexDigits);
        std::string col2_color = std::format(" \033[97mscause\033[0m : \033[36m{}\033[0m", scause_str);

        return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
    }

    if (row_idx == 24) {
        std::string col1_color = std::format(" \033[97mstval\033[0m  : \033[36m0x{:0{}x}\033[0m", st.stval, simrv::xlen::kXLenHexDigits);
        std::string col2_color = std::format(" \033[97mloadres\033[0m: \033[36m0x{:0{}x}\033[0m", st.load_res, simrv::xlen::kXLenHexDigits);

        return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
    }

    if (row_idx == 25) {
        std::string title = " ── L1 Cache & Performance ";
        std::string dashes = make_repeated_string("─", std::max(0, pane_width - static_cast<int>(title.length())));
        return "\033[1;90m" + format_to_width(title + dashes, pane_width) + "\033[0m";
    }

    if (row_idx == 26) {
        uint64_t i_hits = cpu.icache.hit_count();
        uint64_t i_misses = cpu.icache.miss_count();
        uint64_t i_total = i_hits + i_misses;
        double i_ratio = (i_total == 0) ? 0.0 : static_cast<double>(i_hits) / static_cast<double>(i_total);

        // Dynamic progress bar calculation
        std::string suffix = std::format(" {:.1f}% (H:{} M:{})", i_ratio * 100.0, format_compact(i_hits), format_compact(i_misses));
        std::string prefix = " L1-I Cache : [";
        int bar_width = pane_width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length()) - 1;
        if (bar_width < 5) bar_width = 5;

        std::string bar = make_progress_bar(i_ratio, bar_width, "\033[96m"); // Cyan bar
        std::string color = std::format("\033[97mL1-I Cache\033[0m : [{}]\033[96m{:.1f}%\033[0m \033[90m(H:{} M:{})\033[0m", 
                                        bar, i_ratio * 100.0, format_compact(i_hits), format_compact(i_misses));

        return format_to_width(color, pane_width);
    }

    if (row_idx == 27) {
        uint64_t d_hits = cpu.dcache.hit_count();
        uint64_t d_misses = cpu.dcache.miss_count();
        uint64_t d_total = d_hits + d_misses;
        double d_ratio = (d_total == 0) ? 0.0 : static_cast<double>(d_hits) / static_cast<double>(d_total);

        // Dynamic progress bar calculation
        std::string suffix = std::format(" {:.1f}% (H:{} M:{})", d_ratio * 100.0, format_compact(d_hits), format_compact(d_misses));
        std::string prefix = " L1-D Cache : [";
        int bar_width = pane_width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length()) - 1;
        if (bar_width < 5) bar_width = 5;

        std::string bar = make_progress_bar(d_ratio, bar_width, "\033[95m"); // Magenta bar
        std::string color = std::format("\033[97mL1-D Cache\033[0m : [{}]\033[95m{:.1f}%\033[0m \033[90m(H:{} M:{})\033[0m", 
                                        bar, d_ratio * 100.0, format_compact(d_hits), format_compact(d_misses));

        return format_to_width(color, pane_width);
    }

    if (row_idx == 28) {
        uint64_t max_val = 1;
        for (auto val : kips_history_) {
            if (val > max_val) max_val = val;
        }

        std::string suffix = std::format(" {} Max:{}", format_with_commas(kips_), format_with_commas(max_val));
        std::string prefix = " Speed (KIPS): [";
        int spark_width = pane_width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length()) - 1;
        if (spark_width < 5) spark_width = 5;

        std::string spark = get_sparkline_string(spark_width);
        std::string color = std::format("\033[97mSpeed (KIPS)\033[0m: [\033[92m{}\033[0m]\033[92m {}\033[0m \033[90mMax:{}\033[0m", 
                                        spark, format_with_commas(kips_), format_with_commas(max_val));

        return format_to_width(color, pane_width);
    }

    if (row_idx == 29) {
        uint64_t alu_count = 0;
        uint64_t mem_count = 0;
        uint64_t ctrl_count = 0;
        uint64_t sys_count = 0;

        for (int i = 0; i < OperationIdCount; ++i) {
            uint64_t count = cpu.e_instmix[i];
            if (i == OperationId::JAL || i == OperationId::JALR || (i >= OperationId::BEQ && i <= OperationId::BGEU)) {
                ctrl_count += count;
            } else if ((i >= OperationId::LB && i <= OperationId::SD) || i == OperationId::LR_W || i == OperationId::SC_W || (i >= OperationId::AMOSWAP_W && i <= OperationId::AMOMAXU_W)) {
                mem_count += count;
            } else if (i == OperationId::ECALL || i == OperationId::EBREAK || i == OperationId::URET || i == OperationId::SRET || i == OperationId::MRET || i == OperationId::WFI || i == OperationId::SFENCE_VMA) {
                sys_count += count;
            } else {
                alu_count += count;
            }
        }

        uint64_t total = alu_count + mem_count + ctrl_count + sys_count;
        double alu_p = (total == 0) ? 0.0 : static_cast<double>(alu_count * 100ULL) / static_cast<double>(total);
        double mem_p = (total == 0) ? 0.0 : static_cast<double>(mem_count * 100ULL) / static_cast<double>(total);
        double ctrl_p = (total == 0) ? 0.0 : static_cast<double>(ctrl_count * 100ULL) / static_cast<double>(total);
        double sys_p = (total == 0) ? 0.0 : static_cast<double>(sys_count * 100ULL) / static_cast<double>(total);

        std::string color = std::format(" \033[97mInst Mix\033[0m   : \033[92mALU:{:.1f}%\033[0m \033[96mMEM:{:.1f}%\033[0m \033[93mCTRL:{:.1f}%\033[0m \033[95mSYS:{:.1f}%\033[0m", 
                                        alu_p, mem_p, ctrl_p, sys_p);

        return format_to_width(color, pane_width);
    }

    return format_to_width("", pane_width);
}

auto Tui::get_right_pane_row(int row_idx, int pane_width) -> std::string {
    std::string line = lines_to_draw_[static_cast<std::size_t>(row_idx)];
    return format_to_width(line, pane_width);
}

}  // namespace simrv::device
