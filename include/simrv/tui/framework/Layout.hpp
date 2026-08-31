/**
 * @file Layout.hpp
 * @brief Pure responsive geometry policies with no simulator dependencies.
 */
#pragma once

#include <algorithm>
#include <cstdint>

#include "simrv/tui/framework/Types.hpp"

namespace simrv::tui::framework {

enum class Layout : std::uint8_t { Split, FullRight, FullLeft, ThreeColumn, FourColumn };

struct PaneWidths {
    DisplayWidth left = 0;
    DisplayWidth right = 0;
};

struct ColumnWidths {
    DisplayWidth widths[4] = {0, 0, 0, 0};
    uint8_t count = 0;
};

struct FrameGeometry {
    PaneWidths panes{};
    DisplayHeight content_rows = 0;
    DisplayHeight frame_rows = 0;
    bool renderable = false;
};
struct OverlayGeometry {
    DisplayWidth width = 0;
    DisplayHeight height = 0;
    ColIndex start_x = 0;
    RowIndex start_y = 0;
    int visible_content_rows = 0;
    bool renderable = false;

    [[nodiscard]] constexpr auto rect() const noexcept -> ScreenRect {
        return {.origin = {.x = start_x, .y = start_y}, .size = {.width = width, .height = height}};
    }
};

inline constexpr int kBaseColumnUnitWidth = 40;
inline constexpr int kMinimumTerminalWidth = 40;
inline constexpr int kMinimumTerminalHeight = 10;
inline constexpr int kFrameChromeRows = 7;

[[nodiscard]] constexpr auto max_supported_columns(int terminal_width) -> uint8_t {
    if (terminal_width >= 165) return 4;
    if (terminal_width >= 124) return 3;
    if (terminal_width >= 82) return 2;
    return 1;
}

[[nodiscard]] constexpr auto multi_column_widths(int terminal_width, Layout layout,
                                                 int requested_left = -1) -> ColumnWidths {
    int const full_width = std::max(0, terminal_width - 2);
    if (layout == Layout::FullLeft || layout == Layout::FullRight) {
        return {.widths = {full_width, 0, 0, 0}, .count = 1};
    }

    if (layout == Layout::FourColumn && terminal_width >= 165) {
        int const usable = std::max(0, terminal_width - 5);  // 3 inner dividers + 2 borders
        int const c_left = std::clamp((usable * 22) / 100, kBaseColumnUnitWidth, 60);
        int const c3 = usable - (c_left * 3);
        return {.widths = {c_left, c_left, c_left, c3}, .count = 4};
    }

    if ((layout == Layout::ThreeColumn || layout == Layout::FourColumn) && terminal_width >= 124) {
        int const usable = std::max(0, terminal_width - 4);  // 2 inner dividers + 2 borders
        int const c_left = std::clamp((usable * 30) / 100, kBaseColumnUnitWidth, 60);
        int const c2 = usable - (c_left * 2);
        return {.widths = {c_left, c_left, c2, 0}, .count = 3};
    }

    // Default 2-column Split
    int const split_width = std::max(0, terminal_width - 3);
    if (split_width <= 0) return {.widths = {0, 0, 0, 0}, .count = 0};
    if (terminal_width < 75 && layout != Layout::Split) {
        return {.widths = {full_width, 0, 0, 0}, .count = 1};
    }

    int desired_left = requested_left;
    if (desired_left <= 0) {
        if (terminal_width <= 82) {
            desired_left = kBaseColumnUnitWidth;
        } else if (terminal_width <= 110) {
            desired_left = std::clamp((split_width * 42) / 100, kBaseColumnUnitWidth, 58);
        } else {
            desired_left = std::clamp((split_width * 45) / 100, kBaseColumnUnitWidth, 64);
        }
    }
    int const minimum_left = std::min(kBaseColumnUnitWidth, split_width / 2);
    int const minimum_right = std::min(30, split_width - minimum_left);
    int const maximum_left = std::max(minimum_left, split_width - minimum_right);
    int const left = std::clamp(desired_left, minimum_left, maximum_left);
    return {.widths = {left, split_width - left, 0, 0}, .count = 2};
}

[[nodiscard]] constexpr auto pane_widths(int terminal_width, Layout layout, int requested_left = -1)
    -> PaneWidths {
    auto cols = multi_column_widths(terminal_width, layout, requested_left);
    if (cols.count == 1) {
        if (layout == Layout::FullLeft) return {.left = cols.widths[0], .right = 0};
        return {.left = 0, .right = cols.widths[0]};
    }
    return {.left = cols.widths[0], .right = cols.widths[1]};
}

[[nodiscard]] constexpr auto frame_geometry(int terminal_width, int terminal_height, Layout layout,
                                            int requested_left = -1) -> FrameGeometry {
    if (terminal_width < kMinimumTerminalWidth || terminal_height < kMinimumTerminalHeight)
        return {};
    int const rows = terminal_height - kFrameChromeRows;
    return {.panes = pane_widths(terminal_width, layout, requested_left),
            .content_rows = rows,
            .frame_rows = rows + kFrameChromeRows,
            .renderable = true};
}

[[nodiscard]] constexpr auto overlay_geometry(int terminal_width, int terminal_height,
                                              int maximum_width, int content_rows,
                                              int minimum_width = 35) -> OverlayGeometry {
    int const width = std::min(maximum_width, terminal_width - 4);
    if (width < minimum_width || terminal_height < 3 || content_rows <= 0) return {};
    int const height = std::min(content_rows + 2, terminal_height);
    return {.width = width,
            .height = height,
            .start_x = std::max(0, (terminal_width - width) / 2),
            .start_y = std::max(0, (terminal_height - height) / 2),
            .visible_content_rows = height - 2,
            .renderable = true};
}

}  // namespace simrv::tui::framework
