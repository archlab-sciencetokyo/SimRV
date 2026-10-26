/**
 * @file MissionObjectiveModal.hpp
 * @brief Modal dialog handler for student guidance and mission objectives.
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
class MissionProgress;
class TuiModal;
}  // namespace simrv::tui

namespace simrv::tui::modals {

class MissionObjectiveModal : public framework::IModal {
   public:
    explicit MissionObjectiveModal(TuiModal* host, simrv::core::Machine* machine,
                                   const MissionProgress* mission, uint8_t& hint_level)
        : host_(host), machine_(machine), mission_(mission), hint_level_(hint_level) {}

    MissionObjectiveModal()
        : host_(nullptr), machine_(nullptr), mission_(nullptr), hint_level_(owned_hint_level_) {}

    [[nodiscard]] auto title() const -> std::string override {
        return "Lesson Objective & Guidance";
    }
    [[nodiscard]] auto type() const -> ModalType override { return ModalType::MissionObjective; }

    void render_content(std::vector<std::string>& content_rows, int width, int height) override;
    auto handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction override;
    auto handle_control_click(int content_row, size_t action_index)
        -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction override;

   private:
    uint8_t owned_hint_level_ = 0;
    [[maybe_unused]] TuiModal* host_;
    [[maybe_unused]] simrv::core::Machine* machine_;
    const MissionProgress* mission_;
    uint8_t& hint_level_;
};

}  // namespace simrv::tui::modals
