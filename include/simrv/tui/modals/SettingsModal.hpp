/**
 * @file SettingsModal.hpp
 * @brief Modal dialog handler for simulator settings configuration.
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

class SettingsModal {
   public:
    static void open(SettingsDraft& draft, int& cursor, const simrv::core::Machine& machine);
    static void move_cursor(int& cursor, int delta);
    static void toggle_setting(SettingsDraft& draft, int index, const simrv::core::Machine& machine);
    static auto submit(const SettingsDraft& draft, simrv::core::Machine& machine,
                        const std::function<void(TuiRegPage)>& set_reg_page_cb) -> bool;
    static void render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row_cb,
                       const SettingsDraft& draft, int cursor, const simrv::core::Machine& machine);
};

}  // namespace simrv::tui::modals
