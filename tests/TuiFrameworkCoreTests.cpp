#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/tui/framework/Components.hpp"
#include "simrv/tui/framework/Modal.hpp"
#include "simrv/tui/framework/ScrollView.hpp"
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

    ScrollView sv(ScrollIndicatorMode::EmbeddedRows);
    sv.set_geometry(100, 10, 80, 40);
    expect(sv.can_scroll_down() && !sv.can_scroll_up(),
           "scroll view starts at top with scroll down available");
    expect(sv.can_scroll_right() && !sv.can_scroll_left(),
           "scroll view starts at left with scroll right available");

    sv.scroll_y(15);
    expect(sv.offset_y() == 15 && sv.can_scroll_up(), "vertical scrolling advances offset");
    sv.scroll_x(10);
    expect(sv.offset_x() == 10 && sv.can_scroll_left(), "horizontal scrolling advances offset");

    auto const top_row =
        sv.render_row(0, 40, [](RowIndex r, ColumnIndex, int) { return std::format("row-{}", r); });
    expect(display_width(top_row) == 40 && top_row.find("above") != std::string::npos,
           "top row renders embedded scroll indicator when scrolled down");

    auto const mid_row = sv.render_row(
        1, 40, [](RowIndex r, ColumnIndex, int) { return std::format("content-row-{}", r); });
    expect(display_width(mid_row) == 40 && mid_row.find("-16") != std::string::npos,
           "middle row dispatches logical row offset to renderer with horizontal crop");

    sv.reset_x();
    auto const uncropped_row = sv.render_row(
        1, 40, [](RowIndex r, ColumnIndex, int) { return std::format("content-row-{}", r); });
    expect(display_width(uncropped_row) == 40 &&
               uncropped_row.find("content-row-16") != std::string::npos,
           "middle row dispatches logical row offset without horizontal crop");

    sv.set_indicator_mode(ScrollIndicatorMode::HeaderSummary);
    auto const summary = sv.header_summary("Trace");
    expect(summary.find("above") != std::string::npos && summary.find("below") != std::string::npos,
           "header summary formats scroll indicator with counts");

    ScrollView hsv;
    hsv.set_geometry(10, 5, 100, 20);
    auto const current_thm = active_theme();
    auto const at_start = hsv.format_horizontal_row("012345678901234567890123456789", 20);
    expect(display_width(at_start) == 20 &&
               at_start.find(current_thm.glyphs.arrow_right) != std::string::npos &&
               at_start.find(current_thm.glyphs.arrow_left) == std::string::npos,
           "horizontal row at start shows right indicator and no left indicator");

    hsv.scroll_x(10);
    auto const in_middle = hsv.format_horizontal_row("012345678901234567890123456789", 20);
    expect(display_width(in_middle) == 20 &&
               in_middle.find(current_thm.glyphs.arrow_right) != std::string::npos &&
               in_middle.find(current_thm.glyphs.arrow_left) != std::string::npos,
           "horizontal row in middle shows both left and right indicators");

    simrv::tui::set_theme_style(simrv::tui::TuiThemeStyle::ModernUnicode);
    return failures == 0 ? 0 : 1;
}
