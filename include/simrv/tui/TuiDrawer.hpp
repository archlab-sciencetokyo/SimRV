/**
 * @file TuiDrawer.hpp
 * @brief Reusable bottom-drawer layout for TUI panels.
 */
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace simrv::tui {

/// A panel drawer owns its row content; the stack handles shared placement and content space.
struct TuiDrawer {
    int height = 0;
    std::function<std::string(int row, int width)> render_row;
};

class TuiDrawerStack {
   public:
    void add(TuiDrawer drawer) {
        if (drawer.height <= 0 || !drawer.render_row) return;
        total_height_ += drawer.height;
        drawers_.push_back(std::move(drawer));
    }

    [[nodiscard]] auto height() const noexcept -> int { return total_height_; }

    [[nodiscard]] auto render_row(int row, int total_rows, int width) const
        -> std::optional<std::string> {
        if (row < total_rows - total_height_ || row >= total_rows) return std::nullopt;
        int first = total_rows - total_height_;
        for (const auto& drawer : drawers_) {
            if (row < first + drawer.height)
                return drawer.render_row(row - first, width);
            first += drawer.height;
        }
        return std::nullopt;
    }

   private:
    std::vector<TuiDrawer> drawers_;
    int total_height_ = 0;
};

}  // namespace simrv::tui
