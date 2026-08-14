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

[[nodiscard]] constexpr auto calculate_pane_widths(int terminal_width, TuiLayout layout,
                                                   int requested_left = -1) -> PaneWidths {
    const int full_width = std::max(0, terminal_width - 2);
    if (layout == TuiLayout::FullLeft) return {.left = full_width, .right = 0};
    if (layout == TuiLayout::FullRight) return {.left = 0, .right = full_width};

    const int split_width = std::max(0, terminal_width - 3);
    if (split_width == 0) return {};

    int desired_left = requested_left;
    if (desired_left <= 0) {
        desired_left =
            terminal_width <= 80
                ? split_width / 2
                : (terminal_width <= 120 ? std::max(40, (terminal_width * 33) / 100)
                                         : std::min(75, std::max(40, (terminal_width * 40) / 100)));
    }

    // Below the normal desktop width, shrink both panes together. At larger sizes retain at least
    // 40 columns for inspection and 20 for the guest terminal.
    const int minimum_left = std::min(40, split_width / 2);
    const int minimum_right = std::min(20, split_width - minimum_left);
    const int maximum_left = std::max(minimum_left, split_width - minimum_right);
    const int left = std::clamp(desired_left, minimum_left, maximum_left);
    return {.left = left, .right = split_width - left};
}

}  // namespace simrv::tui
