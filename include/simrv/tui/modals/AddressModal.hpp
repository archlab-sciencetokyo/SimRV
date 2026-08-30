/**
 * @file AddressModal.hpp
 * @brief Modal dialog handler for InspectAddress dialog.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace simrv::core {
class Machine;
}

namespace simrv::tui {
class InspectorPane;
}

namespace simrv::tui::modals {

class AddressModal {
   public:
    static void open(std::string& input, InspectorPane* left_pane);
    static auto submit(const std::string& input, simrv::core::Machine& machine, InspectorPane* left_pane,
                       const std::function<void(const std::string&)>& set_status_override_cb)
        -> bool;
    static void render(std::vector<std::string>& content_rows, const std::string& input);
};

}  // namespace simrv::tui::modals
