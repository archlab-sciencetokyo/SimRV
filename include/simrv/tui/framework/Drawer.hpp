/**
 * @file Drawer.hpp
 * @brief Pure reusable bottom-drawer stack primitives for bounded TUI panels.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace simrv::tui::framework {

template <typename IdType = uint8_t>
struct DrawerSpan {
    IdType id{};
    std::string_view name{};
    int start_row = 0;  // 0-indexed panel row
    int height = 0;

    [[nodiscard]] constexpr auto end_row() const noexcept -> int { return start_row + height; }
    [[nodiscard]] constexpr auto contains(int row_idx) const noexcept -> bool {
        return row_idx >= start_row && row_idx < end_row();
    }
    [[nodiscard]] constexpr auto is_header_row(int row_idx) const noexcept -> bool {
        return height > 0 && row_idx == start_row;
    }
    [[nodiscard]] constexpr auto start_terminal_row(int panel_top_row = 4) const noexcept -> int {
        return panel_top_row + start_row;
    }
    [[nodiscard]] constexpr auto end_terminal_row(int panel_top_row = 4) const noexcept -> int {
        return panel_top_row + end_row() - 1;
    }
};

template <typename IdType = uint8_t>
struct Drawer {
    IdType id{};
    std::string_view name{};
    int height = 0;
    std::function<std::string(int row, int width)> render_row;
};

template <typename IdType = uint8_t>
class DrawerStack {
   public:
    using SpanType = DrawerSpan<IdType>;
    using DrawerType = Drawer<IdType>;

    void add(DrawerType drawer) {
        if (drawer.height <= 0) return;
        total_height_ += drawer.height;
        drawers_.push_back(std::move(drawer));
    }

    [[nodiscard]] auto height() const noexcept -> int { return total_height_; }
    [[nodiscard]] auto empty() const noexcept -> bool { return drawers_.empty(); }
    [[nodiscard]] auto size() const noexcept -> size_t { return drawers_.size(); }

    [[nodiscard]] auto top_drawer_row(int total_rows) const noexcept -> int {
        return std::max(0, total_rows - total_height_);
    }

    [[nodiscard]] auto spans(int total_rows) const -> std::vector<SpanType> {
        std::vector<SpanType> result;
        result.reserve(drawers_.size());
        int first = total_rows - total_height_;
        for (const auto& drawer : drawers_) {
            result.push_back({.id = drawer.id,
                              .name = drawer.name,
                              .start_row = first,
                              .height = drawer.height});
            first += drawer.height;
        }
        return result;
    }

    [[nodiscard]] auto find_span(IdType id, int total_rows) const -> std::optional<SpanType> {
        int first = total_rows - total_height_;
        for (const auto& drawer : drawers_) {
            if (drawer.id == id) {
                return SpanType{.id = drawer.id,
                                .name = drawer.name,
                                .start_row = first,
                                .height = drawer.height};
            }
            first += drawer.height;
        }
        return std::nullopt;
    }

    [[nodiscard]] auto drawer_at_row(int row, int total_rows) const -> std::optional<SpanType> {
        if (row < total_rows - total_height_ || row >= total_rows) return std::nullopt;
        int first = total_rows - total_height_;
        for (const auto& drawer : drawers_) {
            if (row < first + drawer.height) {
                return SpanType{.id = drawer.id,
                                .name = drawer.name,
                                .start_row = first,
                                .height = drawer.height};
            }
            first += drawer.height;
        }
        return std::nullopt;
    }

    [[nodiscard]] auto render_row(int row, int total_rows, int width) const
        -> std::optional<std::string> {
        if (row < total_rows - total_height_ || row >= total_rows) return std::nullopt;
        int first = total_rows - total_height_;
        for (const auto& drawer : drawers_) {
            if (row < first + drawer.height) {
                if (!drawer.render_row) return std::string{};
                return drawer.render_row(row - first, width);
            }
            first += drawer.height;
        }
        return std::nullopt;
    }

   private:
    std::vector<DrawerType> drawers_;
    int total_height_ = 0;
};

}  // namespace simrv::tui::framework
