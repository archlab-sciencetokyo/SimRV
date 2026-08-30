/**
 * @file ScrollView.hpp
 * @brief Reusable 2D viewport scrolling, bounds management, and indicator rendering.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "simrv/tui/framework/Theme.hpp"
#include "simrv/tui/framework/Types.hpp"

namespace simrv::tui::framework {

using ScrollOffset = int;
using RowCount = int;
using RowIndex = int;
using ColumnCount = int;
using ColumnIndex = int;

enum class ScrollIndicatorMode : std::uint8_t {
    None,
    EmbeddedRows,
    HeaderSummary,
};

struct ScrollBounds {
    RowCount total_rows = 0;
    RowCount viewport_height = 0;
    ScrollOffset offset_y = 0;

    ColumnCount total_cols = 0;
    ColumnCount viewport_width = 0;
    ScrollOffset offset_x = 0;

    [[nodiscard]] constexpr auto max_offset_y() const -> ScrollOffset {
        return std::max(0, total_rows - viewport_height);
    }
    [[nodiscard]] constexpr auto max_offset_x() const -> ScrollOffset {
        return std::max(0, total_cols - viewport_width);
    }
    [[nodiscard]] constexpr auto can_scroll_up() const -> bool { return offset_y > 0; }
    [[nodiscard]] constexpr auto can_scroll_down() const -> bool {
        return offset_y < max_offset_y();
    }
    [[nodiscard]] constexpr auto can_scroll_left() const -> bool { return offset_x > 0; }
    [[nodiscard]] constexpr auto can_scroll_right() const -> bool {
        return offset_x < max_offset_x();
    }
    [[nodiscard]] constexpr auto remaining_above() const -> RowCount {
        return std::max(0, offset_y);
    }
    [[nodiscard]] constexpr auto remaining_below() const -> RowCount {
        return std::max(0, total_rows - (offset_y + viewport_height));
    }
    [[nodiscard]] constexpr auto remaining_left() const -> ColumnCount {
        return std::max(0, offset_x);
    }
    [[nodiscard]] constexpr auto remaining_right() const -> ColumnCount {
        return std::max(0, total_cols - (offset_x + viewport_width));
    }
};

class ScrollView {
   public:
    using RowRenderer = std::function<std::string(RowIndex logical_row, ColumnIndex logical_col,
                                                  int visible_width)>;

    constexpr ScrollView() = default;
    constexpr explicit ScrollView(ScrollIndicatorMode mode) : indicator_mode_(mode) {}

    void set_indicator_mode(ScrollIndicatorMode mode) { indicator_mode_ = mode; }
    [[nodiscard]] auto indicator_mode() const -> ScrollIndicatorMode { return indicator_mode_; }

    void set_geometry(RowCount total_rows, RowCount viewport_height, ColumnCount total_cols = 0,
                      ColumnCount viewport_width = 0) {
        bounds_.total_rows = total_rows;
        bounds_.viewport_height = viewport_height;
        bounds_.total_cols = total_cols;
        bounds_.viewport_width = viewport_width;
        clamp();
    }

    void set_total_rows(RowCount total) {
        bounds_.total_rows = total;
        clamp();
    }

    void set_viewport_height(RowCount height) {
        bounds_.viewport_height = height;
        clamp();
    }

    void set_total_cols(ColumnCount total) {
        bounds_.total_cols = total;
        clamp();
    }

    void set_viewport_width(ColumnCount width) {
        bounds_.viewport_width = width;
        clamp();
    }

    void scroll_y(ScrollOffset delta) {
        bounds_.offset_y += delta;
        clamp();
    }

    void scroll_x(ScrollOffset delta) {
        bounds_.offset_x += delta;
        clamp();
    }

    void scroll_pages_y(int pages) {
        int page_step = std::max(1, bounds_.viewport_height - 1);
        scroll_y(pages * page_step);
    }

    void scroll_pages_x(int pages) {
        int page_step = std::max(1, bounds_.viewport_width / 2);
        scroll_x(pages * page_step);
    }

    void reset_y() { bounds_.offset_y = 0; }

    void reset_x() { bounds_.offset_x = 0; }

    void reset() {
        reset_y();
        reset_x();
    }

    void scroll_to_bottom() { bounds_.offset_y = bounds_.max_offset_y(); }

    void scroll_to_right() { bounds_.offset_x = bounds_.max_offset_x(); }

    [[nodiscard]] auto bounds() const -> const ScrollBounds& { return bounds_; }
    [[nodiscard]] auto offset_y() const -> ScrollOffset { return bounds_.offset_y; }
    [[nodiscard]] auto offset_x() const -> ScrollOffset { return bounds_.offset_x; }
    [[nodiscard]] auto total_rows() const -> RowCount { return bounds_.total_rows; }
    [[nodiscard]] auto viewport_height() const -> RowCount { return bounds_.viewport_height; }
    [[nodiscard]] auto total_cols() const -> ColumnCount { return bounds_.total_cols; }
    [[nodiscard]] auto viewport_width() const -> ColumnCount { return bounds_.viewport_width; }

    [[nodiscard]] auto can_scroll_up() const -> bool { return bounds_.can_scroll_up(); }
    [[nodiscard]] auto can_scroll_down() const -> bool { return bounds_.can_scroll_down(); }
    [[nodiscard]] auto can_scroll_left() const -> bool { return bounds_.can_scroll_left(); }
    [[nodiscard]] auto can_scroll_right() const -> bool { return bounds_.can_scroll_right(); }
    [[nodiscard]] auto remaining_above() const -> RowCount { return bounds_.remaining_above(); }
    [[nodiscard]] auto remaining_below() const -> RowCount { return bounds_.remaining_below(); }
    [[nodiscard]] auto remaining_left() const -> ColumnCount { return bounds_.remaining_left(); }
    [[nodiscard]] auto remaining_right() const -> ColumnCount { return bounds_.remaining_right(); }

    [[nodiscard]] auto render_row(int visible_row_idx, int width, RowRenderer renderer) const
        -> std::string;
    [[nodiscard]] auto header_summary(std::string_view base_title) const -> std::string;

   private:
    void clamp() {
        bounds_.offset_y = std::clamp(bounds_.offset_y, 0, bounds_.max_offset_y());
        bounds_.offset_x = std::clamp(bounds_.offset_x, 0, bounds_.max_offset_x());
    }

    ScrollBounds bounds_{};
    ScrollIndicatorMode indicator_mode_ = ScrollIndicatorMode::EmbeddedRows;
};

}  // namespace simrv::tui::framework
