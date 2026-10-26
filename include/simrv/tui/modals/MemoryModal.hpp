/**
 * @file MemoryModal.hpp
 * @brief Interactive Full-Screen Hex Dump and Memory Inspector Modal.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "simrv/core/PlatformMap.h"
#include "simrv/tui/TuiTypes.hpp"
#include "simrv/tui/framework/Modal.hpp"
#include "simrv/xlen/AddressTypes.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {
class InspectorPane;
class TuiModal;
}  // namespace simrv::tui

namespace simrv::tui::modals {

class MemoryModal : public framework::IModal {
   public:
    enum class InputMode : uint8_t { Normal, GotoAddress, EditByte };

    explicit MemoryModal(TuiModal* host, simrv::core::Machine* machine, std::string& input)
        : host_(host), machine_(machine), input_(input) {}

    MemoryModal() : host_(nullptr), machine_(nullptr), input_(owned_input_) {}

    [[nodiscard]] auto title() const -> std::string override {
        return "Memory Hex Viewer & Inspection";
    }
    [[nodiscard]] auto type() const -> ModalType override { return ModalType::MemoryDump; }
    [[nodiscard]] auto input() const -> std::string override { return input_; }

    void render_content(std::vector<std::string>& content_rows, int width, int height) override;
    auto handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction override;
    auto handle_control_click(int content_row, size_t action_index)
        -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction override;

    void handle_wheel(int delta) override;
    auto handle_horizontal(int delta) -> bool override {
        if (machine_) move_cursor(delta, 0, *machine_);
        return true;
    }

    static void open(simrv::xlen::Address initial_addr, simrv::core::Machine& machine,
                     InspectorPane* inspector_pane);
    static void move_cursor(int delta_byte, int delta_row, const simrv::core::Machine& machine);
    static void scroll_page(int pages, const simrv::core::Machine& machine);
    static void toggle_addressing_mode();
    static void set_input_mode(InputMode mode);
    static auto handle_cell_click(int row, int rel_x, unsigned int xlen) -> bool;
    [[nodiscard]] static auto input_mode() -> InputMode;
    [[nodiscard]] static auto current_address() -> simrv::xlen::Address;

    static auto submit_input(const std::string& input, simrv::core::Machine& machine,
                             InspectorPane* inspector_pane,
                             const std::function<void(const std::string&)>& set_status_override_cb)
        -> bool;

    static void render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row,
                       const std::string& input, simrv::core::Machine& machine,
                       InspectorPane* inspector_pane, int term_height, int term_width);

   private:
    std::string owned_input_;
    [[maybe_unused]] TuiModal* host_;
    simrv::core::Machine* machine_;
    std::string& input_;

    static inline simrv::xlen::Address base_address_{SIMRV_PLATFORM_DRAM_BASE};
    static inline int cursor_byte_{0};  // 0 to 15 within row
    static inline int cursor_row_{0};   // 0 to num_rows-1
    static inline bool physical_mode_{false};
    static inline InputMode mode_{InputMode::Normal};
};

}  // namespace simrv::tui::modals
