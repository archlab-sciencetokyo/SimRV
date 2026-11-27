/**
 * @file Components.hpp
 * @brief Semantic, measurable building blocks for SimRV TUI views.
 */
#pragma once

#include <span>
#include <string>
#include <string_view>

#include "simrv/tui/framework/ScreenBuffer.hpp"
#include "simrv/tui/framework/Types.hpp"

namespace simrv::tui::framework {

struct Action {
    std::string id;
    std::string key;
    std::string label;
    Role role = Role::Accent;
};

struct Tab {
    std::string id;
    std::string label;
};

[[nodiscard]] auto align_row(Row row, int width) -> Row;
[[nodiscard]] auto keycap(std::string_view key, Role role = Role::Accent) -> std::string;
[[nodiscard]] auto action_row(std::span<const Action> actions, int width) -> RenderedControl;
[[nodiscard]] auto tab_row(std::span<const Tab> tabs, std::size_t selected, int width)
    -> RenderedControl;
[[nodiscard]] auto section_divider(std::string_view title, int width) -> std::string;
[[nodiscard]] auto scroll_indicator(int before, int after, int width) -> std::string;

// Modern Canvas-based rendering primitives
void draw_box(Canvas& canvas, ScreenRect rect, BoxBorderKind border_kind = BoxBorderKind::Single,
              Style style = {});
void draw_table_header(Canvas& canvas, int y, std::span<const std::string_view> headers,
                       std::span<const int> widths, Style style = {});
void draw_tab_bar(Canvas& canvas, int y, std::span<const Tab> tabs, std::size_t selected,
                  Style active_style = {}, Style inactive_style = {});

}  // namespace simrv::tui::framework
