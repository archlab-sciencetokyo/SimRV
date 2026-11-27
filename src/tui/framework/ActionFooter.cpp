/**
 * @file ActionFooter.cpp
 * @brief Unified keyboard shortcut action footer bar with built-in hit-testing.
 */
#include "simrv/tui/framework/ActionFooter.hpp"

#include <algorithm>
#include <format>

#include "simrv/tui/framework/Text.hpp"

namespace simrv::tui::framework {

auto ActionFooter::render(int width, Density density, HorizontalAlign align) -> std::string {
    spans_.clear();
    if (actions_.empty() || width <= 0)
        return std::string(static_cast<size_t>(std::max(0, width)), ' ');

    const bool compact = (density == Density::Compact);
    const std::string_view sep = compact ? " " : " · ";
    const int sep_w = static_cast<int>(sep.size());

    struct RenderedAction {
        std::string text;
        int display_w = 0;
    };
    std::vector<RenderedAction> rendered;
    rendered.reserve(actions_.size());
    int total_actions_w = 0;

    for (size_t i = 0; i < actions_.size(); ++i) {
        const auto& act = actions_[i];
        std::string act_text;
        if (act.disabled) {
            act_text = std::format("\033[2m{} {}\033[0m", act.key_hint, act.label);
        } else {
            act_text =
                std::format("\033[1;36m{}\033[0m {}{}\033[0m", act.key_hint, kThemeText, act.label);
        }

        int w = get_display_width(act_text);
        if (i > 0) total_actions_w += sep_w;
        total_actions_w += w;
        rendered.push_back({.text = std::move(act_text), .display_w = w});
    }

    int start_col = 0;
    if (align == HorizontalAlign::Center) {
        start_col = std::max(0, (width - total_actions_w) / 2);
    } else if (align == HorizontalAlign::Right) {
        start_col = std::max(0, width - total_actions_w);
    }

    std::string line;
    if (start_col > 0) {
        line.append(static_cast<size_t>(start_col), ' ');
    }

    int current_col = start_col;
    for (size_t i = 0; i < rendered.size(); ++i) {
        if (i > 0) {
            line += std::format("{}{}\033[0m", kThemeMuted, sep);
            current_col += sep_w;
        }
        spans_.push_back({.action_id = actions_[i].action_id,
                          .index = i,
                          .start_col = current_col,
                          .width = rendered[i].display_w});
        line += rendered[i].text;
        current_col += rendered[i].display_w;
    }

    int remaining = width - current_col;
    if (remaining > 0) {
        line.append(static_cast<size_t>(remaining), ' ');
    }
    return line;
}

auto ActionFooter::hit_test(int col) const noexcept -> std::optional<uint16_t> {
    for (const auto& span : spans_) {
        if (span.contains(col)) {
            return span.action_id;
        }
    }
    return std::nullopt;
}

auto ActionFooter::hit_test_index(int col) const noexcept -> std::optional<size_t> {
    for (const auto& span : spans_) {
        if (span.contains(col)) {
            return span.index;
        }
    }
    return std::nullopt;
}

}  // namespace simrv::tui::framework
