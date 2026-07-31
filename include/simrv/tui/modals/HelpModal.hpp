/**
 * @file HelpModal.hpp
 * @brief Modal dialog handler for Help keyboard shortcuts dialog.
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace simrv::tui::modals {

class HelpModal {
   public:
    static void render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row_cb, int term_height,
                       int box_w);
};

}  // namespace simrv::tui::modals
