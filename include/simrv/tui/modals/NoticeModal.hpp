/**
 * @file NoticeModal.hpp
 * @brief Simple announcement, alert, or error notice dialog.
 */
#pragma once

#include <format>
#include <string>
#include <vector>

#include "simrv/tui/TuiTypes.hpp"
#include "simrv/tui/framework/Modal.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {
class TuiModal;
}

namespace simrv::tui::modals {

class NoticeModal : public framework::IModal {
   public:
    explicit NoticeModal(TuiModal* host, simrv::core::Machine* machine, std::string title,
                         std::string message, bool is_error = false)
        : host_(host),
          machine_(machine),
          title_(std::move(title)),
          message_(std::move(message)),
          is_error_(is_error) {}

    NoticeModal() : host_(nullptr), machine_(nullptr), title_("NOTICE"), message_("") {}

    [[nodiscard]] auto title() const -> std::string override {
        return title_.empty() ? (is_error_ ? "ERROR" : "NOTICE") : title_;
        return is_error_ ? std::format(" ❌ {} ", title_.empty() ? "ERROR" : title_)
                         : std::format(" ⚠️  {} ", title_.empty() ? "NOTICE" : title_);
    }
    [[nodiscard]] auto type() const -> ModalType override { return ModalType::Notice; }
    [[nodiscard]] auto is_error() const noexcept -> bool { return is_error_; }

    void render_content(std::vector<std::string>& content_rows, int width, int height) override;
    auto handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction override;
    auto handle_control_click(int content_row, size_t action_index)
        -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction override;

   private:
    TuiModal* host_;
    simrv::core::Machine* machine_;
    std::string title_;
    std::string message_;
    bool is_error_ = false;
};

}  // namespace simrv::tui::modals
