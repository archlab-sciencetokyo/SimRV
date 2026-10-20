/**
 * @file MemoryModal.hpp
 * @brief Interactive Full-Screen Hex Dump and Memory Inspector Modal.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "simrv/tui/TuiModal.hpp"
#include "simrv/xlen/AddressTypes.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {
class InspectorPane;
}

namespace simrv::tui::modals {

class MemoryModal {
   public:
    enum class InputMode : uint8_t { Normal, GotoAddress, EditByte };

    static void open(simrv::xlen::Address initial_addr, simrv::core::Machine& machine,
                     InspectorPane* inspector_pane);
    static void move_cursor(int delta_byte, int delta_row, const simrv::core::Machine& machine);
    static void scroll_page(int pages, const simrv::core::Machine& machine);
    static void toggle_addressing_mode();
    static void set_input_mode(InputMode mode);
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
    static inline simrv::xlen::Address base_address_{0x80000000ULL};
    static inline int cursor_byte_{0};  // 0 to 15 within row
    static inline int cursor_row_{0};   // 0 to num_rows-1
    static inline bool physical_mode_{false};
    static inline InputMode mode_{InputMode::Normal};
};

}  // namespace simrv::tui::modals
