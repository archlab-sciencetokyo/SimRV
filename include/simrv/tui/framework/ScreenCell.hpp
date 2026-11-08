/**
 * @file ScreenCell.hpp
 * @brief Representation of a single 2D terminal character cell and styling.
 */
#pragma once

#include <cstdint>

#include "simrv/tui/framework/Color.hpp"

namespace simrv::tui::framework {

struct TextAttributes {
    std::uint8_t bold : 1 {0};
    std::uint8_t dim : 1 {0};
    std::uint8_t italic : 1 {0};
    std::uint8_t underline : 1 {0};
    std::uint8_t reverse : 1 {0};

    constexpr auto operator==(const TextAttributes& other) const noexcept -> bool = default;
    [[nodiscard]] constexpr auto is_none() const noexcept -> bool {
        return !bold && !dim && !italic && !underline && !reverse;
    }
};

struct Style {
    Color fg{};
    Color bg{};
    TextAttributes attr{};

    constexpr auto operator==(const Style& other) const noexcept -> bool = default;

    [[nodiscard]] constexpr auto with_fg(Color color) const noexcept -> Style {
        Style s = *this;
        s.fg = color;
        return s;
    }

    [[nodiscard]] constexpr auto with_bg(Color color) const noexcept -> Style {
        Style s = *this;
        s.bg = color;
        return s;
    }

    [[nodiscard]] constexpr auto with_attr(TextAttributes attributes) const noexcept -> Style {
        Style s = *this;
        s.attr = attributes;
        return s;
    }

    [[nodiscard]] constexpr auto with_bold(bool b = true) const noexcept -> Style {
        Style s = *this;
        s.attr.bold = b ? 1 : 0;
        return s;
    }

    [[nodiscard]] constexpr auto with_dim(bool d = true) const noexcept -> Style {
        Style s = *this;
        s.attr.dim = d ? 1 : 0;
        return s;
    }

    [[nodiscard]] constexpr auto with_reverse(bool r = true) const noexcept -> Style {
        Style s = *this;
        s.attr.reverse = r ? 1 : 0;
        return s;
    }
};

enum class CellKind : std::uint8_t {
    Standard,
    WideLead,
    WideTail,
    GraphicPassthrough,
};

struct ScreenCell {
    char32_t codepoint{U' '};
    Style style{};
    CellKind kind{CellKind::Standard};

    constexpr auto operator==(const ScreenCell& other) const noexcept -> bool = default;
};

}  // namespace simrv::tui::framework
