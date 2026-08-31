/**
 * @file TuiFrameRenderer.cpp
 * @brief Pure TUI frame composition implementation.
 */
#include "simrv/tui/TuiFrameRenderer.hpp"

#include <format>

#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui {

namespace {

void append_block_lines(std::vector<std::string>& lines, std::string_view block) {
    std::size_t start = 0;
    while (start < block.size()) {
        const std::size_t pos = block.find('\n', start);
        if (pos == std::string_view::npos) {
            lines.emplace_back(block.substr(start));
            return;
        }
        lines.emplace_back(block.substr(start, pos - start));
        start = pos + 1;
    }
}

[[nodiscard]] auto fit_pane_row(std::string row, int width) -> std::string {
    if (get_display_width(row) == width) return row;
    return format_to_width(row, width);
}

[[nodiscard]] auto strip_ansi(std::string_view row) -> std::string {
    std::string plain;
    bool escape = false;
    bool csi = false;
    for (char ch : row) {
        if (ch == '\033') {
            escape = true;
            csi = false;
        } else if (escape) {
            if (!csi && ch == '[') {
                csi = true;
            } else if (!csi || (ch >= '@' && ch <= '~')) {
                escape = false;
                csi = false;
            }
        } else {
            plain.push_back(ch);
        }
    }
    return plain;
}

[[nodiscard]] auto is_horizontal_rule(const std::string& row, const char* horiz) -> bool {
    const std::string plain = strip_ansi(row);
    return plain.starts_with(horiz) && plain.ends_with(horiz);
}

}  // namespace

auto compose_multi_frame_lines(const FrameGeometry& frame, int terminal_width,
                               framework::ColumnWidths col_widths, std::string_view header_block,
                               std::string_view footer_block, const ColumnRowRenderer& render_col)
    -> std::vector<std::string> {
    if (!frame.renderable || terminal_width < kFrameRendererMinimumTerminalWidth ||
        col_widths.count == 0)
        return {};

    std::vector<std::string> lines;
    lines.reserve(static_cast<std::size_t>(frame.frame_rows));
    append_block_lines(lines, header_block);

    const auto style = get_active_theme_style();
    const bool is_ansi = (style == TuiThemeStyle::ClassicAnsi);
    const char* border_v = is_ansi ? "|" : "║";
    const char* div_v = is_ansi ? "|" : "│";

    for (int row = 0; row < frame.content_rows; ++row) {
        std::string cells[4];
        bool is_rule[4] = {false, false, false, false};
        for (size_t col = 0; col < col_widths.count; ++col) {
            const int w = col_widths.widths[col];
            cells[col] = fit_pane_row(render_col(col, row, w), w);
            is_rule[col] = is_horizontal_rule(cells[col], is_ansi ? "-" : "─");
        }

        const char* left_border = is_rule[0] ? (is_ansi ? "+" : "╟") : border_v;
        std::string line;
        line += std::format("{}{}\033[0m", kThemeBorder, left_border);

        for (size_t col = 0; col < col_widths.count; ++col) {
            line += cells[col];
            if (col + 1 < col_widths.count) {
                const char* div = div_v;
                if (is_rule[col] && is_rule[col + 1]) {
                    div = is_ansi ? "+" : "┼";
                } else if (is_rule[col]) {
                    div = is_ansi ? "+" : "┤";
                } else if (is_rule[col + 1]) {
                    div = is_ansi ? "+" : "├";
                }
                line += std::format("{}{}\033[0m", kThemeBorder, div);
            }
        }

        const bool last_rule = is_rule[col_widths.count - 1];
        const char* right_border =
            (col_widths.count == 1 && last_rule) ? (is_ansi ? "+" : "╢") : border_v;
        line += std::format("{}{}\033[0m", kThemeBorder, right_border);
        lines.push_back(std::move(line));
    }

    if (is_ansi) {
        std::string bottom = std::format("{}+", kThemeBorder);
        for (size_t col = 0; col < col_widths.count; ++col) {
            bottom += make_repeated_string("-", col_widths.widths[col]);
            bottom += "+";
        }
        bottom += "\033[0m";
        lines.push_back(std::move(bottom));
    } else {
        std::string bottom = std::format("{}╠", kThemeBorder);
        for (size_t col = 0; col < col_widths.count; ++col) {
            bottom += make_repeated_string("═", col_widths.widths[col]);
            if (col + 1 < col_widths.count) {
                bottom += "╧";
            }
        }
        bottom += std::format("╣\033[0m");
        lines.push_back(std::move(bottom));
    }

    append_block_lines(lines, footer_block);
    return lines;
}

auto compose_frame_lines(const FrameGeometry& frame, int terminal_width, TuiLayout layout,
                         std::string_view header_block, std::string_view footer_block,
                         const PaneRowRenderer& render_left, const PaneRowRenderer& render_right)
    -> std::vector<std::string> {
    if (!frame.renderable || terminal_width < kFrameRendererMinimumTerminalWidth) return {};

    auto col_widths = framework::multi_column_widths(terminal_width, layout);
    return compose_multi_frame_lines(frame, terminal_width, col_widths, header_block, footer_block,
                                     [&](size_t col_idx, int row, int width) -> std::string {
                                         if (col_widths.count == 1) {
                                             if (layout == TuiLayout::FullRight) {
                                                 return render_right(row, width);
                                             }
                                             return render_left(row, width);
                                         }
                                         if (col_idx == 0) {
                                             return render_left(row, width);
                                         }
                                         return render_right(row, width);
                                     });
}

}  // namespace simrv::tui
