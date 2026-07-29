/**
 * @file SystemConfigModal.hpp
 * @brief System configuration modal dialog for Cycle-Accurate (CA) mode parameters.
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

using simrv::tui::SysConfigDraft;

class SystemConfigModal {
   public:
    static void open(SysConfigDraft& draft, int& cursor, const simrv::core::Machine& machine);
    static void move_cursor(int& cursor, int delta);
    static void adjust_setting(SysConfigDraft& draft, int index, int dir);
    static void toggle_setting(SysConfigDraft& draft, int index);
    static auto submit(const SysConfigDraft& draft, simrv::core::Machine& machine) -> bool;
    static void render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row_cb,
                       const SysConfigDraft& draft, int cursor);
};

}  // namespace simrv::tui::modals
