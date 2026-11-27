/**
 * @file StepModal.hpp
 * @brief Modal dialog handler for SetStepSize and SetSpeed dialogs.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "simrv/tui/TuiTypes.hpp"
#include "simrv/tui/framework/Modal.hpp"

namespace simrv::tui::modals {

class StepModal : public framework::IModal {
   public:
    explicit StepModal(std::string& input) : input_(input) {}
    StepModal() : input_(owned_input_) {}

    [[nodiscard]] auto title() const -> std::string override { return "Set Execution Speed"; }
    [[nodiscard]] auto type() const -> ModalType override { return ModalType::SetSpeed; }
    [[nodiscard]] auto input() const -> std::string override { return input_; }

    void render_content(std::vector<std::string>& content_rows, int width, int height) override;
    auto handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction override;
    auto handle_control_click(int content_row, size_t action_index)
        -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction override;

    static void open(ModalType type, std::string& input, uint64_t step_delay_us);
    static auto submit(ModalType type, const std::string& input,
                       std::atomic<uint64_t>& step_delay_us,
                       const std::function<void()>& on_speed_changed_cb,
                       const std::function<void(const std::string&)>& notice_cb = nullptr) -> bool;
    static void render(ModalType type, std::vector<std::string>& content_rows,
                       const std::string& input);

   private:
    std::string owned_input_;
    std::string& input_;
};

}  // namespace simrv::tui::modals
