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

}  // namespace

auto compose_frame_lines(const FrameGeometry& frame, int terminal_width, TuiLayout layout,
                         std::string_view header_block, std::string_view footer_block,
                         const PaneRowRenderer& render_left, const PaneRowRenderer& render_right)
    -> std::vector<std::string> {
    if (!frame.renderable || terminal_width < kMinimumTerminalWidth) return {};

    std::vector<std::string> lines;
    lines.reserve(static_cast<std::size_t>(frame.frame_rows));
    append_block_lines(lines, header_block);

    const auto style = get_active_theme_style();
    const bool is_ansi = (style == TuiThemeStyle::ClassicAnsi);
    const char* border_v = is_ansi ? "|" : "║";
    const char* div_v = is_ansi ? "|" : "│";

    for (int row = 0; row < frame.content_rows; ++row) {
        if (layout == TuiLayout::Split) {
            lines.push_back(std::format(
                "{}{}\033[0m{}{}{}\033[0m{}{}{}\033[0m", kThemeBorder, border_v,
                fit_pane_row(render_left(row, frame.panes.left), frame.panes.left), kThemeBorder,
                div_v, fit_pane_row(render_right(row, frame.panes.right), frame.panes.right),
                kThemeBorder, border_v));
        } else if (layout == TuiLayout::FullRight) {
            lines.push_back(
                std::format("{}{}\033[0m{}{}{}\033[0m", kThemeBorder, border_v,
                            fit_pane_row(render_right(row, frame.panes.right), frame.panes.right),
                            kThemeBorder, border_v));
        } else {
            lines.push_back(
                std::format("{}{}\033[0m{}{}{}\033[0m", kThemeBorder, border_v,
                            fit_pane_row(render_left(row, frame.panes.left), frame.panes.left),
                            kThemeBorder, border_v));
        }
    }

    if (is_ansi) {
        if (layout == TuiLayout::Split) {
            lines.push_back(std::format("{}+{}+{}+\033[0m", kThemeBorder,
                                        make_repeated_string("-", frame.panes.left),
                                        make_repeated_string("-", frame.panes.right)));
        } else {
            lines.push_back(std::format("{}+{}+\033[0m", kThemeBorder,
                                        make_repeated_string("-", terminal_width - 2)));
        }
    } else {
        if (layout == TuiLayout::Split) {
            lines.push_back(std::format("{}╠{}╧{}╣\033[0m", kThemeBorder,
                                        make_repeated_string("═", frame.panes.left),
                                        make_repeated_string("═", frame.panes.right)));
        } else {
            lines.push_back(std::format("{}╠{}╣\033[0m", kThemeBorder,
                                        make_repeated_string("═", terminal_width - 2)));
        }
    }

    append_block_lines(lines, footer_block);
    return lines;
}

}  // namespace simrv::tui
