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

enum class ScrollDirection : std::uint8_t { Up, Down, Left, Right };
enum class ScrollGranularity : std::uint8_t { Step, Page, Edge };
enum class ScrollOrigin : std::uint8_t { Top, Bottom };

inline constexpr int kDefaultVerticalStep = 1;
inline constexpr int kDefaultHorizontalStep = 4;
inline constexpr int kDefaultWheelVerticalStep = 2;
inline constexpr int kDefaultConsoleWheelStep = 5;

struct ScrollBounds {
    RowCount total_rows = 0;
    RowCount viewport_height = 0;
    ScrollOffset offset_y = 0;

    ColumnCount total_cols = 0;
    ColumnCount viewport_width = 0;
    ScrollOffset offset_x = 0;

    ScrollOrigin origin = ScrollOrigin::Top;

    [[nodiscard]] constexpr auto max_offset_y() const -> ScrollOffset {
        return std::max(0, total_rows - viewport_height);
    }
    [[nodiscard]] constexpr auto max_offset_x() const -> ScrollOffset {
        return std::max(0, total_cols - viewport_width);
    }
    [[nodiscard]] constexpr auto can_scroll_up() const -> bool {
        return origin == ScrollOrigin::Top ? offset_y > 0 : offset_y < max_offset_y();
    }
    [[nodiscard]] constexpr auto can_scroll_down() const -> bool {
        return origin == ScrollOrigin::Top ? offset_y < max_offset_y() : offset_y > 0;
    }
    [[nodiscard]] constexpr auto can_scroll_left() const -> bool { return offset_x > 0; }
    [[nodiscard]] constexpr auto can_scroll_right() const -> bool {
        return offset_x < max_offset_x();
    }
    [[nodiscard]] constexpr auto remaining_above() const -> RowCount {
        return origin == ScrollOrigin::Top ? std::max(0, offset_y)
                                           : std::max(0, max_offset_y() - offset_y);
    }
    [[nodiscard]] constexpr auto remaining_below() const -> RowCount {
        return origin == ScrollOrigin::Top ? std::max(0, max_offset_y() - offset_y)
                                           : std::max(0, offset_y);
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
    constexpr explicit ScrollView(ScrollIndicatorMode mode, ScrollOrigin origin = ScrollOrigin::Top)
        : indicator_mode_(mode) {
        bounds_.origin = origin;
    }

    void set_indicator_mode(ScrollIndicatorMode mode) { indicator_mode_ = mode; }
    [[nodiscard]] auto indicator_mode() const -> ScrollIndicatorMode { return indicator_mode_; }

    void set_origin(ScrollOrigin origin) {
        bounds_.origin = origin;
        clamp();
    }
    [[nodiscard]] auto origin() const -> ScrollOrigin { return bounds_.origin; }

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

    void scroll(ScrollDirection direction, ScrollGranularity granularity = ScrollGranularity::Step,
                int custom_step = 0) {
        switch (direction) {
            case ScrollDirection::Up: {
                if (granularity == ScrollGranularity::Edge) {
                    scroll_to_top();
                } else if (granularity == ScrollGranularity::Page) {
                    scroll_pages_y(bounds_.origin == ScrollOrigin::Top ? -1 : 1);
                } else {
                    int const step = custom_step > 0 ? custom_step : kDefaultVerticalStep;
                    scroll_y(bounds_.origin == ScrollOrigin::Top ? -step : step);
                }
                break;
            }
            case ScrollDirection::Down: {
                if (granularity == ScrollGranularity::Edge) {
                    scroll_to_bottom();
                } else if (granularity == ScrollGranularity::Page) {
                    scroll_pages_y(bounds_.origin == ScrollOrigin::Top ? 1 : -1);
                } else {
                    int const step = custom_step > 0 ? custom_step : kDefaultVerticalStep;
                    scroll_y(bounds_.origin == ScrollOrigin::Top ? step : -step);
                }
                break;
            }
            case ScrollDirection::Left: {
                if (granularity == ScrollGranularity::Edge) {
                    reset_x();
                } else if (granularity == ScrollGranularity::Page) {
                    scroll_pages_x(-1);
                } else {
                    int const step = custom_step > 0 ? custom_step : kDefaultHorizontalStep;
                    scroll_x(-step);
                }
                break;
            }
            case ScrollDirection::Right: {
                if (granularity == ScrollGranularity::Edge) {
                    scroll_to_right();
                } else if (granularity == ScrollGranularity::Page) {
                    scroll_pages_x(1);
                } else {
                    int const step = custom_step > 0 ? custom_step : kDefaultHorizontalStep;
                    scroll_x(step);
                }
                break;
            }
        }
    }

    void reset_y() { bounds_.offset_y = 0; }

    void reset_x() { bounds_.offset_x = 0; }

    void reset() {
        reset_y();
        reset_x();
    }

    void scroll_to_top() {
        if (bounds_.origin == ScrollOrigin::Top) {
            bounds_.offset_y = 0;
        } else {
            bounds_.offset_y = bounds_.max_offset_y();
        }
    }

    void scroll_to_bottom() {
        if (bounds_.origin == ScrollOrigin::Top) {
            bounds_.offset_y = bounds_.max_offset_y();
        } else {
            bounds_.offset_y = 0;
        }
    }

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

    [[nodiscard]] auto render_top_indicator(int width) const -> std::string;
    [[nodiscard]] auto render_bottom_indicator(int width) const -> std::string;

    [[nodiscard]] auto render_row(int visible_row_idx, int width, RowRenderer renderer) const
        -> std::string;
    [[nodiscard]] auto format_horizontal_row(std::string_view raw_line, int width) const
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
