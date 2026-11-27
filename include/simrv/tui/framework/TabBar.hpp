/**
 * @file TabBar.hpp
 * @brief Structured, responsive tab bar component with accurate hit-testing.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/tui/framework/Theme.hpp"
#include "simrv/tui/framework/Types.hpp"

namespace simrv::tui::framework {

struct TabItem {
    uint8_t id = 0;
    std::string label = {};
    std::string badge = {};
    char shortcut = '\0';
    bool disabled = false;
};

struct TabSpan {
    uint8_t id = 0;
    size_t index = 0;
    int start_col = 0;
    int width = 0;

    [[nodiscard]] constexpr auto contains(int col) const noexcept -> bool {
        return col >= start_col && col < start_col + width;
    }
};

class TabBar {
   public:
    TabBar() = default;
    explicit TabBar(std::vector<TabItem> items) : items_(std::move(items)) {}

    void set_items(std::vector<TabItem> items) { items_ = std::move(items); }
    void add_item(TabItem item) { items_.push_back(std::move(item)); }
    void clear() {
        items_.clear();
        spans_.clear();
    }

    [[nodiscard]] auto size() const noexcept -> size_t { return items_.size(); }
    [[nodiscard]] auto empty() const noexcept -> bool { return items_.empty(); }

    [[nodiscard]] auto active_index() const noexcept -> size_t { return active_index_; }
    void set_active_index(size_t index) noexcept { active_index_ = index; }

    [[nodiscard]] auto items() const noexcept -> const std::vector<TabItem>& { return items_; }
    [[nodiscard]] auto item(size_t index) const -> const TabItem& { return items_.at(index); }

    /// Render tab bar into a string for the given width.
    [[nodiscard]] auto render(int width, Density density = Density::Comfortable,
                              HorizontalAlign align = HorizontalAlign::Center) -> std::string;

    /// Return calculated tab spans from the most recent render.
    [[nodiscard]] auto spans() const noexcept -> const std::vector<TabSpan>& { return spans_; }

    /// Hit-test column against the most recent render geometry.
    [[nodiscard]] auto hit_test(int col) const noexcept -> std::optional<size_t>;

    /// Hit-test by item id.
    [[nodiscard]] auto hit_test_id(int col) const noexcept -> std::optional<uint8_t>;

   private:
    std::vector<TabItem> items_;
    size_t active_index_ = 0;
    std::vector<TabSpan> spans_;
};

}  // namespace simrv::tui::framework
