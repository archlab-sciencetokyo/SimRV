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
#include <print>

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
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0/fp", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

static constexpr std::array<const char*, 32> kFpRegNames = {
    "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",
    "fs0", "fs1", "fa0", "fa1", "fa2", "fa3", "fa4", "fa5",
    "fa6", "fa7", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
    "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"
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
    // Enter alternate screen buffer, hide cursor & clear screen
    (void)(::write(STDOUT_FILENO, "\033[?1049h\033[?25l\033[2J", 18) == 0);
    g_resized = 1; // Force a full clear on the first render to ensure no layout corruption

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
    // Show cursor, reset text styles & exit alternate screen buffer
    (void)(::write(STDOUT_FILENO, "\033[?25h\033[0m\033[?1049l\n", 18) == 0);

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
    // 1. Clear screen if resized
    if (g_resized) {
        g_resized = 0;
        cached_left_rows_.clear();
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

    // 60 columns is the minimum width to comfortably show the raw console in FullConsole mode.
    int min_term_width = 60;
    if (term_width < min_term_width || term_height < 28) {
        std::string screen = "\033[H\033[2J\033[?25l";
        std::string warning_title = " ⚠️  Terminal Too Small ";
        std::string req_text = std::format("Required: at least {}x28  |  Current: {}x{}", min_term_width, term_width, term_height);
        std::string action_text = "Please resize your terminal window to continue simulation.";

        int box_width = std::max(60, term_width - 4);
        int pad_y = (term_height - 6) / 2;
        if (pad_y < 1) pad_y = 1;

        for (int y = 0; y < pad_y; ++y) screen += "\n";

        screen += "\033[1;31m╔" + make_repeated_string("═", box_width - 2) + "╗\033[0m\n";
        screen += "\033[1;31m║\033[0m" + format_to_width(std::format(" \033[1;97m{}\033[0m", warning_title), box_width - 2) + "\033[1;31m║\033[0m\n";
        screen += "\033[1;31m╠" + make_repeated_string("═", box_width - 2) + "╣\033[0m\n";
        screen += "\033[1;31m║\033[0m" + format_to_width(std::format(" \033[93m{}\033[0m", req_text), box_width - 2) + "\033[1;31m║\033[0m\n";
        screen += "\033[1;31m║\033[0m" + format_to_width(std::format(" {}", action_text), box_width - 2) + "\033[1;31m║\033[0m\n";
        screen += "\033[1;31m╚" + make_repeated_string("═", box_width - 2) + "╝\033[0m\n";

        for (int y = 0; y < pad_y; ++y) screen += "\n";

        (void)(::write(STDOUT_FILENO, screen.data(), screen.size()) == 0);
        ::fflush(stdout);
        return;
    }

    // Auto-fallback to FullConsole if the terminal isn't wide enough to comfortably fit the Split view.
    // 64-bit needs ~62 for left pane + 40 for right pane = ~105
    // 32-bit needs ~46 for left pane + 40 for right pane = ~90
    int split_threshold = simrv::xlen::kIsXLen64 ? 105 : 90;
    auto effective_layout = layout_;
    if (layout_ == TuiLayout::Split && term_width < split_threshold) {
        effective_layout = TuiLayout::FullConsole;
    }

    // Calculate dynamic pane widths to sum to EXACTLY term_width
    int left_pane_width = term_width - 2;
    int right_pane_width = term_width - 2;
    if (effective_layout == TuiLayout::Split) {
        left_pane_width = (term_width - 3) / 2;
        if (left_pane_width > 62) {
            left_pane_width = 62;
        }
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
    int total_wrapped = static_cast<int>(wrapped_lines.size());
    int start_idx = total_wrapped - num_rows - scroll_offset_;
    int end_idx = total_wrapped - scroll_offset_;
    if (start_idx < 0) start_idx = 0;
    if (end_idx < 0) end_idx = 0;
    if (end_idx > total_wrapped) end_idx = total_wrapped;

    int actual_drawn = end_idx - start_idx;
    if (actual_drawn < num_rows) {
        lines_to_draw_.insert(lines_to_draw_.end(), num_rows - actual_drawn, "");
    }
    if (actual_drawn > 0) {
        lines_to_draw_.insert(lines_to_draw_.end(), wrapped_lines.begin() + start_idx, wrapped_lines.begin() + end_idx);
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
    std::string status_badge;
    if (!status_override_.empty()) {
        status_badge = status_override_;
    } else {
        status_badge = paused_ ? "\033[1;43;30m PAUSED \033[0m" : "\033[1;42;30m RUNNING \033[0m";
    }

    std::string page_badge;
    if (reg_page_ == TuiRegPage::GPR) page_badge = "GPR";
    else if (reg_page_ == TuiRegPage::FPR) page_badge = "FPR";
    else page_badge = "VEC";

    std::string binary_name = machine_.s_fn_memimg;
    auto last_slash = binary_name.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        binary_name = binary_name.substr(last_slash + 1);
    }
    std::string left_text = std::format(" SimRV [{}] | Status: ", binary_name);
    std::string left_render = left_text + status_badge + std::format(" | Page: \033[1;36m{}\033[0m", page_badge);
    int left_printed_len = get_display_width(left_render);
    int pad_left = (effective_layout == TuiLayout::Split ? left_pane_width : term_width - 2) - left_printed_len;
    if (pad_left > 0) {
        left_render += std::string(static_cast<std::size_t>(pad_left), ' ');
    } else {
        left_render = format_to_width(left_render, effective_layout == TuiLayout::Split ? left_pane_width : term_width - 2);
    }

    std::string right_text;
    int right_text_display_len = 0;
    std::string color_prefix = "";
    std::string color_suffix = "";
    if (scroll_offset_ > 0) {
        color_prefix = "\033[1;5;30;43m";
        color_suffix = "\033[0m";
        right_text = std::format(" ═══ SCROLLBACK (Offset: -{}) [Press 'c'/'Enter' to Live] ═══ ", scroll_offset_);
        right_text_display_len = get_display_width(right_text);
    } else {
        right_text = std::format("Cycles: {} | Insns: {} | KIPS: {}", 
                                 format_with_commas(machine_.cpu.clint_mmio.mtime),
                                 format_with_commas(machine_.cpu.e_icount),
                                 format_with_commas(kips_));
        right_text_display_len = get_display_width(right_text);
    }
    int pad_right = (effective_layout == TuiLayout::Split ? right_pane_width : term_width - 2) - right_text_display_len;
    std::string right_render = right_text;
    if (pad_right > 0) {
        if (scroll_offset_ > 0) {
            right_render = std::string(static_cast<std::size_t>(pad_right / 2), ' ') + right_render + std::string(static_cast<std::size_t>((pad_right + 1) / 2), ' ');
            right_render = color_prefix + right_render + color_suffix;
        } else {
            right_render = std::string(static_cast<std::size_t>(pad_right), ' ') + right_render;
        }
    } else {
        right_render = format_to_width(right_render, effective_layout == TuiLayout::Split ? right_pane_width : term_width - 2);
        if (scroll_offset_ > 0) {
            right_render = color_prefix + right_render + color_suffix;
        }
    }

    if (effective_layout == TuiLayout::Split) {
        screen += "\033[1;94m║\033[0m" + left_render + "\033[1;94m│\033[0m" + right_render + "\033[1;94m║\033[0m\n";
        screen += "\033[1;94m╠" + make_repeated_string("═", left_pane_width) + "╪" + make_repeated_string("═", right_pane_width) + "╣\033[0m\n";
    } else if (effective_layout == TuiLayout::FullConsole) {
        screen += "\033[1;94m║\033[0m" + right_render + "\033[1;94m║\033[0m\n";
        screen += "\033[1;94m╠" + make_repeated_string("═", term_width - 2) + "╣\033[0m\n";
    } else {
        screen += "\033[1;94m║\033[0m" + left_render + "\033[1;94m║\033[0m\n";
        screen += "\033[1;94m╠" + make_repeated_string("═", term_width - 2) + "╣\033[0m\n";
    }

    // Rows 4 to end (num_rows)
    for (int i = 0; i < num_rows; ++i) {
        if (effective_layout == TuiLayout::Split) {
            std::string left = get_left_pane_row(i, left_pane_width);
            std::string right = get_right_pane_row(i, right_pane_width);
            screen += "\033[1;94m║\033[0m" + left + "\033[1;94m│\033[0m" + right + "\033[1;94m║\033[0m\n";
        } else if (effective_layout == TuiLayout::FullConsole) {
            std::string right = get_right_pane_row(i, term_width - 2);
            screen += "\033[1;94m║\033[0m" + right + "\033[1;94m║\033[0m\n";
        } else {
            std::string left = get_left_pane_row(i, term_width - 2);
            screen += "\033[1;94m║\033[0m" + left + "\033[1;94m║\033[0m\n";
        }
    }

    // Split border
    if (effective_layout == TuiLayout::Split) {
        screen += "\033[1;94m╠" + make_repeated_string("═", left_pane_width) + "╧" + make_repeated_string("═", right_pane_width) + "╣\033[0m\n";
    } else {
        screen += "\033[1;94m╠" + make_repeated_string("═", term_width - 2) + "╣\033[0m\n";
    }

    // Footer info
    std::string footer_text;
    if (paused_) {
        footer_text = " [Ctrl+Q] Quit | \033[1;92m[c] Continue\033[0m | [Space] Step | [Tab] Layout | [u/d] Scroll | [r] Reg Page ";
    } else {
        footer_text = " [Ctrl+Q] Quit | \033[1;93m[Ctrl+P] Pause\033[0m | [Tab] Layout | [u/d] Scroll | [r] Reg Page ";
    }
    int footer_len = get_display_width(footer_text);
    int pad_foot = (term_width - 2) - footer_len;
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
    int pad = width - history_size;
    if (pad > 0) {
        s += std::string(static_cast<std::size_t>(pad), ' ');
    }

    const char* blocks[8] = { " ", "▂", "▃", "▄", "▅", "▆", "▇", "█" };
    int start_hist = (history_size > width) ? (history_size - width) : 0;
    for (int i = start_hist; i < history_size; ++i) {
        double ratio = static_cast<double>(kips_history_[static_cast<std::size_t>(i)]) / static_cast<double>(max_val);
        int block_idx = static_cast<int>(ratio * 7.0);
        if (block_idx < 0) block_idx = 0;
        if (block_idx > 7) block_idx = 7;
        s += blocks[block_idx];
    }
    return s;
}

auto Tui::get_left_pane_row(int row_idx, int pane_width) -> std::string {
    if (cached_left_rows_.size() <= static_cast<std::size_t>(row_idx)) {
        cached_left_rows_.resize(30);
    }

    if (row_idx >= 0 && row_idx <= 24) {
        if (!paused_ && !cached_left_rows_[static_cast<std::size_t>(row_idx)].empty()) {
            return cached_left_rows_[static_cast<std::size_t>(row_idx)];
        }
    }

    auto get_row_uncached = [&]() -> std::string {
        auto& cpu = machine_.cpu;
        auto& st = cpu.state();
        int col_width = pane_width / 2;

        if (row_idx >= 0 && row_idx < 16) {
            int reg1 = row_idx;
            int reg2 = row_idx + 16;

            if (reg_page_ == TuiRegPage::GPR) {
                auto val1 = st.regs.read(static_cast<RegId>(reg1));
                auto val2 = st.regs.read(static_cast<RegId>(reg2));

                std::string name1 = kRegNames[static_cast<std::size_t>(reg1)];
                std::string name2 = kRegNames[static_cast<std::size_t>(reg2)];

                std::string col1_color = std::format(" \033[97mx{:<2}\033[0m/\033[36m{:<5}\033[0m: \033[92m0x{:0{}x}\033[0m", 
                                                     reg1, name1, val1, simrv::xlen::kXLenHexDigits);
                std::string col2_color = std::format(" \033[97mx{:<2}\033[0m/\033[36m{:<5}\033[0m: \033[92m0x{:0{}x}\033[0m", 
                                                     reg2, name2, val2, simrv::xlen::kXLenHexDigits);

                return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
            } else if (reg_page_ == TuiRegPage::FPR) {
                auto val1 = st.regs.read_fp(static_cast<RegId>(reg1));
                auto val2 = st.regs.read_fp(static_cast<RegId>(reg2));

                std::string name1 = kFpRegNames[static_cast<std::size_t>(reg1)];
                std::string name2 = kFpRegNames[static_cast<std::size_t>(reg2)];

                std::string col1_color = std::format(" \033[97mf{:<2}\033[0m/\033[36m{:<5}\033[0m: \033[92m0x{:016x}\033[0m", 
                                                     reg1, name1, val1);
                std::string col2_color = std::format(" \033[97mf{:<2}\033[0m/\033[36m{:<5}\033[0m: \033[92m0x{:016x}\033[0m", 
                                                     reg2, name2, val2);

                return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
            } else {
                // VEC page
                std::string col1_color = std::format(" \033[97mv{:<2}\033[0m       : \033[90m0x0000000000000000\033[0m", reg1);
                std::string col2_color = std::format(" \033[97mv{:<2}\033[0m       : \033[90m0x0000000000000000\033[0m", reg2);

                return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
            }
        }

        if (row_idx == 16) {
            std::string title = " ── CSRs & Privilege State ";
            std::string dashes = make_repeated_string("─", std::max(0, pane_width - static_cast<int>(title.length())));
            return "\033[1;90m" + format_to_width(title + dashes, pane_width) + "\033[0m";
        }

        if (row_idx == 17) {
            std::string priv_str = (st.priv == PrivilegeLevel::Machine)      ? "Machine"
                                   : (st.priv == PrivilegeLevel::Supervisor) ? "Supervisor"
                                                                             : "User";
            std::string col1_color = std::format(" \033[97mpc\033[0m      : \033[92m0x{:0{}x}\033[0m", st.pc, simrv::xlen::kXLenHexDigits);
            std::string col2_color = std::format(" \033[97mpriv\033[0m    : \033[35m{}\033[0m", priv_str);

            return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
        }

        if (row_idx == 18) {
            std::string misa_str = simrv::xlen::resolve_misa_string(st.misa);
            if (misa_str.size() >= 4 && (misa_str.starts_with("RV32") || misa_str.starts_with("RV64"))) {
                misa_str = misa_str.substr(4);
            }

            std::string col1_color = std::format(" \033[97mmstatus\033[0m: \033[36m0x{:0{}x}\033[0m", st.mstatus, simrv::xlen::kXLenHexDigits);
            std::string col2_color = std::format(" \033[97mmisa\033[0m   : \033[36m{}\033[0m", misa_str);

            return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
        }

        if (row_idx == 19) {
            std::string col1_color = std::format(" \033[97mmie\033[0m     : \033[36m0x{:0{}x}\033[0m", st.mie, simrv::xlen::kXLenHexDigits);
            std::string col2_color = std::format(" \033[97mmip\033[0m    : \033[36m0x{:0{}x}\033[0m", st.mip, simrv::xlen::kXLenHexDigits);

            return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
        }

        if (row_idx == 20) {
            std::string col1_color = std::format(" \033[97mmtvec\033[0m    : \033[36m0x{:0{}x}\033[0m", st.mtvec, simrv::xlen::kXLenHexDigits);
            std::string col2_color = std::format(" \033[97mmepc\033[0m   : \033[36m0x{:0{}x}\033[0m", st.mepc, simrv::xlen::kXLenHexDigits);

            return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
        }

        if (row_idx == 21) {
            std::string col1_color = std::format(" \033[97mstvec\033[0m    : \033[36m0x{:0{}x}\033[0m", st.stvec, simrv::xlen::kXLenHexDigits);
            std::string col2_color = std::format(" \033[97msepc\033[0m   : \033[36m0x{:0{}x}\033[0m", st.sepc, simrv::xlen::kXLenHexDigits);

            return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
        }

        if (row_idx == 22) {
            std::string col1_color = std::format(" \033[97mmtval\033[0m  : \033[36m0x{:0{}x}\033[0m", st.mtval, simrv::xlen::kXLenHexDigits);
            std::string col2_color = std::format(" \033[97msatp\033[0m   : \033[36m0x{:0{}x}\033[0m", st.satp, simrv::xlen::kXLenHexDigits);

            return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
        }

        if (row_idx == 23) {
            std::string col1_color = std::format(" \033[97mscause\033[0m  : \033[36m0x{:0{}x}\033[0m", st.scause, simrv::xlen::kXLenHexDigits);
            std::string col2_color = std::format(" \033[97mstval\033[0m  : \033[36m0x{:0{}x}\033[0m", st.stval, simrv::xlen::kXLenHexDigits);

            return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
        }

        if (row_idx == 24) {
            std::string col1_color = std::format(" \033[97mmedeleg\033[0m : \033[36m0x{:0{}x}\033[0m", st.medeleg, simrv::xlen::kXLenHexDigits);
            std::string col2_color = std::format(" \033[97mmideleg\033[0m: \033[36m0x{:0{}x}\033[0m", st.mideleg, simrv::xlen::kXLenHexDigits);

            return format_to_width(col1_color, col_width) + format_to_width(col2_color, pane_width - col_width);
        }

        return "";
    };

    if (row_idx >= 0 && row_idx <= 24) {
        std::string res = get_row_uncached();
        cached_left_rows_[static_cast<std::size_t>(row_idx)] = res;
        return res;
    }

    auto& cpu = machine_.cpu;

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

void Tui::cycle_reg_page() {
    bool has_f = (machine_.cpu.state().misa & (1ULL << ('f' - 'a'))) != 0;
    bool has_d = (machine_.cpu.state().misa & (1ULL << ('d' - 'a'))) != 0;
    bool has_v = (machine_.cpu.state().misa & (1ULL << ('v' - 'a'))) != 0;

    if (reg_page_ == TuiRegPage::GPR) {
        if (has_f || has_d) reg_page_ = TuiRegPage::FPR;
        else if (has_v) reg_page_ = TuiRegPage::VEC;
        else reg_page_ = TuiRegPage::GPR;
    } else if (reg_page_ == TuiRegPage::FPR) {
        if (has_v) reg_page_ = TuiRegPage::VEC;
        else reg_page_ = TuiRegPage::GPR;
    } else {
        reg_page_ = TuiRegPage::GPR;
    }
    cached_left_rows_.clear();
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

}  // namespace simrv::device
