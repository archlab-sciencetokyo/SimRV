/**
 * @file MisaModal.hpp
 * @brief Modal dialog handler for CPU MISA CSR & extensions configuration.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "simrv/tui/TuiModal.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui::modals {

class MisaModal {
   public:
    static void open(MisaDraft& draft, int& cursor, const simrv::core::Machine& machine);
    static void move_cursor(int& cursor, int delta);
    static void toggle_item(MisaDraft& draft, int index);
    static void apply_profile(MisaDraft& draft, int profile_idx);
    static auto submit(const MisaDraft& draft, simrv::core::Machine& machine,
                       const std::function<void(const std::string&)>& set_status_override_cb)
        -> bool;
    static void render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row_cb,
                       const MisaDraft& draft, int cursor, const simrv::core::Machine& machine);
};

}  // namespace simrv::tui::modals
