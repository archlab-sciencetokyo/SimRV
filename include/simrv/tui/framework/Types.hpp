/**
 * @file Types.hpp
 * @brief Domain-independent types shared by the SimRV TUI framework.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace simrv::tui::framework {

using RowIndex = int;
using ColIndex = int;
using DisplayWidth = int;
using DisplayHeight = int;

enum class DensityMode : std::uint8_t { Auto, Compact, Comfortable };
enum class Density : std::uint8_t { Compact, Comfortable };

struct RenderContext {
    DisplayWidth width{0};
    DisplayHeight height{0};
    Density density{Density::Comfortable};
};

inline thread_local RenderContext g_active_render_context{};

[[nodiscard]] constexpr auto resolve_density(DensityMode mode, DisplayWidth width,
                                             DisplayHeight height) noexcept -> Density {
    if (mode == DensityMode::Compact) return Density::Compact;
    if (mode == DensityMode::Comfortable) return Density::Comfortable;
    return width < 60 || height < 24 ? Density::Compact : Density::Comfortable;
}

class ScopedRenderContext {
   public:
    explicit ScopedRenderContext(RenderContext context) noexcept
        : previous_(g_active_render_context) {
        g_active_render_context = context;
    }
    ~ScopedRenderContext() { g_active_render_context = previous_; }
    ScopedRenderContext(const ScopedRenderContext&) = delete;
    auto operator=(const ScopedRenderContext&) -> ScopedRenderContext& = delete;

   private:
    RenderContext previous_;
};

[[nodiscard]] inline auto active_render_context() noexcept -> RenderContext {
    return g_active_render_context;
}

struct ScreenCoord {
    ColIndex x{0};
    RowIndex y{0};

    constexpr auto operator<=>(const ScreenCoord&) const noexcept = default;
};

struct ScreenSize {
    DisplayWidth width{0};
    DisplayHeight height{0};

    constexpr auto operator<=>(const ScreenSize&) const noexcept = default;
};

struct ScreenRect {
    ScreenCoord origin{};
    ScreenSize size{};

    [[nodiscard]] constexpr auto contains(ScreenCoord coord) const noexcept -> bool {
        return coord.x >= origin.x && coord.x < origin.x + size.width && coord.y >= origin.y &&
               coord.y < origin.y + size.height;
    }

    [[nodiscard]] constexpr auto contains(ColIndex x, RowIndex y) const noexcept -> bool {
        return contains(ScreenCoord{.x = x, .y = y});
    }
};

enum class HorizontalAlign : std::uint8_t { Left, Center, Right };

enum class Role : std::uint8_t {
    Surface,
    Border,
    Text,
    Muted,
    Value,
    Accent,
    Success,
    Warning,
    Danger,
};

enum class RowKind : std::uint8_t { Content, Controls, Divider, ScrollIndicator };

struct Row {
    std::string text;
    HorizontalAlign alignment = HorizontalAlign::Left;
    Role role = Role::Text;
    RowKind kind = RowKind::Content;
};

struct ControlSpan {
    ColIndex start = 0;
    DisplayWidth width = 0;
    std::string id;

    [[nodiscard]] constexpr auto contains(ColIndex column) const -> bool {
        return column >= start && column < start + width;
    }
};

struct RenderedControl {
    std::string text;
    DisplayWidth width = 0;
    std::vector<ControlSpan> spans;
};

}  // namespace simrv::tui::framework
