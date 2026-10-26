/**
 * @file ConfirmModal.hpp
 * @brief Modal dialog handler for platform profile and configuration reload confirmations.
 */
#pragma once

#include <string>
#include <vector>

#include "simrv/tui/TuiTypes.hpp"
#include "simrv/tui/framework/Modal.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {
class TuiModal;
struct SettingsDraft;
}  // namespace simrv::tui

namespace simrv::tui::modals {

class ConfirmModal : public framework::IModal {
   public:
    explicit ConfirmModal(TuiModal* host, simrv::core::Machine* machine, ModalType type,
                          const SettingsDraft* draft, std::string summary)
        : host_(host),
          machine_(machine),
          type_(type),
          draft_(draft),
          summary_(std::move(summary)) {}

    ConfirmModal() : host_(nullptr), machine_(nullptr), type_(ModalType::PlatformChangeConfirm) {}

    [[nodiscard]] auto title() const -> std::string override {
        return " ⚙️  PLATFORM PROFILE CHANGE: RELOAD REQUIRED ";
    }
    [[nodiscard]] auto type() const -> ModalType override { return type_; }

    void render_content(std::vector<std::string>& content_rows, int width, int height) override;
    auto handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction override;
    auto handle_control_click(int content_row, size_t action_index)
        -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction override;

   private:
    TuiModal* host_;
    simrv::core::Machine* machine_;
    ModalType type_;
    const SettingsDraft* draft_ = nullptr;
    std::string summary_;
};

}  // namespace simrv::tui::modals
