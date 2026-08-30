/**
 * @file ScrollView.cpp
 * @brief Implementation of reusable ScrollView rendering and header summaries.
 */
#include "simrv/tui/framework/ScrollView.hpp"

#include <format>

#include "simrv/tui/framework/Text.hpp"

namespace simrv::tui::framework {

auto ScrollView::render_row(int visible_row_idx, int width, RowRenderer renderer) const
    -> std::string {
    if (visible_row_idx < 0 ||
        (bounds_.viewport_height > 0 && visible_row_idx >= bounds_.viewport_height)) {
        return fit_to_width("", width);
    }

    if (indicator_mode_ == ScrollIndicatorMode::EmbeddedRows) {
        if (can_scroll_up() && visible_row_idx == 0) {
            auto const theme = active_theme();
            std::string text = std::format(" {}▲ [{} more lines above - scroll up]\033[0m",
                                           theme.palette.muted, bounds_.remaining_above());
            return fit_to_width(text, width);
        }
        if (can_scroll_down() && visible_row_idx == bounds_.viewport_height - 1 &&
            bounds_.viewport_height > 1) {
            auto const theme = active_theme();
            std::string text = std::format(" {}▼ [{} more lines below - scroll down]\033[0m",
                                           theme.palette.muted, bounds_.remaining_below());
            return fit_to_width(text, width);
        }
    }

    RowIndex const logical_row = visible_row_idx + bounds_.offset_y;
    ColumnIndex const logical_col = bounds_.offset_x;
    if (logical_row < 0 || (bounds_.total_rows > 0 && logical_row >= bounds_.total_rows)) {
        return fit_to_width("", width);
    }

    std::string line = renderer ? renderer(logical_row, logical_col, width) : "";
    if (bounds_.total_cols > width || bounds_.offset_x > 0) {
        return format_horizontal_row(line, width);
    }
    return fit_to_width(line, width);
}

auto ScrollView::format_horizontal_row(std::string_view raw_line, int width) const -> std::string {
    if (width <= 2) {
        return fit_to_width(raw_line, width);
    }
    bool const has_left = can_scroll_left();
    bool const has_right = can_scroll_right();

    if (!has_left && !has_right && bounds_.total_cols <= width) {
        return fit_to_width(raw_line, width);
    }

    auto const theme = active_theme();
    int const content_width = std::max(1, width - 2);
    std::string const cropped = crop_columns(raw_line, bounds_.offset_x, content_width);

    std::string const left_marker =
        has_left ? std::format("{}{}\033[0m", theme.palette.muted, theme.glyphs.arrow_left) : " ";
    std::string const right_marker =
        has_right ? std::format("{}{}\033[0m", theme.palette.muted, theme.glyphs.arrow_right) : " ";

    return fit_to_width(left_marker + cropped + right_marker, width);
}

auto ScrollView::header_summary(std::string_view base_title) const -> std::string {
    if (bounds_.total_rows <= bounds_.viewport_height) {
        return std::string(base_title);
    }
    const int above = bounds_.remaining_above();
    const int below = bounds_.remaining_below();
    if (above > 0 && below > 0) {
        return std::format("{} (▲ {} above · ▼ {} below)", base_title, above, below);
    }
    if (above > 0) {
        return std::format("{} (▲ {} above)", base_title, above);
    }
    if (below > 0) {
        return std::format("{} (▼ {} below)", base_title, below);
    }
    return std::string(base_title);
}

}  // namespace simrv::tui::framework
