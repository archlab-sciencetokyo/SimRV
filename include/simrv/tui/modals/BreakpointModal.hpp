/**
 * @file BreakpointModal.hpp
 * @brief Modal dialog handler for SetBreakpoint, SetWatchpoint, and ManageBreakpoints dialogs.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "simrv/tui/TuiModal.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {
class InspectorPane;
}

namespace simrv::tui::modals {

class BreakpointModal {
   public:
    static void open(ModalType type, std::string& input, simrv::core::Machine& machine,
                     InspectorPane* left_pane);
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
};

}  // namespace simrv::tui::modals
