/**
 * @file Widget.hpp
 * @brief Declarative Widget base class for composable TUI views.
 */
#pragma once

#include "simrv/tui/framework/ScreenBuffer.hpp"
#include "simrv/tui/framework/Types.hpp"

namespace simrv::tui::framework {

class Widget {
   public:
    virtual ~Widget() = default;

    virtual auto measure(ScreenSize available) -> ScreenSize { return available; }
    virtual void layout(ScreenRect bounds) { bounds_ = bounds; }
    virtual void render(Canvas& canvas) = 0;

    [[nodiscard]] auto bounds() const noexcept -> ScreenRect { return bounds_; }

   protected:
    ScreenRect bounds_{};
};

}  // namespace simrv::tui::framework
