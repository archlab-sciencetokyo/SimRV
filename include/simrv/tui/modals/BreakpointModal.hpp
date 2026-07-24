/**
 * @file BreakpointModal.hpp
 * @brief Modal dialog handler for SetBreakpoint and SetWatchpoint dialogs.
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
class LeftPane;
}

namespace simrv::tui::modals {

class BreakpointModal {
   public:
    static void open(ModalType type, std::string& input, simrv::core::Machine& machine,
                     LeftPane* left_pane);
    static auto submit(ModalType type, const std::string& input, simrv::core::Machine& machine,
                        const std::function<void(const std::string&)>& set_status_override_cb)
        -> bool;
    static void render(ModalType type, std::vector<std::string>& content_rows,
                       const std::string& input);
};

}  // namespace simrv::tui::modals
