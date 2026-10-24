/**
 * @file HelpModal.cpp
 * @brief Implementation of Help shortcuts modal overlay rendering.
 */
#include "simrv/tui/modals/HelpModal.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "simrv/core/BuildInfo.hpp"
#include "simrv/tui/TuiKeybindings.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/framework/Text.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::tui::modals {

namespace {

using Shortcut = std::pair<std::string, std::string>;

struct ShortcutGroup {
    ActionCategory category;
    std::string_view title;
    std::vector<Shortcut> shortcuts;
};

[[nodiscard]] auto help_shortcuts() -> std::vector<ShortcutGroup> {
    std::vector<ShortcutGroup> groups;
    for (const auto& category : Keybindings::categories()) {
        groups.push_back({.category = category.category, .title = category.title, .shortcuts = {}});
    }
    const auto bindings = Keybindings::all();
    for (const auto& binding : bindings) {
        const auto group = std::ranges::find(groups, binding.category, &ShortcutGroup::category);
        if (group != groups.end()) {
            group->shortcuts.emplace_back(binding.key_display, binding.help_label);
        }
    }
    const auto group_for = [&](ActionCategory category) -> ShortcutGroup& {
        return *std::ranges::find(groups, category, &ShortcutGroup::category);
    };
    auto& navigation = group_for(ActionCategory::Navigate).shortcuts;
    navigation.emplace_back("[u/d] / [PgUp/Dn]", "Scroll terminal or logs");
    navigation.emplace_back("[Alt-w/s]", "Scroll inspection pane");
    navigation.emplace_back("[ [ / ] ]", "Resize split panes");
    auto& configuration = group_for(ActionCategory::Configure).shortcuts;
    configuration.emplace_back("[Alt-h]", "Toggle High Contrast");
    configuration.emplace_back("[Alt-t]", "Toggle Sakura Pastel");
    group_for(ActionCategory::Help).shortcuts.emplace_back("[Esc]", "Close active dialog");
    for (auto& group : groups) {
        std::ranges::sort(group.shortcuts, {}, &Shortcut::second);
    }
    return groups;
}

[[nodiscard]] auto format_shortcut(const Shortcut& shortcut, int key_width, int total_width = 0)
    -> std::string {
    const std::string rendered =
        std::format("\033[1m{}{:<{}}\033[0m {}{}\033[0m", kThemeSky, shortcut.first, key_width,
                    kThemeText, shortcut.second);
    return total_width > 0 ? format_to_width(rendered, total_width) : rendered;
}

}  // namespace

void HelpModal::render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row_cb, int term_height,
                       int box_w) {
    (void)content_rows;
    (void)term_height;
    const int inner_width = std::max(1, box_w - 2);
    auto add_wrapped = [&](std::string_view row, int continuation_indent = 0) {
        for (const auto& wrapped : framework::wrap_text(row, inner_width, continuation_indent)) {
            add_row_cb(wrapped);
        }
    };

    add_wrapped(
        std::format(" \033[1mSimRV Version:\033[0m \033[1;36m{}\033[0m  \033[90m(RV{})\033[0m",
                    simrv::buildinfo::kVersion, simrv::xlen::kXLenBits));
    add_wrapped("");

    for (const auto& group : help_shortcuts()) {
        add_wrapped(std::format(" {}{}\033[0m", kThemeMint, group.title));
        for (const auto& shortcut : group.shortcuts) {
            add_wrapped(" " + format_shortcut(shortcut, 24), 2);
        }
        add_wrapped("");
    }
}

}  // namespace simrv::tui::modals
