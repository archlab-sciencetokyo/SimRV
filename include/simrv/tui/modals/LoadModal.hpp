/**
 * @file LoadModal.hpp
 * @brief Modal dialog handler for LoadBinary and LoadDiskImage dialogs.
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

class LoadModal {
   public:
    static void open(ModalType type, std::string& input, bool& load_appmode,
                     const simrv::core::Machine& machine);
    static auto submit(ModalType type, const std::string& input, bool load_appmode,
                       simrv::core::Machine& machine, std::string& staged_binary_path,
                       bool& staged_mode_change, bool& staged_target_appmode,
                       const std::function<void(const std::string&)>& set_status_override_cb)
        -> bool;
    static void render(ModalType type, std::vector<std::string>& content_rows,
                       const std::string& input, bool load_appmode,
                       const std::string& staged_binary_path);
};

}  // namespace simrv::tui::modals
