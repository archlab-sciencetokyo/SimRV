/**
 * @file LayoutPresetsModal.hpp
 * @brief Modal dialog handler for Workbench Column Presets.
 */
#pragma once

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "simrv/tui/TuiPreferences.hpp"
#include "simrv/tui/TuiTypes.hpp"
#include "simrv/tui/framework/Modal.hpp"

namespace simrv::tui {
class TuiModal;
}

namespace simrv::tui::modals {

class LayoutPresetsModal : public framework::IModal {
   public:
    explicit LayoutPresetsModal(TuiModal* host, int& cursor) : host_(host), cursor_(cursor) {}
    LayoutPresetsModal() : host_(nullptr), cursor_(owned_cursor_) {}

    [[nodiscard]] auto title() const -> std::string override { return "Workbench Column Presets"; }
    [[nodiscard]] auto type() const -> ModalType override { return ModalType::LayoutPresets; }

    void render_content(std::vector<std::string>& content_rows, int width, int height) override;
    auto handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction override;
    auto handle_control_click(int content_row, size_t action_index)
        -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction override;

    void scroll_to_top() override { cursor_ = 0; }
    void scroll_to_bottom() override { cursor_ = 3; }
    void handle_wheel(int delta) override { cursor_ = std::clamp(cursor_ + delta, 0, 3); }

   private:
    int owned_cursor_ = 0;
    [[maybe_unused]] TuiModal* host_;
    int& cursor_;
};

}  // namespace simrv::tui::modals
