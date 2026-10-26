/**
 * @file BreakpointModal.hpp
 * @brief Modal dialog handler for SetBreakpoint, SetWatchpoint, and ManageBreakpoints dialogs.
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
class TuiModal;
}  // namespace simrv::tui

namespace simrv::tui::modals {

class BreakpointModal : public framework::IModal {
   public:
    explicit BreakpointModal(TuiModal* host, simrv::core::Machine* machine, ModalType type,
                             std::string& input, int& cursor)
        : host_(host), machine_(machine), type_(type), input_(input), cursor_(cursor) {}

    BreakpointModal()
        : host_(nullptr),
          machine_(nullptr),
          type_(ModalType::ManageBreakpoints),
          input_(owned_input_),
          cursor_(owned_cursor_) {}

    [[nodiscard]] auto title() const -> std::string override;
    [[nodiscard]] auto type() const -> ModalType override { return type_; }
    [[nodiscard]] auto input() const -> std::string override { return input_; }

    void render_content(std::vector<std::string>& content_rows, int width, int height) override;
    auto handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction override;
    auto handle_control_click(int content_row, size_t action_index)
        -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction override;

    [[nodiscard]] auto cursor_row() const -> int override;
    void scroll_to_top() override;
    void scroll_to_bottom() override;
    void handle_wheel(int delta) override;

    static void open(ModalType type, std::string& input, simrv::core::Machine& machine,
                     InspectorPane* inspector_pane);
    static void move_cursor(int& cursor, int delta, const simrv::core::Machine& machine);
    static auto remove_at_cursor(
        int& cursor, simrv::core::Machine& machine,
        const std::function<void(const std::string&)>& set_status_override_cb) -> bool;
    static auto submit(ModalType type, const std::string& input, simrv::core::Machine& machine,
                       const std::function<void(const std::string&)>& set_status_override_cb)
        -> bool;
    static void render(ModalType type, std::vector<std::string>& content_rows,
                       const std::string& input, const simrv::core::Machine* machine = nullptr,
                       int bp_cursor = 0);

   private:
    std::string owned_input_;
    int owned_cursor_ = 0;
    [[maybe_unused]] TuiModal* host_;
    [[maybe_unused]] simrv::core::Machine* machine_;
    [[maybe_unused]] ModalType type_;
    std::string& input_;
    int& cursor_;
};

}  // namespace simrv::tui::modals
