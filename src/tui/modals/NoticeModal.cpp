/**
 * @file NoticeModal.cpp
 * @brief Implementation of notice modal dialog.
 */
#include "simrv/tui/modals/NoticeModal.hpp"

#include <format>
#include <string>
#include <vector>

#include "simrv/core/Machine.hpp"
#include "simrv/tui/TuiModal.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"

namespace simrv::tui::modals {

void NoticeModal::render_content(std::vector<std::string>& content_rows, int /*width*/,
                                 int /*height*/) {
    content_rows.push_back("");
    std::size_t start = 0;
    std::size_t pos = 0;
    while ((pos = message_.find('\n', start)) != std::string::npos) {
        content_rows.push_back(std::format(" {}", message_.substr(start, pos - start)));
        start = pos + 1;
    }
    if (start < message_.size()) {
        content_rows.push_back(std::format(" {}", message_.substr(start)));
    }
    content_rows.push_back("");
    content_rows.push_back(
        build_modal_footer({{.key = "[Enter / Space / Esc]", .action = "Dismiss"}}));
}

auto NoticeModal::handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction {
    if (byte == 'q' || byte == 'Q') {
        if (machine_ && machine_->is_shutdown_) {
            machine_->request_exit();
        }
        return framework::ModalAction::Close;
    }
    if (key == simrv::tui::TuiKey::CtrlR || byte == 0x12) {
        if (machine_) {
            machine_->request_reboot();
        }
        return framework::ModalAction::Close;
    }
    if (byte == 'o' || byte == 'O') {
        if (host_) host_->open(ModalType::LoadBinary, nullptr, 0);
        return framework::ModalAction::Continue;
    }
    if (byte == 27 || key == simrv::tui::TuiKey::Esc || key == simrv::tui::TuiKey::Enter ||
        key == simrv::tui::TuiKey::Newline || byte == ' ') {
        return framework::ModalAction::Close;
    }
    return framework::ModalAction::Continue;
}

auto NoticeModal::handle_control_click(int /*content_row*/, size_t /*action_index*/)
    -> framework::ModalAction {
    return framework::ModalAction::Close;
}

auto NoticeModal::handle_click(framework::ScreenCoord /*local_pt*/) -> framework::ModalAction {
    return framework::ModalAction::Close;
}

}  // namespace simrv::tui::modals
