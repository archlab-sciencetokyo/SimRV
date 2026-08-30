/**
 * @file Layout.hpp
 * @brief Pure responsive geometry policies with no simulator dependencies.
 */
#pragma once

#include <algorithm>
#include <cstdint>

namespace simrv::tui::framework {

enum class Layout : std::uint8_t { Split, FullRight, FullLeft, ThreeColumn, FourColumn };

struct PaneWidths {
    int left = 0;
    int right = 0;
};

struct ColumnWidths {
    int widths[4] = {0, 0, 0, 0};
    uint8_t count = 0;
};

struct FrameGeometry {
    PaneWidths panes{};
    int content_rows = 0;
    int frame_rows = 0;
    bool renderable = false;
};
struct OverlayGeometry {
    int width = 0;
    int height = 0;
    int start_x = 0;
    int start_y = 0;
    int visible_content_rows = 0;
    bool renderable = false;
};

inline constexpr int kMinimumTerminalWidth = 40;
inline constexpr int kMinimumTerminalHeight = 10;
inline constexpr int kFrameChromeRows = 7;

[[nodiscard]] constexpr auto multi_column_widths(int terminal_width, Layout layout,
                                                 int requested_left = -1) -> ColumnWidths {
    int const full_width = std::max(0, terminal_width - 2);
    if (layout == Layout::FullLeft) {
        return {.widths = {full_width, 0, 0, 0}, .count = 1};
    }
    if (layout == Layout::FullRight) {
        return {.widths = {full_width, 0, 0, 0}, .count = 1};
    }
    if (layout == Layout::ThreeColumn && terminal_width >= 110) {
        int const usable = std::max(0, terminal_width - 4);  // 2 inner dividers + borders
        int const c1 = (usable * 30) / 100;
        int const c2 = (usable * 30) / 100;
        int const c3 = usable - c1 - c2;
        return {.widths = {c1, c2, c3, 0}, .count = 3};
    }
    if (layout == Layout::FourColumn && terminal_width >= 135) {
        int const usable = std::max(0, terminal_width - 5);  // 3 inner dividers + borders
        int const c = usable / 4;
        int const c4 = usable - (c * 3);
        return {.widths = {c, c, c, c4}, .count = 4};
    }
    if (layout == Layout::FourColumn && terminal_width >= 110) {
        // Fallback to 3 columns on medium width
        int const usable = std::max(0, terminal_width - 4);
        int const c1 = (usable * 30) / 100;
        int const c2 = (usable * 30) / 100;
        int const c3 = usable - c1 - c2;
        return {.widths = {c1, c2, c3, 0}, .count = 3};
    }

    // Default 2-column Split
    int const split_width = std::max(0, terminal_width - 3);
    if (split_width == 0) return {.widths = {0, 0, 0, 0}, .count = 0};
    int desired_left = requested_left;
    if (desired_left <= 0) {
        if (terminal_width <= 70)
            desired_left = split_width / 2;
        else if (terminal_width <= 95)
            desired_left = 35;
        else if (terminal_width <= 114)
            desired_left = std::max(35, (terminal_width * 35) / 100);
        else
            desired_left = std::min(70, std::max(58, (terminal_width * 45) / 100));
    }
    int const minimum_left = std::min(34, split_width / 2);
    int const minimum_right = std::min(20, split_width - minimum_left);
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
