#include "simrv/tui/framework/Theme.hpp"

namespace simrv::tui::framework {

auto ThemeContext::color(Role role) const -> std::string_view {
    switch (role) {
        case Role::Surface:
            return palette.surface;
        case Role::Border:
            return palette.border;
        case Role::Text:
            return palette.text;
        case Role::Muted:
            return palette.muted;
        case Role::Value:
            return palette.value;
        case Role::Accent:
            return palette.accent;
        case Role::Success:
            return palette.success;
        case Role::Warning:
            return palette.warning;
        case Role::Danger:
            return palette.danger;
    }
    return palette.text;
}

auto active_theme() -> ThemeContext {
    auto const style = get_active_theme_style();
    return {.palette = {.surface = kThemeModalBg,
                        .border = kThemeBorder,
                        .text = kThemeText,
                        .muted = kThemeMuted,
                        .value = kThemeVal,
                        .accent = kThemeSky,
                        .success = kThemeMint,
                        .warning = kThemePeach,
                        .danger = kThemeCoral},
            .glyphs = get_theme_glyphs(style),
            .style = style};
}

}  // namespace simrv::tui::framework
