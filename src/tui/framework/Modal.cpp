#include "simrv/tui/framework/Modal.hpp"

#include <algorithm>

#include "simrv/tui/framework/Components.hpp"
#include "simrv/tui/framework/Text.hpp"

namespace simrv::tui::framework {

auto layout_modal(std::string_view title, std::span<const Row> rows, int terminal_width,
                  int terminal_height, int minimum_width) -> ModalLayout {
    int desired_width = std::max(minimum_width, display_width(title) + 4);
    for (auto const& row : rows)
        desired_width = std::max(desired_width, display_width(row.text) + 2);
    desired_width = std::min(desired_width, std::max(0, terminal_width - 4));
    auto const geometry = overlay_geometry(terminal_width, terminal_height, desired_width,
                                           static_cast<int>(rows.size()), minimum_width);
    ModalLayout result{.geometry = geometry, .rows = {}};
    if (!geometry.renderable) return result;
    result.rows.reserve(rows.size());
    for (auto row : rows) result.rows.push_back(align_row(std::move(row), geometry.width - 2));
    return result;
}

}  // namespace simrv::tui::framework
