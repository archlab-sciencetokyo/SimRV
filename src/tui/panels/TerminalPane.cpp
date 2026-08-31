/**
 * @file TerminalPane.cpp
 * @brief Implements TerminalPane widget rendering.
 */
#include "simrv/tui/panels/TerminalPane.hpp"

#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui {

auto TerminalPane::render_row(int row_idx, int width) -> std::string {
    if (static_cast<std::size_t>(row_idx) >= lines_.size()) return format_to_width("", width);
    return lines_.at(static_cast<std::size_t>(row_idx));
}

}  // namespace simrv::tui
