/**
 * @file TuiWidget.hpp
 * @brief Base interface for OOP TUI components.
 */
#pragma once

#include <string>

namespace simrv::tui {

/**
 * @class TuiWidget
 * @brief Abstract base class for all renderable TUI components.
 */
class TuiWidget {
   public:
    virtual ~TuiWidget() = default;

    /**
     * @brief Render a specific row of the widget.
     * @param row_idx The index of the row to render.
     * @param width The maximum width allowed for this widget.
     * @return A string containing the ANSI-formatted row.
     */
    [[nodiscard]] virtual auto render_row(int row_idx, int width) -> std::string = 0;
};

}  // namespace simrv::device
