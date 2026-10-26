/**
 * @file LayoutPresetsModal.cpp
 * @brief Implementation of Workbench Column Presets modal dialog.
 */
#include "simrv/tui/modals/LayoutPresetsModal.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "simrv/tui/TuiPreferences.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"

namespace simrv::tui::modals {

void LayoutPresetsModal::render_content(std::vector<std::string>& content_rows, int /*width*/,
                                        int /*height*/) {
    content_rows.push_back("Select a workbench preset to configure pane columns:");
    content_rows.push_back("");
    for (size_t i = 0; i < kLayoutPresets.size(); ++i) {
        const auto& preset = kLayoutPresets[i];
        bool const is_sel = (cursor_ == static_cast<int>(i));
        std::string line;
        if (is_sel) {
            line = std::format(" \033[1;7m [{}] {:<44} \033[0m", preset.shortcut_label,
                               preset.modal_summary);
        } else {
            line = std::format("  [{}] {:<44}", preset.shortcut_label, preset.modal_summary);
        }
        content_rows.push_back(line);
    }
    content_rows.push_back("");
    content_rows.push_back(
        modals::build_modal_footer({{.key = "[Enter / 1-4]", .action = "Apply Preset"},
                                    {.key = "[Up/Down]", .action = "Navigate"},
                                    {.key = "[Esc / q / F4]", .action = "Cancel"}}));
}

auto LayoutPresetsModal::handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction {
    if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q' ||
        key == simrv::tui::TuiKey::F4) {
        return framework::ModalAction::Close;
    }
    if (byte >= '1' && byte <= '4') {
        cursor_ = byte - '1';
        return framework::ModalAction::Submit;
    }
    if (key == simrv::tui::TuiKey::Tab || byte == 'j' || byte == 'J') {
        handle_wheel(1);
        return framework::ModalAction::Continue;
    }
    if (key == simrv::tui::TuiKey::BackTab || byte == 'k' || byte == 'K') {
        handle_wheel(-1);
        return framework::ModalAction::Continue;
    }
    if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline || byte == ' ') {
        return framework::ModalAction::Submit;
    }
    return framework::ModalAction::Continue;
}

auto LayoutPresetsModal::handle_control_click(int /*content_row*/, size_t action_index)
    -> framework::ModalAction {
    return action_index == 0 ? framework::ModalAction::Submit : framework::ModalAction::Close;
}

auto LayoutPresetsModal::handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction {
    int content_row = local_pt.y - 1;
    if (content_row >= 2 && content_row < 2 + static_cast<int>(kLayoutPresets.size())) {
        cursor_ = content_row - 2;
        return framework::ModalAction::Submit;
    }
    return framework::ModalAction::Continue;
}

}  // namespace simrv::tui::modals
