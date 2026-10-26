/**
 * @file ConfirmModal.cpp
 * @brief Implementation of platform change and configuration confirmation modals.
 */
#include "simrv/tui/modals/ConfirmModal.hpp"

#include <format>
#include <string>
#include <vector>

#include "simrv/core/Machine.hpp"
#include "simrv/core/MachineConfig.hpp"
#include "simrv/tui/TuiModal.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"

namespace simrv::tui::modals {

void ConfirmModal::render_content(std::vector<std::string>& content_rows, int /*width*/,
                                  int /*height*/) {
    if (type_ == ModalType::PlatformChangeConfirm) {
        content_rows.push_back("");
        const char* cur_prof =
            (machine_ && machine_->platform_profile() == simrv::core::PlatformProfile::Pcie)
                ? "PCIe (PCIe 1.2 + ECAM)"
                : "MMIO (VirtIO-MMIO v2)";
        const char* new_prof = (draft_ && draft_->platform_profile == 0) ? "PCIe (PCIe 1.2 + ECAM)"
                                                                         : "MMIO (VirtIO-MMIO v2)";
        content_rows.push_back(std::format("  {}Platform profile changed:\033[0m", kThemeText));
        content_rows.push_back(
            std::format("    {}Current:\033[0m \033[1;36m{}\033[0m", kThemeText, cur_prof));
        content_rows.push_back(
            std::format("    {}Target :\033[0m \033[1;32m{}\033[0m", kThemeText, new_prof));
        content_rows.push_back("");
        content_rows.push_back("  Changing the platform bus topology requires regenerating the");
        content_rows.push_back("  Device Tree (FDT) and restarting CPU execution state.");
        content_rows.push_back("");
        content_rows.push_back(
            build_modal_footer({{.key = "[R / Enter]", .action = "Reload Simulator"},
                                {.key = "[D / Space]", .action = "Discard Change"},
                                {.key = "[Esc / q]", .action = "Cancel"}}));
        return;
    }

    content_rows.push_back("");
    content_rows.push_back(
        std::format("  {}Configuration requires a simulator restart:\033[0m", kThemeText));
    content_rows.push_back(std::format("    {}{}\033[0m", kThemeMuted, summary_));
    content_rows.push_back("");
    content_rows.push_back("  Applying this model resets CPU, cache, and pipeline state.");
    content_rows.push_back("  The simulator must restart with the new configuration.");
    content_rows.push_back("");
    content_rows.push_back(build_modal_footer({{.key = "[R / Enter]", .action = "Reload Simulator"},
                                               {.key = "[D / Space]", .action = "Discard Change"},
                                               {.key = "[Esc / q]", .action = "Cancel"}}));
}

auto ConfirmModal::handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction {
    if (byte == 'r' || byte == 'R' || key == simrv::tui::TuiKey::Enter ||
        key == simrv::tui::TuiKey::Newline) {
        return framework::ModalAction::ReloadRequested;
    }
    if (byte == 'd' || byte == 'D' || byte == ' ') {
        return framework::ModalAction::DiscardRequested;
    }
    if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q') {
        if (type_ == ModalType::PlatformChangeConfirm && host_) {
            host_->open(ModalType::Settings, nullptr, 0);
            return framework::ModalAction::Continue;
        }
        return framework::ModalAction::Close;
    }
    return framework::ModalAction::Continue;
}

auto ConfirmModal::handle_control_click(int /*content_row*/, size_t action_index)
    -> framework::ModalAction {
    if (action_index == 0) return framework::ModalAction::ReloadRequested;
    if (action_index == 1) return framework::ModalAction::DiscardRequested;
    return framework::ModalAction::Close;
}

auto ConfirmModal::handle_click(framework::ScreenCoord /*local_pt*/) -> framework::ModalAction {
    return framework::ModalAction::Continue;
}

}  // namespace simrv::tui::modals
