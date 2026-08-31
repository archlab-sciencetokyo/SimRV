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
    static void open(SettingsDraft& draft, const simrv::core::Machine& machine);
    static void move_cursor(SettingsDraft& draft, int delta);
    static void move_cursor(int& cursor, int delta);
    static void cycle_tab(SettingsDraft& draft, int delta = 1);
    static void set_tab(SettingsDraft& draft, uint8_t tab);
    static void adjust_setting(SettingsDraft& draft, int dir,
                               const simrv::core::Machine* machine = nullptr);
    static void adjust_setting(SettingsDraft& draft, int index, int dir,
                               const simrv::core::Machine* machine = nullptr);
    static void toggle_setting(SettingsDraft& draft, const simrv::core::Machine* machine = nullptr);
    static void toggle_setting(SettingsDraft& draft, int index,
                               const simrv::core::Machine* machine = nullptr);
    static void push_digit(SettingsDraft& draft, std::string& input, char c);
    static void pop_digit(SettingsDraft& draft, std::string& input);
    static void apply_misa_profile(SettingsDraft& draft, int profile_idx);
    static auto submit(const SettingsDraft& draft, simrv::core::Machine& machine,
                       const std::function<void(TuiRegPage)>& set_reg_page_cb) -> bool;
    static void render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row_cb,
                       const SettingsDraft& draft, const simrv::core::Machine& machine);
};

}  // namespace simrv::tui::modals
