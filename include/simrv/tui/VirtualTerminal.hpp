/**
 * @file VirtualTerminal.hpp
 * @brief Virtual terminal emulator screen buffer and ANSI parser for SimRV TUI.
 */
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace simrv::tui {

extern bool g_high_contrast;
extern std::array<const char*, 16> g_theme_palette;
extern std::array<const char*, 16> g_theme_bg_palette;

struct Cell {
    std::string ch = " ";
    uint8_t fg = 7;
    uint8_t bg = 0;
    bool bold = false;
    bool underline = false;
    bool reverse = false;

    auto operator==(const Cell&) const -> bool = default;
};

// Sakura Pastel Theme Colors mapping for Virtual Terminal emulation
inline constexpr std::array<const char*, 16> kSakuraPalette = {
    "\033[38;5;234m",  // 0: Black (very dark gray)
    "\033[38;5;210m",  // 1: Red (Sakura Coral)
    "\033[38;5;121m",  // 2: Green (Sakura Mint)
    "\033[38;5;223m",  // 3: Yellow (Sakura Peach)
    "\033[38;5;117m",  // 4: Blue (Sakura Sky)
    "\033[38;5;183m",  // 5: Magenta (Sakura Val/Lavender)
    "\033[38;5;122m",  // 6: Cyan (Teal/Sky)
    "\033[38;5;254m",  // 7: White (Sakura Text)
    "\033[38;5;244m",  // 8: Bright Black (Muted gray)
    "\033[38;5;211m",  // 9: Bright Red (Sakura Pink)
    "\033[38;5;121m",  // 10: Bright Green (Sakura Mint)
    "\033[38;5;223m",  // 11: Bright Yellow (Sakura Peach)
    "\033[38;5;117m",  // 12: Bright Blue (Sakura Sky)
    "\033[38;5;183m",  // 13: Bright Magenta (Lavender)
    "\033[38;5;122m",  // 14: Bright Cyan (Teal)
    "\033[38;5;255m"   // 15: Bright White (Pure pastel white)
};

inline constexpr std::array<const char*, 16> kSakuraBgPalette = {
    "\033[48;5;234m",  // 0: Black
    "\033[48;5;210m",  // 1: Red
    "\033[48;5;121m",  // 2: Green
    "\033[48;5;223m",  // 3: Yellow
    "\033[48;5;117m",  // 4: Blue
    "\033[48;5;183m",  // 5: Magenta
    "\033[48;5;122m",  // 6: Cyan
    "\033[48;5;254m",  // 7: White
    "\033[48;5;244m",  // 8: Bright Black
    "\033[48;5;211m",  // 9: Bright Red
    "\033[48;5;121m",  // 10: Bright Green
    "\033[48;5;223m",  // 11: Bright Yellow
    "\033[48;5;117m",  // 12: Bright Blue
    "\033[48;5;183m",  // 13: Bright Magenta
    "\033[48;5;122m",  // 14: Bright Cyan
    "\033[48;5;255m"   // 15: Bright White
};

inline constexpr std::array<const char*, 16> kHighContrastPalette = {
    "\033[30m",  // 0: Black
    "\033[31m",  // 1: Red
    "\033[32m",  // 2: Green
    "\033[33m",  // 3: Yellow
    "\033[34m",  // 4: Blue
    "\033[35m",  // 5: Magenta
    "\033[36m",  // 6: Cyan
    "\033[37m",  // 7: White
    "\033[90m",  // 8: Bright Black
    "\033[91m",  // 9: Bright Red
    "\033[92m",  // 10: Bright Green
    "\033[93m",  // 11: Bright Yellow
    "\033[94m",  // 12: Bright Blue
    "\033[95m",  // 13: Bright Magenta
    "\033[96m",  // 14: Bright Cyan
    "\033[97m"   // 15: Bright White
};

inline constexpr std::array<const char*, 16> kHighContrastBgPalette = {
    "\033[40m",   // 0: Black
    "\033[41m",   // 1: Red
    "\033[42m",   // 2: Green
    "\033[43m",   // 3: Yellow
    "\033[44m",   // 4: Blue
    "\033[45m",   // 5: Magenta
    "\033[46m",   // 6: Cyan
    "\033[47m",   // 7: White
    "\033[100m",  // 8: Bright Black
    "\033[101m",  // 9: Bright Red
    "\033[102m",  // 10: Bright Green
    "\033[103m",  // 11: Bright Yellow
    "\033[104m",  // 12: Bright Blue
    "\033[105m",  // 13: Bright Magenta
    "\033[106m",  // 14: Bright Cyan
    "\033[107m"   // 15: Bright White
};

class VirtualTerminal {
   public:
    VirtualTerminal(int w = 80, int h = 24) { resize(w, h); }

    void resize(int new_width, int new_height) {
        if (new_width <= 0) new_width = 80;
        if (new_height <= 0) new_height = 24;

        if (new_width == width_ && new_height == height_) {
            return;
        }

        // Adjust rows
        if (new_height < height_) {
            // Shrink: push top lines to scrollback
            int diff = height_ - new_height;
            for (int i = 0; i < diff; ++i) {
                scrollback_.push_back(cells_[i]);
            }
            cells_.erase(cells_.begin(), cells_.begin() + diff);
        } else if (new_height > height_) {
            // Grow: add new lines at the bottom
            int diff = new_height - height_;
            for (int i = 0; i < diff; ++i) {
                cells_.push_back(
                    std::vector<Cell>(new_width, Cell{.ch = " ", .fg = 7, .bg = current_attr_.bg}));
            }
        }

        // Adjust width of all rows
        for (auto& row : cells_) {
            row.resize(new_width, Cell{.ch = " ", .fg = 7, .bg = current_attr_.bg});
        }
        for (auto& row : scrollback_) {
            row.resize(new_width, Cell{.ch = " ", .fg = 7, .bg = current_attr_.bg});
        }

        width_ = new_width;
        height_ = new_height;

        // Clamp cursor
        cursor_x_ = std::clamp(cursor_x_, 0, width_ - 1);
        cursor_y_ = std::clamp(cursor_y_, 0, height_ - 1);
    }

    void write_char(char ch) {
        // Handle ANSI/ESC state machine
        if (ansi_state_ == AnsiState::Esc) {
            handle_esc_char(ch);
            return;
        }
        if (ansi_state_ == AnsiState::G0G1) {
            ansi_state_ = AnsiState::Normal;
            return;
        }
        if (ansi_state_ == AnsiState::Csi) {
            handle_csi_char(ch);
            return;
        }

        if (ch == '\x1b') {
            ansi_state_ = AnsiState::Esc;
            return;
        }

        // Handle UTF-8 assembly
        unsigned char uc = static_cast<unsigned char>(ch);
        if (!utf8_buf_.empty()) {
            if ((uc & 0xC0) == 0xC0) {
                // New lead byte while already building: flush old malformed buffer
                for (char c : utf8_buf_) {
                    if (c >= 32) write_cell_string(std::string(1, c));
                }
                utf8_buf_.clear();
                utf8_buf_ += ch;
                utf8_len_ = get_utf8_len(uc);
            } else {
                utf8_buf_ += ch;
                if (utf8_buf_.size() >= utf8_len_) {
                    write_cell_string(utf8_buf_);
                    utf8_buf_.clear();
                }
            }
            return;
        } else if ((uc & 0x80) != 0) {
            utf8_buf_ += ch;
            utf8_len_ = get_utf8_len(uc);
            return;
        }

        // Interpret normal/control characters
        if (ch == '\n') {
            cursor_x_ = 0;
            cursor_y_++;
            if (cursor_y_ >= height_) {
                scroll_up();
                cursor_y_ = height_ - 1;
            }
        } else if (ch == '\r') {
            cursor_x_ = 0;
        } else if (ch == '\t') {
            int next_x = (cursor_x_ + 8) & ~7;
            cursor_x_ = std::min(next_x, width_ - 1);
        } else if (ch == '\b' || ch == 127) {
            cursor_x_ = std::max(0, cursor_x_ - 1);
        } else if (ch >= 32) {
            write_cell_string(std::string(1, ch));
        }
    }

    void write_string(const std::string& str) {
        for (char ch : str) {
            write_char(ch);
        }
    }

    [[nodiscard]] auto get_cursor_x() const -> int { return cursor_x_; }
    [[nodiscard]] auto get_cursor_y() const -> int { return cursor_y_; }
    [[nodiscard]] auto is_cursor_visible() const -> bool { return cursor_visible_; }

    [[nodiscard]] auto get_lines_count() const -> int {
        return static_cast<int>(scrollback_.size() + cells_.size());
    }

    [[nodiscard]] auto get_scrollback_size() const -> int {
        return static_cast<int>(scrollback_.size());
    }

    [[nodiscard]] auto get_line_as_string(int idx, int width_limit,
                                          bool draw_cursor_at_x = false) const -> std::string {
        const std::vector<Cell>* row_ptr = nullptr;
        int s_size = static_cast<int>(scrollback_.size());
        if (idx < s_size) {
            row_ptr = &scrollback_[idx];
        } else {
            int cells_idx = idx - s_size;
            if (cells_idx >= 0 && cells_idx < static_cast<int>(cells_.size())) {
                row_ptr = &cells_[cells_idx];
            }
        }

        if (!row_ptr) {
            return std::string(static_cast<std::size_t>(width_limit), ' ');
        }

        const auto& row = *row_ptr;
        std::string res;
        Cell prev_cell;
        // Start with default/reset attributes
        res += "\033[0m";

        int limit = std::min(width_limit, static_cast<int>(row.size()));
        for (int x = 0; x < limit; ++x) {
            Cell cell = row[x];
            if (draw_cursor_at_x && x == cursor_x_) {
                cell.reverse = !cell.reverse;
            }

            if (!(cell == prev_cell) || x == 0 ||
                (draw_cursor_at_x && (x == cursor_x_ || x == cursor_x_ + 1))) {
                res += "\033[0m";

                // 1. Text attributes
                if (cell.bold) res += "\033[1m";
                if (cell.underline) res += "\033[4m";
                if (cell.reverse) res += "\033[7m";

                // 2. FG color mapping to Active Theme
                if (cell.fg < 16) {
                    res += g_theme_palette.at(cell.fg);
                } else {
                    res += std::format("\033[38;5;{}m", cell.fg);
                }

                // 3. BG color mapping to Active Theme
                if (cell.bg < 16) {
                    if (cell.bg != 0) {
                        res += g_theme_bg_palette.at(cell.bg);
                    }
                } else {
                    res += std::format("\033[48;5;{}m", cell.bg);
                }

                prev_cell = cell;
            }
            res += cell.ch;
        }

        // Pad with empty cells
        if (limit < width_limit) {
            res += "\033[0m";
            res += std::string(static_cast<std::size_t>(width_limit - limit), ' ');
        }

        res += "\033[0m";
        return res;
    }

    void set_scroll_offset_callback(std::function<void(int)> cb) { scroll_offset_cb_ = cb; }

   private:
    enum class AnsiState { Normal, Esc, G0G1, Csi };

    static auto get_utf8_len(unsigned char b) -> int {
        if ((b & 0x80) == 0) return 1;
        if ((b & 0xE0) == 0xC0) return 2;
        if ((b & 0xF0) == 0xE0) return 3;
        if ((b & 0xF8) == 0xF0) return 4;
        return 1;
    }

    void scroll_up() {
        scrollback_.push_back(cells_.front());
        cells_.pop_front();
        cells_.push_back(
            std::vector<Cell>(width_, Cell{.ch = " ", .fg = 7, .bg = current_attr_.bg}));
        if (scrollback_.size() > 9999) {
            scrollback_.pop_front();
        }
        if (scroll_offset_cb_) {
            scroll_offset_cb_(1);
        }
    }

    void write_cell_string(const std::string& s) {
        if (cursor_y_ >= 0 && cursor_y_ < height_ && cursor_x_ >= 0 && cursor_x_ < width_) {
            cells_[cursor_y_][cursor_x_] = Cell{.ch = s,
                                                .fg = current_attr_.fg,
                                                .bg = current_attr_.bg,
                                                .bold = current_attr_.bold,
                                                .underline = current_attr_.underline,
                                                .reverse = current_attr_.reverse};
        }
        cursor_x_++;
        if (cursor_x_ >= width_) {
            cursor_x_ = 0;
            cursor_y_++;
            if (cursor_y_ >= height_) {
                scroll_up();
                cursor_y_ = height_ - 1;
            }
        }
    }

    void handle_esc_char(char ch) {
        if (ch == '[') {
            ansi_state_ = AnsiState::Csi;
            csi_buf_.clear();
        } else if (ch == '(' || ch == ')') {
            ansi_state_ = AnsiState::G0G1;
        } else {
            ansi_state_ = AnsiState::Normal;
            if (ch == 'M') {  // Reverse Index
                cursor_y_--;
                if (cursor_y_ < 0) {
                    cells_.push_front(std::vector<Cell>(
                        width_, Cell{.ch = " ", .fg = 7, .bg = current_attr_.bg}));
                    cells_.pop_back();
                    cursor_y_ = 0;
                }
            } else if (ch == 'D') {  // Index
                cursor_y_++;
                if (cursor_y_ >= height_) {
                    scroll_up();
                    cursor_y_ = height_ - 1;
                }
            } else if (ch == 'E') {  // Next Line
                cursor_x_ = 0;
                cursor_y_++;
                if (cursor_y_ >= height_) {
                    scroll_up();
                    cursor_y_ = height_ - 1;
                }
            } else if (ch == 'c') {  // Reset terminal
                reset_terminal();
            }
        }
    }

    void handle_csi_char(char ch) {
        if (ch >= 0x30 && ch <= 0x3F) {
            csi_buf_ += ch;
        } else if (ch >= 0x20 && ch <= 0x2F) {
            csi_buf_ += ch;
        } else if (ch >= 0x40 && ch <= 0x7E) {
            ansi_state_ = AnsiState::Normal;
            execute_csi_command(ch);
        } else {
            ansi_state_ = AnsiState::Normal;
        }
    }

    void execute_csi_command(char cmd) {
        bool is_private = !csi_buf_.empty() && csi_buf_[0] == '?';
        std::string_view sv = csi_buf_;
        if (is_private) {
            sv.remove_prefix(1);
        }

        std::vector<int> params;
        std::size_t start = 0;
        while (start < sv.size()) {
            std::size_t end = sv.find(';', start);
            std::string_view part = sv.substr(start, end - start);
            if (part.empty()) {
                params.push_back(0);
            } else {
                int val = 0;
                std::from_chars(part.data(), part.data() + part.size(), val);
                params.push_back(val);
            }
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1;
        }

        if (cmd == 'm') {  // SGR
            if (params.empty()) {
                params.push_back(0);
            }
            for (int p : params) {
                if (p == 0) {
                    current_attr_ = Cell{.ch = " ", .fg = 7, .bg = 0};
                } else if (p == 1) {
                    current_attr_.bold = true;
                } else if (p == 22) {
                    current_attr_.bold = false;
                } else if (p == 4) {
                    current_attr_.underline = true;
                } else if (p == 24) {
                    current_attr_.underline = false;
                } else if (p == 7) {
                    current_attr_.reverse = true;
                } else if (p == 27) {
                    current_attr_.reverse = false;
                } else if (p >= 30 && p <= 37) {
                    current_attr_.fg = p - 30;
                } else if (p == 39) {
                    current_attr_.fg = 7;
                } else if (p >= 40 && p <= 47) {
                    current_attr_.bg = p - 40;
                } else if (p == 49) {
                    current_attr_.bg = 0;
                } else if (p >= 90 && p <= 97) {
                    current_attr_.fg = p - 90 + 8;
                } else if (p >= 100 && p <= 107) {
                    current_attr_.bg = p - 100 + 8;
                }
            }
        } else if (cmd == 'H' || cmd == 'f') {  // Cursor Position
            int r = params.size() > 0 ? params[0] : 1;
            int c = params.size() > 1 ? params[1] : 1;
            cursor_y_ = std::clamp(r - 1, 0, height_ - 1);
            cursor_x_ = std::clamp(c - 1, 0, width_ - 1);
        } else if (cmd == 'A') {  // Cursor Up
            int n = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
            cursor_y_ = std::max(0, cursor_y_ - n);
        } else if (cmd == 'B') {  // Cursor Down
            int n = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
            cursor_y_ = std::min(height_ - 1, cursor_y_ + n);
        } else if (cmd == 'C') {  // Cursor Forward
            int n = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
            cursor_x_ = std::min(width_ - 1, cursor_x_ + n);
        } else if (cmd == 'D') {  // Cursor Backward
            int n = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
            cursor_x_ = std::max(0, cursor_x_ - n);
        } else if (cmd == 'G') {  // Cursor Horizontal Absolute
            int c = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
            cursor_x_ = std::clamp(c - 1, 0, width_ - 1);
        } else if (cmd == 'd') {  // Cursor Vertical Absolute
            int r = (params.size() > 0 && params[0] > 0) ? params[0] : 1;
            cursor_y_ = std::clamp(r - 1, 0, height_ - 1);
        } else if (cmd == 'J') {  // Erase Display
            int mode = params.size() > 0 ? params[0] : 0;
            if (mode == 0) {
                for (int x = cursor_x_; x < width_; ++x)
                    cells_[cursor_y_][x] = Cell{.ch = " ", .fg = 7, .bg = current_attr_.bg};
                for (int y = cursor_y_ + 1; y < height_; ++y) {
                    std::fill(cells_[y].begin(), cells_[y].end(),
                              Cell{.ch = " ", .fg = 7, .bg = current_attr_.bg});
                }
            } else if (mode == 1) {
                for (int y = 0; y < cursor_y_; ++y) {
                    std::fill(cells_[y].begin(), cells_[y].end(),
                              Cell{.ch = " ", .fg = 7, .bg = current_attr_.bg});
                }
                for (int x = 0; x <= cursor_x_; ++x)
                    cells_[cursor_y_][x] = Cell{.ch = " ", .fg = 7, .bg = current_attr_.bg};
            } else if (mode == 2 || mode == 3) {
                for (auto& row : cells_) {
                    std::fill(row.begin(), row.end(),
                              Cell{.ch = " ", .fg = 7, .bg = current_attr_.bg});
                }
            }
        } else if (cmd == 'K') {  // Erase Line
            int mode = params.size() > 0 ? params[0] : 0;
            if (mode == 0) {
                for (int x = cursor_x_; x < width_; ++x)
                    cells_[cursor_y_][x] = Cell{.ch = " ", .fg = 7, .bg = current_attr_.bg};
            } else if (mode == 1) {
                for (int x = 0; x <= cursor_x_; ++x)
                    cells_[cursor_y_][x] = Cell{.ch = " ", .fg = 7, .bg = current_attr_.bg};
            } else if (mode == 2) {
                std::fill(cells_[cursor_y_].begin(), cells_[cursor_y_].end(),
                          Cell{.ch = " ", .fg = 7, .bg = current_attr_.bg});
            }
        } else if (cmd == 'h' && is_private) {
            for (int p : params) {
                if (p == 25) cursor_visible_ = true;
            }
        } else if (cmd == 'l' && is_private) {
            for (int p : params) {
                if (p == 25) cursor_visible_ = false;
            }
        } else if (cmd == 's' && !is_private) {
            saved_x_ = cursor_x_;
            saved_y_ = cursor_y_;
        } else if (cmd == 'u' && !is_private) {
            cursor_x_ = saved_x_;
            cursor_y_ = saved_y_;
        }
    }

    void reset_terminal() {
        for (auto& row : cells_) {
            std::fill(row.begin(), row.end(), Cell{.ch = " ", .fg = 7, .bg = 0});
        }
        cursor_x_ = 0;
        cursor_y_ = 0;
        current_attr_ = Cell{.ch = " ", .fg = 7, .bg = 0};
        ansi_state_ = AnsiState::Normal;
        utf8_buf_.clear();
    }

    int width_ = 0;
    int height_ = 0;
    int cursor_x_ = 0;
    int cursor_y_ = 0;
    int saved_x_ = 0;
    int saved_y_ = 0;
    bool cursor_visible_ = true;
    Cell current_attr_;

    std::deque<std::vector<Cell>> cells_;
    std::deque<std::vector<Cell>> scrollback_;

    AnsiState ansi_state_ = AnsiState::Normal;
    std::string csi_buf_;

    std::string utf8_buf_;
    std::size_t utf8_len_ = 0;

    std::function<void(int)> scroll_offset_cb_;
};

}  // namespace simrv::tui
