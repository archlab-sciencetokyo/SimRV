/**
 * @file Modal.hpp
 * @brief Content-driven modal measurement and aligned row layout.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/tui/TuiKeybindings.hpp"
#include "simrv/tui/TuiTypes.hpp"
#include "simrv/tui/framework/Layout.hpp"
#include "simrv/tui/framework/Types.hpp"

namespace simrv::tui::framework {

enum class ModalAction : uint8_t {
    Ignored = 0,
    Continue = 1,
    Handled = Continue,
    Close = 2,
    Closed = Close,
    Submit = 3,
    ReloadRequested = 4,
    DiscardRequested = 5
};

class IModal {
   public:
    virtual ~IModal() = default;

    [[nodiscard]] virtual auto title() const -> std::string = 0;
    [[nodiscard]] virtual auto type() const -> ModalType { return ModalType::None; }
    [[nodiscard]] virtual auto preferred_size(int term_width, int term_height) const -> ScreenSize {
        return {.width = std::min(78, std::max(40, term_width - 4)),
                .height = std::min(24, std::max(10, term_height - 4))};
    }

    virtual void render_content(std::vector<std::string>& content_rows, int width, int height) = 0;
    virtual auto handle_key(uint8_t byte, TuiKey key) -> ModalAction = 0;
    virtual auto handle_click(ScreenCoord local_pt) -> ModalAction {
        (void)local_pt;
        return ModalAction::Continue;
    }
    virtual auto handle_click(ScreenCoord local_pt, ScreenSize box_size) -> ModalAction {
        (void)box_size;
        return handle_click(local_pt);
    }
    virtual auto handle_control_click(int content_row, size_t action_index) -> ModalAction {
        (void)content_row;
        (void)action_index;
        return ModalAction::Ignored;
    }
    virtual auto is_custom_control_at(int rel_x, int rel_y) const -> bool {
        (void)rel_x;
        (void)rel_y;
        return false;
    }

    virtual void scroll_to_top() {}
    virtual void scroll_to_bottom() {}
    virtual void handle_wheel(int delta) { (void)delta; }
    virtual auto handle_horizontal(int delta) -> bool {
        (void)delta;
        return false;
    }
    [[nodiscard]] virtual auto cursor_row() const -> int { return 0; }
    [[nodiscard]] virtual auto input() const -> std::string { return ""; }
};

class ModalHost {
   public:
    void open(std::unique_ptr<IModal> modal) noexcept { active_modal_ = std::move(modal); }
    void close() noexcept { active_modal_.reset(); }
    [[nodiscard]] auto is_active() const noexcept -> bool { return active_modal_ != nullptr; }
    [[nodiscard]] auto modal() const noexcept -> const IModal* { return active_modal_.get(); }
    [[nodiscard]] auto modal() noexcept -> IModal* { return active_modal_.get(); }

    auto handle_key(uint8_t byte, TuiKey key) -> ModalAction {
        if (!active_modal_) return ModalAction::Continue;
        auto action = active_modal_->handle_key(byte, key);
        if (action == ModalAction::Close) close();
        return action;
    }

    auto handle_click(ScreenCoord pt) -> ModalAction {
        if (!active_modal_) return ModalAction::Continue;
        auto action = active_modal_->handle_click(pt);
        if (action == ModalAction::Close) close();
        return action;
    }

   private:
    std::unique_ptr<IModal> active_modal_{};
};

struct ModalLayout {
    OverlayGeometry geometry;
    std::vector<Row> rows;
};

[[nodiscard]] auto layout_modal(std::string_view title, std::span<const Row> rows,
                                int terminal_width, int terminal_height, int minimum_width = 35)
    -> ModalLayout;

}  // namespace simrv::tui::framework
