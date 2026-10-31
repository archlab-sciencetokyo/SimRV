/**
 * @file TerminalPane.hpp
 * @brief OOP Widget for TUI guest terminal pane (Guest console / graphical display).
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
    void set_geometry(int total_lines, int viewport_height) {
        scroll_view_.set_geometry(total_lines, viewport_height);
    }
    void set_scroll_offset(int offset) {
        scroll_view_.reset_y();
        scroll_view_.scroll_y(offset);
    }
    [[nodiscard]] auto get_scroll_offset() const -> int { return scroll_view_.offset_y(); }
    [[nodiscard]] auto scroll_view() const -> const framework::ScrollView& { return scroll_view_; }
    [[nodiscard]] auto scroll_view() -> framework::ScrollView& { return scroll_view_; }

   private:
    std::vector<std::string> lines_;
    framework::ScrollView scroll_view_{framework::ScrollIndicatorMode::None,
                                       framework::ScrollOrigin::Bottom};
};

}  // namespace simrv::tui
