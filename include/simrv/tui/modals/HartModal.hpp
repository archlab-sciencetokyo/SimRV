/**
 * @file HartModal.hpp
 * @brief Multi-Hart SMP Visual Hart Switcher Modal.
 */
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace simrv::core {
class Machine;
}

namespace simrv::tui::modals {

class HartModal {
   public:
    static void open(size_t current_hart, const simrv::core::Machine& machine);
    static void move_cursor(int delta, const simrv::core::Machine& machine);
    static auto submit(simrv::core::Machine& machine, size_t& selected_hart_out) -> bool;
    static void render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row,
                       const simrv::core::Machine& machine);

   private:
    static inline int cursor_{0};
};

}  // namespace simrv::tui::modals
