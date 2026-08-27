/**
 * @file TuiLayoutPolicy.hpp
 * @brief Pure pane geometry policy for terminal resize handling.
 */
#pragma once

#include "simrv/tui/TuiTypes.hpp"
#include "simrv/tui/framework/Layout.hpp"

namespace simrv::tui {

using framework::FrameGeometry;
using framework::OverlayGeometry;
using framework::PaneWidths;
inline constexpr int kMinimumTerminalWidth = framework::kMinimumTerminalWidth;
inline constexpr int kMinimumTerminalHeight = framework::kMinimumTerminalHeight;
inline constexpr int kFrameChromeRows = framework::kFrameChromeRows;

[[nodiscard]] constexpr auto calculate_pane_widths(int terminal_width, TuiLayout layout,
                                                   int requested_left = -1) -> PaneWidths {
    return framework::pane_widths(terminal_width, layout, requested_left);
}

/// Compute the complete frame geometry once so renderers and hit-testing share the same policy.
[[nodiscard]] constexpr auto calculate_frame_geometry(int terminal_width, int terminal_height,
                                                      TuiLayout layout, int requested_left = -1)
    -> FrameGeometry {
    return framework::frame_geometry(terminal_width, terminal_height, layout, requested_left);
}

/// Center and constrain a modal so both borders remain visible after terminal resizes.
[[nodiscard]] constexpr auto calculate_overlay_geometry(int terminal_width, int terminal_height,
                                                        int maximum_width, int content_rows,
                                                        int minimum_width = 35) -> OverlayGeometry {
    return framework::overlay_geometry(terminal_width, terminal_height, maximum_width, content_rows,
                                       minimum_width);
}

}  // namespace simrv::tui
