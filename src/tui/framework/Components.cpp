#include "simrv/tui/framework/Components.hpp"

#include <algorithm>
#include <format>

#include "simrv/tui/framework/Text.hpp"
#include "simrv/tui/framework/Theme.hpp"

namespace simrv::tui::framework {

auto align_row(Row row, int width) -> Row {
    int const content = display_width(row.text);
    int padding = 0;
    if (row.alignment == HorizontalAlign::Center) padding = std::max(0, (width - content) / 2);
    if (row.alignment == HorizontalAlign::Right) padding = std::max(0, width - content);
    row.text = fit_to_width(std::string(static_cast<std::size_t>(padding), ' ') + row.text, width);
    return row;
}

auto keycap(std::string_view key, Role role) -> std::string {
    auto const theme = active_theme();
    return std::format("\033[1;7m{} {} \033[0m", theme.color(role), key);
}

auto action_row(std::span<const Action> actions, int width) -> RenderedControl {
    auto const theme = active_theme();
    RenderedControl result;
    int cursor = 0;
    for (std::size_t i = 0; i < actions.size(); ++i) {
        if (i > 0) {
            result.text += std::format(" {}·\033[0m ", theme.palette.muted);
            cursor += 3;
        }
        std::string const control = keycap(actions[i].key, actions[i].role) + " " +
                                    std::string(theme.palette.muted) + actions[i].label + "\033[0m";
        int const control_width = display_width(control);
        result.spans.push_back({.start = cursor, .width = control_width, .id = actions[i].id});
        result.text += control;
        cursor += control_width;
    }
    int const left = std::max(0, (width - cursor) / 2);
    result.text =
        fit_to_width(std::string(static_cast<std::size_t>(left), ' ') + result.text, width);
    for (auto& span : result.spans) span.start += left;
    result.width = width;
    return result;
}

auto tab_row(std::span<const Tab> tabs, std::size_t selected, int width) -> RenderedControl {
    auto const theme = active_theme();
    std::vector<Action> controls;
    controls.reserve(tabs.size());
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        controls.push_back({.id = tabs[i].id,
                            .key = std::to_string(i + 1),
                            .label = tabs[i].label,
                            .role = i == selected ? Role::Accent : Role::Muted});
    }
    auto result = action_row(controls, width);
    if (selected < result.spans.size()) {
        // Selection remains measurable through the same span; the keycap fill is the primary cue.
        (void)theme;
    }
    return result;
}

auto section_divider(std::string_view title, int width) -> std::string {
    auto const theme = active_theme();
    std::string const label = " " + std::string(title) + " ";
    int const remaining = std::max(0, width - display_width(label));
    int const left = std::min(4, remaining / 2);
    return fit_to_width(std::string(theme.palette.border) +
                            make_repeated_string(theme.glyphs.horiz, left) + label +
                            make_repeated_string(theme.glyphs.horiz, remaining - left) + "\033[0m",
                        width);
}

auto scroll_indicator(int before, int after, int width) -> std::string {
    auto const theme = active_theme();
    std::string text;
    if (before > 0)
        text +=
            std::format("{}{} {} above\033[0m", theme.palette.muted, theme.glyphs.arrow_up, before);
    if (before > 0 && after > 0) text += std::format(" {}·\033[0m ", theme.palette.muted);
    if (after > 0)
        text += std::format("{}{} {} below\033[0m", theme.palette.muted, theme.glyphs.arrow_down,
                            after);
    return align_row({.text = std::move(text),
                      .alignment = HorizontalAlign::Center,
                      .role = Role::Muted,
                      .kind = RowKind::ScrollIndicator},
                     width)
        .text;
}

}  // namespace simrv::tui::framework
