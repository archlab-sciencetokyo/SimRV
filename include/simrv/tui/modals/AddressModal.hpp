/**
 * @file AddressModal.hpp
 * @brief Modal dialog handler for InspectAddress dialog.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "simrv/tui/TuiTypes.hpp"
#include "simrv/tui/framework/Modal.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {
class InspectorPane;
}

namespace simrv::tui::modals {

class AddressModal : public framework::IModal {
   public:
    explicit AddressModal(std::string& input) : input_(input) {}
    AddressModal() : input_(owned_input_) {}

    [[nodiscard]] auto title() const -> std::string override { return "Inspect Memory Address"; }
    [[nodiscard]] auto type() const -> ModalType override { return ModalType::InspectAddress; }
    [[nodiscard]] auto input() const -> std::string override { return input_; }

    void render_content(std::vector<std::string>& content_rows, int width, int height) override;
    auto handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction override;
    auto handle_control_click(int content_row, size_t action_index)
        -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction override;

    static void open(std::string& input, InspectorPane* inspector_pane);
    static auto submit(const std::string& input, simrv::core::Machine& machine,
                       InspectorPane* inspector_pane,
                       const std::function<void(const std::string&)>& set_status_override_cb)
        -> bool;
    static void render(std::vector<std::string>& content_rows, const std::string& input);

   private:
    std::string owned_input_;
    std::string& input_;
};

}  // namespace simrv::tui::modals
