/**
 * @file HelpModal.hpp
 * @brief Modal dialog handler for Help keyboard shortcuts dialog.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "simrv/tui/framework/Modal.hpp"

namespace simrv::tui::modals {

class HelpModal : public framework::IModal {
   public:
    explicit HelpModal(int& scroll_offset) : scroll_(scroll_offset) {}
    HelpModal() : owned_scroll_(0), scroll_(owned_scroll_) {}

    [[nodiscard]] auto title() const -> std::string override {
        return " SIMULATOR KEYBOARD SHORTCUTS ";
    }
    [[nodiscard]] auto type() const -> ModalType override { return ModalType::Help; }

    void render_content(std::vector<std::string>& content_rows, int width, int height) override;
    auto handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction override;
    auto handle_control_click(int content_row, size_t action_index)
        -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction override;

    void scroll_to_top() override { scroll_ = 0; }
    void scroll_to_bottom() override { scroll_ = 999999; }
    void handle_wheel(int delta) override { scroll_ = std::max(0, scroll_ + delta * 2); }

    static void render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row_cb, int term_height,
                       int box_w);

   private:
    int owned_scroll_ = 0;
    int& scroll_;
};

}  // namespace simrv::tui::modals
