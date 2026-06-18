/**
 * @file ConsolePane.hpp
 * @brief OOP Widget for standard log console.
 */
#pragma once

#include <vector>
#include <string>
#include "simrv/tui/TuiWidget.hpp"

namespace simrv::tui {

class ConsolePane : public TuiWidget {
   public:
    ConsolePane() = default;
    ~ConsolePane() override = default;

    [[nodiscard]] auto render_row(int row_idx, int width) -> std::string override;
    
    void set_lines(const std::vector<std::string>& lines) { lines_ = lines; }
    void set_scroll_offset(int offset) { scroll_offset_ = offset; }
    
   private:
    std::vector<std::string> lines_;
    int scroll_offset_ = 0;
};

}  // namespace simrv::tui
