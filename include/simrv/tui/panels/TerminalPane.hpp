/**
 * @file TerminalPane.hpp
 * @brief OOP Widget for TUI guest terminal pane (Guest console / Sixel display).
 */
#pragma once

#include <string>
#include <vector>

#include "simrv/tui/TuiWidget.hpp"
#include "simrv/tui/framework/ScrollView.hpp"

namespace simrv::tui {

class TerminalPane : public TuiWidget {
   public:
    TerminalPane() = default;
    ~TerminalPane() override = default;

    [[nodiscard]] auto render_row(int row_idx, int width) -> std::string override;

    void set_lines(const std::vector<std::string>& lines) { lines_ = lines; }
    void set_scroll_offset(int offset) {
        scroll_view_.set_geometry(static_cast<int>(lines_.size()), 0);
        scroll_view_.reset_y();
        scroll_view_.scroll_y(offset);
    }
    [[nodiscard]] auto get_scroll_offset() const -> int { return scroll_view_.offset_y(); }

   private:
    std::vector<std::string> lines_;
    framework::ScrollView scroll_view_{framework::ScrollIndicatorMode::None};
};

}  // namespace simrv::tui
