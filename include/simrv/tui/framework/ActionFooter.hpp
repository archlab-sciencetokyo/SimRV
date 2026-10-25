/**
 * @file ActionFooter.hpp
 * @brief Unified keyboard shortcut action footer bar with built-in hit-testing.
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

struct ActionItem {
    std::string key_hint = {};
    std::string label = {};
    uint16_t action_id = 0;
    bool disabled = false;
};

struct ActionSpan {
    uint16_t action_id = 0;
    size_t index = 0;
    int start_col = 0;
    int width = 0;

    [[nodiscard]] constexpr auto contains(int col) const noexcept -> bool {
        return col >= start_col && col < start_col + width;
    }
};

class ActionFooter {
   public:
    ActionFooter() = default;
    explicit ActionFooter(std::vector<ActionItem> actions) : actions_(std::move(actions)) {}

    void set_actions(std::vector<ActionItem> actions) { actions_ = std::move(actions); }
    void add_action(ActionItem action) { actions_.push_back(std::move(action)); }
    void clear() {
        actions_.clear();
        spans_.clear();
    }

    [[nodiscard]] auto size() const noexcept -> size_t { return actions_.size(); }
    [[nodiscard]] auto empty() const noexcept -> bool { return actions_.empty(); }

    [[nodiscard]] auto actions() const noexcept -> const std::vector<ActionItem>& {
        return actions_;
    }

    [[nodiscard]] auto render(int width, Density density = Density::Comfortable,
                              HorizontalAlign align = HorizontalAlign::Center) -> std::string;

    [[nodiscard]] auto spans() const noexcept -> const std::vector<ActionSpan>& { return spans_; }

    [[nodiscard]] auto hit_test(int col) const noexcept -> std::optional<uint16_t>;
    [[nodiscard]] auto hit_test_index(int col) const noexcept -> std::optional<size_t>;

   private:
    std::vector<ActionItem> actions_;
    std::vector<ActionSpan> spans_;
};

}  // namespace simrv::tui::framework
