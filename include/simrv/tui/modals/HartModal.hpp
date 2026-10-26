/**
 * @file HartModal.hpp
 * @brief Multi-Hart SMP Visual Hart Switcher Modal.
 */
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "simrv/tui/TuiTypes.hpp"
#include "simrv/tui/framework/Modal.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui::modals {

class HartModal : public framework::IModal {
   public:
    explicit HartModal(simrv::core::Machine& machine) : machine_(&machine) {}
    HartModal() : machine_(nullptr) {}

    [[nodiscard]] auto title() const -> std::string override {
        return "Switch Active Hart Telemetry";
    }
    [[nodiscard]] auto type() const -> ModalType override { return ModalType::SwitchHart; }

    void render_content(std::vector<std::string>& content_rows, int width, int height) override;
    auto handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction override;
    auto handle_control_click(int content_row, size_t action_index)
        -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction override;

    void scroll_to_top() override;
    void scroll_to_bottom() override;
    void handle_wheel(int delta) override;

    static void open(size_t current_hart, const simrv::core::Machine& machine);
    static void move_cursor(int delta, const simrv::core::Machine& machine);
    static auto submit(simrv::core::Machine& machine, size_t& selected_hart_out) -> bool;
    static void render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row,
                       const simrv::core::Machine& machine);

   private:
    simrv::core::Machine* machine_;
    static inline int cursor_{0};
};

}  // namespace simrv::tui::modals
