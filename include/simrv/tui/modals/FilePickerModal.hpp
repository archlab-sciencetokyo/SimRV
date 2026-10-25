/**
 * @file FilePickerModal.hpp
 * @brief Modal dialog handler for file and directory selection.
 */
#pragma once

#include <string>
#include <vector>

#include "simrv/tui/TuiTypes.hpp"
#include "simrv/tui/framework/Modal.hpp"

namespace simrv::tui {
class TuiModal;
}

namespace simrv::tui::modals {

class FilePickerModal : public framework::IModal {
   public:
    explicit FilePickerModal(TuiModal& host) : host_(&host) {}
    FilePickerModal() : host_(nullptr) {}

    [[nodiscard]] auto title() const -> std::string override { return "Select File or Directory"; }
    [[nodiscard]] auto type() const -> ModalType override { return ModalType::LoadFilePicker; }

    void render_content(std::vector<std::string>& content_rows, int width, int height) override;
    auto handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction override;
    auto handle_control_click(int content_row, size_t action_index)
        -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction override;

   private:
    TuiModal* host_;
};

}  // namespace simrv::tui::modals
