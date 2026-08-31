/**
 * @file Modal.hpp
 * @brief Content-driven modal measurement and aligned row layout.
 */
#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "simrv/tui/framework/Layout.hpp"
#include "simrv/tui/framework/Types.hpp"

namespace simrv::tui::framework {

struct ModalLayout {
    OverlayGeometry geometry;
    std::vector<Row> rows;
};

[[nodiscard]] auto layout_modal(std::string_view title, std::span<const Row> rows,
                                int terminal_width, int terminal_height, int minimum_width = 35)
    -> ModalLayout;

}  // namespace simrv::tui::framework
