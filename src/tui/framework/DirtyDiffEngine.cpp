/**
 * @file DirtyDiffEngine.cpp
 * @brief High-performance cell delta diffing and minimal ANSI escape emitter.
 */
#include "simrv/tui/framework/DirtyDiffEngine.hpp"

#include <format>

namespace simrv::tui::framework {

namespace {

void encode_utf8(char32_t cp, std::string& out) {
    if (cp < 0x80U) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800U) {
        out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else if (cp < 0x10000U) {
        out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
}

class TerminalSgrState {
   public:
    explicit TerminalSgrState(bool truecolor) : truecolor_(truecolor) {}

    void apply_style(const Style& target, std::string& out) {
        if (target == active_style_ && style_active_) return;

        // If target requires resetting an active attribute that cannot be turned off individually,
        // or if resetting to default style:
        const bool need_reset =
            style_active_ && ((active_style_.attr.bold && !target.attr.bold) ||
                              (active_style_.attr.dim && !target.attr.dim) ||
                              (active_style_.attr.italic && !target.attr.italic) ||
                              (active_style_.attr.underline && !target.attr.underline) ||
                              (active_style_.attr.reverse && !target.attr.reverse) ||
                              (!active_style_.fg.is_default() && target.fg.is_default()) ||
                              (!active_style_.bg.is_default() && target.bg.is_default()));

        if (need_reset) {
            out += "\033[0m";
            active_style_ = {};
            style_active_ = false;
        }

        if (target.attr.bold && !active_style_.attr.bold) out += "\033[1m";
        if (target.attr.dim && !active_style_.attr.dim) out += "\033[2m";
        if (target.attr.italic && !active_style_.attr.italic) out += "\033[3m";
        if (target.attr.underline && !active_style_.attr.underline) out += "\033[4m";
        if (target.attr.reverse && !active_style_.attr.reverse) out += "\033[7m";

        if (!(target.fg == active_style_.fg)) {
            out += target.fg.to_fg_ansi(truecolor_);
        }
        if (!(target.bg == active_style_.bg)) {
            out += target.bg.to_bg_ansi(truecolor_);
        }

        active_style_ = target;
        style_active_ = true;
    }

    void reset(std::string& out) {
        if (style_active_) {
            out += "\033[0m";
            active_style_ = {};
            style_active_ = false;
        }
    }

   private:
    bool truecolor_{true};
    Style active_style_{};
    bool style_active_{false};
};

}  // namespace

auto DirtyDiffEngine::emit_delta(const ScreenBuffer& current, const ScreenBuffer& previous,
                                 std::string& out, bool force_redraw) -> DiffStats {
    DiffStats stats;
    const int width = current.width();
    const int height = current.height();
    const bool size_mismatch = (width != previous.width() || height != previous.height());
    const bool full_redraw = force_redraw || size_mismatch;

    const std::size_t start_len = out.size();
    TerminalSgrState sgr(truecolor_supported_);

    int cursor_x = -1;
    int cursor_y = -1;

    for (int y = 0; y < height; ++y) {
        int x = 0;
        bool row_dirty = false;
        while (x < width) {
            ++stats.cells_examined;
            const auto& cur_cell = current.cell(x, y);

            // Skip terminal graphic passthrough areas
            if (cur_cell.kind == CellKind::GraphicPassthrough) {
                ++x;
                cursor_x = -1;
                cursor_y = -1;
                continue;
            }

            const bool dirty = full_redraw || !(cur_cell == previous.cell(x, y));
            if (!dirty) {
                ++x;
                cursor_x = -1;
                cursor_y = -1;
                continue;
            }

            row_dirty = true;
            // Found dirty cell. Gather run of contiguous cells with identical Style.
            const Style run_style = cur_cell.style;
            const int run_start_x = x;
            int run_end_x = x;

            while (run_end_x < width) {
                const auto& c = current.cell(run_end_x, y);
                if (c.kind == CellKind::GraphicPassthrough) break;
                const bool cell_dirty = full_redraw || !(c == previous.cell(run_end_x, y));
                if (!cell_dirty || !(c.style == run_style)) break;
                ++run_end_x;
            }

            // Position cursor if needed (1-indexed for terminal)
            const int term_row = y + 1;
            const int term_col = run_start_x + 1;

            if (cursor_y != term_row || cursor_x != term_col) {
                out += std::format("\033[{};{}H", term_row, term_col);
                cursor_y = term_row;
                cursor_x = term_col;
            }

            sgr.apply_style(run_style, out);

            // Output all cells in this run
            for (int rx = run_start_x; rx < run_end_x; ++rx) {
                const auto& cell_to_draw = current.cell(rx, y);
                ++stats.cells_dirty;
                if (cell_to_draw.kind != CellKind::WideTail) {
                    encode_utf8(cell_to_draw.codepoint, out);
                }
                ++cursor_x;
            }

            ++stats.runs_emitted;
            x = run_end_x;
        }

        if (row_dirty) {
            ++stats.rows_dirty;
        } else {
            ++stats.rows_clean;
        }
    }

    sgr.reset(out);
    stats.bytes_emitted = out.size() - start_len;
    return stats;
}

}  // namespace simrv::tui::framework
