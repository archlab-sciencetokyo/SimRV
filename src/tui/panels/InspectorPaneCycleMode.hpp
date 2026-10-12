/** @file InspectorPaneCycleMode.hpp */
#pragma once

#include <algorithm>
#include <optional>
#include <string>

#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui::detail {

inline auto inactive_cycle_mode_row(int logical_row, int width) -> std::optional<std::string> {
    if (logical_row == 4) {
        const std::string text = "\033[1;31m[!] CYCLE-ACCURATE MODE INACTIVE\033[0m";
        const int spaces = std::max(1, (width - get_display_width(text)) / 2);
        return format_to_width(std::string(static_cast<std::size_t>(spaces), ' ') + text, width);
    }
    if (logical_row == 6) {
        const std::string text = width < 45 ? "Enable Cycle-Accurate mode [,]"
                                            : "Enable cycle mode [,] or --mode cycle-accurate";
        const int spaces = std::max(1, (width - get_display_width(text)) / 2);
        return format_to_width(
            std::string(static_cast<std::size_t>(spaces), ' ') + kThemeMuted + text + "\033[0m",
            width);
    }
    return std::nullopt;
}

}  // namespace simrv::tui::detail
