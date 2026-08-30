/**
 * @file TuiFrameRenderer.hpp
 * @brief Pure composition of fully bordered TUI screen rows.
 */
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/tui/framework/Layout.hpp"

namespace simrv::tui {

using framework::FrameGeometry;
using TuiLayout = framework::Layout;
inline constexpr int kFrameRendererMinimumTerminalWidth = framework::kMinimumTerminalWidth;

using PaneRowRenderer = std::function<std::string(int row, int width)>;
using ColumnRowRenderer = std::function<std::string(size_t col_idx, int row, int width)>;

/// Compose the exact header, multi-column panes, dividers, and footer rows for live rendering.
[[nodiscard]] auto compose_multi_frame_lines(const FrameGeometry& frame, int terminal_width,
                                             framework::ColumnWidths col_widths,
                                             std::string_view header_block,
                                             std::string_view footer_block,
                                             const ColumnRowRenderer& render_col)
    -> std::vector<std::string>;

/// Compose the exact header, pane, divider, and footer rows used by the live terminal renderer.
[[nodiscard]] auto compose_frame_lines(const FrameGeometry& frame, int terminal_width,
                                       TuiLayout layout, std::string_view header_block,
                                       std::string_view footer_block,
                                       const PaneRowRenderer& render_left,
                                       const PaneRowRenderer& render_right)
    -> std::vector<std::string>;

}  // namespace simrv::tui
