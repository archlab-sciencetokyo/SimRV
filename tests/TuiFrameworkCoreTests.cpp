#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/tui/framework/Components.hpp"
#include "simrv/tui/framework/Modal.hpp"
#include "simrv/tui/framework/Text.hpp"
#include "simrv/tui/framework/Theme.hpp"

namespace {
int failures = 0;

void expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
}  // namespace

int main() {
    using namespace simrv::tui::framework;

    expect(display_width("A·界") == 4, "UTF-8 terminal-cell widths are measured");
    expect(display_width("\033[31mred\033[0m") == 3, "ANSI controls have zero width");
    expect(display_width(fit_to_width("abc", 8)) == 8, "fitting preserves requested width");
    expect(display_width(crop_columns("0123456789", 3, 4)) == 4,
           "horizontal cropping preserves viewport width");
    expect(crop_columns("0123456789", 3, 4).find("3456") != std::string::npos,
           "horizontal cropping selects requested columns");
    auto const wrapped = wrap_text("\033[31m界界界\033[0m", 4, 1);
    expect(wrapped.size() == 2 && display_width(wrapped[0]) == 4 && display_width(wrapped[1]) == 4,
           "wrapping preserves ANSI and complete wide UTF-8 glyphs");

    std::vector<Action> actions = {{.id = "apply", .key = "Enter", .label = "Apply"},
                                   {.id = "cancel", .key = "Esc", .label = "Cancel"}};
    auto const controls = action_row(actions, 60);
    expect(display_width(controls.text) == 60, "control row fills its requested width");
    expect(controls.spans.size() == 2 && controls.spans[0].contains(controls.spans[0].start),
           "rendered controls expose reusable hit spans");
    expect(controls.spans[0].start > 0, "control group is centered");

    auto const theme = active_theme();
    expect(!theme.color(Role::Accent).empty() && theme.glyphs.horiz != nullptr &&
               !std::string_view(theme.glyphs.horiz).empty(),
           "active theme exposes semantic colors and glyphs");
    expect(display_width(scroll_indicator(2, 3, 40)) == 40,
           "scroll indicators preserve their requested width");

    std::vector<Row> modal_rows = {
        {.text = "short"},
        {.text = "controls", .alignment = HorizontalAlign::Center, .kind = RowKind::Controls}};
    auto const modal = layout_modal("Title", modal_rows, 100, 24);
    expect(modal.geometry.renderable && modal.geometry.width == 35,
           "modal layout shrinks to its minimum useful content width");
    expect(display_width(modal.rows[1].text) == modal.geometry.width - 2 &&
               modal.rows[1].text.starts_with(' '),
           "modal control rows center against resolved dynamic width");

    simrv::tui::set_theme_style(simrv::tui::TuiThemeStyle::ClassicAnsi);
    auto const classic = active_theme();
    expect(std::string_view(classic.glyphs.horiz) == "-" &&
               display_width(section_divider("Section", 24)) == 24,
           "Classic ANSI supplies complete glyph and component fallbacks");

    return failures == 0 ? 0 : 1;
}
