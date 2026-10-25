/**
 * @file TabBar.cpp
 * @brief Structured, responsive tab bar component with accurate hit-testing.
 */
#include "simrv/tui/framework/TabBar.hpp"

#include <algorithm>
#include <format>

#include "simrv/tui/framework/Text.hpp"

namespace simrv::tui::framework {

auto TabBar::render(int width, Density density, HorizontalAlign align) -> std::string {
    spans_.clear();
    if (items_.empty() || width <= 0)
        return std::string(static_cast<size_t>(std::max(0, width)), ' ');

    const bool compact = (density == Density::Compact);
    const std::string_view sep = compact ? " " : " · ";
    const int sep_w = static_cast<int>(sep.size());

    struct RenderedItem {
        std::string text;
        int display_w = 0;
    };
    std::vector<RenderedItem> rendered_items;
    rendered_items.reserve(items_.size());
    int total_tabs_w = 0;

    for (size_t i = 0; i < items_.size(); ++i) {
        const auto& item = items_[i];
        const bool is_active = (i == active_index_);
        std::string badge_str;
        if (item.shortcut != '\0') {
            badge_str = std::format("{}", item.shortcut);
        } else {
            badge_str = std::format("{}", i + 1);
        }

        std::string tab_text;
        if (is_active) {
            tab_text = std::format("\033[1;7m{} {} \033[0m \033[1;4m{}{}\033[0m", kThemeSky,
                                   badge_str, kThemeSky, item.label);
        } else if (item.disabled) {
            tab_text = std::format("\033[2m{} {}  {}\033[0m", kThemeMuted, badge_str, item.label);
        } else {
            tab_text = std::format("\033[7m{} {} \033[0m {}{}\033[0m", kThemeMuted, badge_str,
                                   kThemeMuted, item.label);
        }

        if (!item.badge.empty()) {
            tab_text += std::format(" {}{}\033[0m", kThemePeach, item.badge);
        }

        int w = get_display_width(tab_text);
        if (i > 0) total_tabs_w += sep_w;
        total_tabs_w += w;
        rendered_items.push_back({.text = std::move(tab_text), .display_w = w});
    }

    int start_col = 0;
    if (align == HorizontalAlign::Center) {
        start_col = std::max(0, (width - total_tabs_w) / 2);
    } else if (align == HorizontalAlign::Right) {
        start_col = std::max(0, width - total_tabs_w);
    }

    std::string line;
    if (start_col > 0) {
        line.append(static_cast<size_t>(start_col), ' ');
    }

    int current_col = start_col;
    for (size_t i = 0; i < rendered_items.size(); ++i) {
        if (i > 0) {
            line += std::format("{}{}\033[0m", kThemeMuted, sep);
            current_col += sep_w;
        }
        spans_.push_back({.id = items_[i].id,
                          .index = i,
                          .start_col = current_col,
                          .width = rendered_items[i].display_w});
        line += rendered_items[i].text;
        current_col += rendered_items[i].display_w;
    }

    int remaining = width - current_col;
    if (remaining > 0) {
        line.append(static_cast<size_t>(remaining), ' ');
    }
    return line;
}

auto TabBar::hit_test(int col) const noexcept -> std::optional<size_t> {
    for (const auto& span : spans_) {
        if (span.contains(col)) {
            return span.index;
        }
    }
    return std::nullopt;
}

auto TabBar::hit_test_id(int col) const noexcept -> std::optional<uint8_t> {
    for (const auto& span : spans_) {
        if (span.contains(col)) {
            return span.id;
        }
    }
    return std::nullopt;
}

}  // namespace simrv::tui::framework
