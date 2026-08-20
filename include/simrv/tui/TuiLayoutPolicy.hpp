/**
 * @file TuiLayoutPolicy.hpp
 * @brief Pure pane geometry policy for terminal resize handling.
 */
#pragma once

#include <algorithm>

#include "simrv/tui/TuiTypes.hpp"

namespace simrv::tui {

struct PaneWidths {
    int left = 0;
    int right = 0;
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

[[nodiscard]] constexpr auto calculate_pane_widths(int terminal_width, TuiLayout layout,
                                                   int requested_left = -1) -> PaneWidths {
    const int full_width = std::max(0, terminal_width - 2);
    if (layout == TuiLayout::FullLeft) return {.left = full_width, .right = 0};
    if (layout == TuiLayout::FullRight) return {.left = 0, .right = full_width};

    const int split_width = std::max(0, terminal_width - 3);
    if (split_width == 0) return {};

    int desired_left = requested_left;
    if (desired_left <= 0) {
        if (terminal_width <= 70) {
            desired_left = split_width / 2;
        } else if (terminal_width <= 95) {
            desired_left = 35;
        } else if (terminal_width <= 114) {
            desired_left = std::max(35, (terminal_width * 35) / 100);
        } else {
            desired_left = std::min(70, std::max(58, (terminal_width * 45) / 100));
        }
    }

    // Below the normal desktop width, shrink both panes together. Retain at least 34 columns
    // for single-column inspection (zero clipping) and 20 for the guest terminal.
    const int minimum_left = std::min(34, split_width / 2);
    const int minimum_right = std::min(20, split_width - minimum_left);
    const int maximum_left = std::max(minimum_left, split_width - minimum_right);
    const int left = std::clamp(desired_left, minimum_left, maximum_left);
    return {.left = left, .right = split_width - left};
}

/// Compute the complete frame geometry once so renderers and hit-testing share the same policy.
[[nodiscard]] constexpr auto calculate_frame_geometry(int terminal_width, int terminal_height,
                                                      TuiLayout layout, int requested_left = -1)
    -> FrameGeometry {
    if (terminal_width < kMinimumTerminalWidth || terminal_height < kMinimumTerminalHeight) {
        return {};
    }
    const int content_rows = terminal_height - kFrameChromeRows;
    return {.panes = calculate_pane_widths(terminal_width, layout, requested_left),
            .content_rows = content_rows,
            .frame_rows = content_rows + kFrameChromeRows,
            .renderable = true};
}

/// Center and constrain a modal so both borders remain visible after terminal resizes.
[[nodiscard]] constexpr auto calculate_overlay_geometry(int terminal_width, int terminal_height,
                                                        int maximum_width, int content_rows,
                                                        int minimum_width = 35) -> OverlayGeometry {
    const int width = std::min(maximum_width, terminal_width - 4);
    if (width < minimum_width || terminal_height < 3 || content_rows <= 0) return {};
    const int height = std::min(content_rows + 2, terminal_height);
    return {.width = width,
            .height = height,
            .start_x = std::max(0, (terminal_width - width) / 2),
            .start_y = std::max(0, (terminal_height - height) / 2),
            .visible_content_rows = height - 2,
            .renderable = true};
}

}  // namespace simrv::tui
