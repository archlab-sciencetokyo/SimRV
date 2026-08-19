/**
 * @file HelpModal.cpp
 * @brief Implementation of Help shortcuts modal overlay rendering.
 */
#include "simrv/tui/modals/HelpModal.hpp"

#include <format>
#include <string>
#include <utility>
#include <vector>

#include "simrv/core/BuildInfo.hpp"
#include "simrv/tui/TuiKeybindings.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::tui::modals {

namespace {

using Shortcut = std::pair<std::string, std::string>;

[[nodiscard]] auto help_shortcuts() -> std::vector<Shortcut> {
    std::vector<Shortcut> shortcuts;
    const auto bindings = Keybindings::all();
    shortcuts.reserve(bindings.size() + 6);
    for (const auto& binding : bindings) {
        shortcuts.emplace_back(binding.key_display, binding.help_label);
    }
    shortcuts.emplace_back("[u/d] / [PgUp/Dn]", "Scroll terminal or logs");
    shortcuts.emplace_back("[Alt-w/s]", "Scroll inspection pane");
    shortcuts.emplace_back("[ [ / ] ]", "Resize split panes");
    shortcuts.emplace_back("[Alt-h]", "Toggle High Contrast");
    shortcuts.emplace_back("[Alt-t]", "Toggle Sakura Pastel");
    shortcuts.emplace_back("[Esc]", "Close active dialog");
    return shortcuts;
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
    add_row_cb(
        std::format(" \033[1mSimRV Version:\033[0m \033[1;36m{}\033[0m  \033[90m(RV{})\033[0m",
                    simrv::buildinfo::kVersion, simrv::xlen::kXLenBits));
    add_row_cb("");

    const std::vector<Shortcut> shortcuts = help_shortcuts();
    if (term_height < 32 && box_w >= 70) {
        constexpr int kKeyWidth = 18;
        constexpr int kColumnSeparatorWidth = 3;
        const int inner_width = box_w - 2;
        const int left_width = (inner_width - kColumnSeparatorWidth) / 2;
        const int right_width = inner_width - kColumnSeparatorWidth - left_width;
        const std::size_t half = (shortcuts.size() + 1) / 2;
        for (std::size_t i = 0; i < half; ++i) {
            std::string row = format_shortcut(shortcuts[i], kKeyWidth, left_width);
            if (i + half < shortcuts.size()) {
                row += " │ " + format_shortcut(shortcuts[i + half], kKeyWidth, right_width);
            } else {
                row +=
                    std::string(static_cast<std::size_t>(kColumnSeparatorWidth + right_width), ' ');
            }
            add_row_cb(row);
        }
        return;
    }

    for (const auto& shortcut : shortcuts) {
        add_row_cb(" " + format_shortcut(shortcut, 24));
    }
}

}  // namespace simrv::tui::modals
