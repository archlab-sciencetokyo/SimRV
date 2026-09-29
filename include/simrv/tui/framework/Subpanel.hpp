/**
 * @file Subpanel.hpp
 * @brief Reusable geometry and hit-testing primitives for bounded TUI subpanels.
 */
#pragma once

#include <algorithm>

namespace simrv::tui::framework {

/// A terminal-cell rectangle belonging to one independently interactive subpanel.
/// Coordinates are inclusive at the top/left and exclusive at the right/bottom.
struct SubpanelBounds {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] constexpr auto right() const -> int { return x + std::max(0, width); }
    [[nodiscard]] constexpr auto bottom() const -> int { return y + std::max(0, height); }
    [[nodiscard]] constexpr auto contains(int col, int row) const -> bool {
        return col >= x && col < right() && row >= y && row < bottom();
    }
    [[nodiscard]] constexpr auto clamp_x(int col) const -> int {
        return std::clamp(col, x, std::max(x, right() - 1));
    }
    [[nodiscard]] constexpr auto clamp_y(int row) const -> int {
        return std::clamp(row, y, std::max(y, bottom() - 1));
    }
};

}  // namespace simrv::tui::framework
