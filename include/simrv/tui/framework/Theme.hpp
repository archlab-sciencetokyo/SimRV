/**
 * @file Theme.hpp
 * @brief Semantic theme interface for reusable TUI components.
 */
#pragma once

#include <string_view>

#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/framework/Types.hpp"

namespace simrv::tui::framework {

struct Palette {
    std::string_view surface;
    std::string_view border;
    std::string_view text;
    std::string_view muted;
    std::string_view value;
    std::string_view accent;
    std::string_view success;
    std::string_view warning;
    std::string_view danger;
};

struct ThemeContext {
    Palette palette;
    const ThemeGlyphs& glyphs;
    TuiThemeStyle style;

    [[nodiscard]] auto color(Role role) const -> std::string_view;
};

[[nodiscard]] auto active_theme() -> ThemeContext;

}  // namespace simrv::tui::framework
