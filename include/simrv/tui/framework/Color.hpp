/**
 * @file Color.hpp
 * @brief Typesafe terminal color representation (Ansi16, Ansi256, TrueColor RGB).
 */
#pragma once

#include <cstdint>
#include <string>

namespace simrv::tui::framework {

enum class ColorKind : std::uint8_t { Default, Ansi16, Ansi256, Rgb };

class Color {
   public:
    constexpr Color() noexcept : kind_(ColorKind::Default) {}

    static constexpr auto default_color() noexcept -> Color { return Color{}; }

    static constexpr auto ansi16(std::uint8_t index) noexcept -> Color {
        Color c;
        c.kind_ = ColorKind::Ansi16;
        c.index_ = index;
        return c;
    }

    static constexpr auto ansi256(std::uint8_t index) noexcept -> Color {
        Color c;
        c.kind_ = ColorKind::Ansi256;
        c.index_ = index;
        return c;
    }

    static constexpr auto rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept -> Color {
        Color c;
        c.kind_ = ColorKind::Rgb;
        c.r_ = r;
        c.g_ = g;
        c.b_ = b;
        return c;
    }

    static constexpr auto from_hex(std::uint32_t hex) noexcept -> Color {
        return rgb(static_cast<std::uint8_t>((hex >> 16U) & 0xFFU),
                   static_cast<std::uint8_t>((hex >> 8U) & 0xFFU),
                   static_cast<std::uint8_t>(hex & 0xFFU));
    }

    [[nodiscard]] constexpr auto kind() const noexcept -> ColorKind { return kind_; }
    [[nodiscard]] constexpr auto r() const noexcept -> std::uint8_t { return r_; }
    [[nodiscard]] constexpr auto g() const noexcept -> std::uint8_t { return g_; }
    [[nodiscard]] constexpr auto b() const noexcept -> std::uint8_t { return b_; }
    [[nodiscard]] constexpr auto index() const noexcept -> std::uint8_t { return index_; }
    [[nodiscard]] constexpr auto is_default() const noexcept -> bool {
        return kind_ == ColorKind::Default;
    }

    constexpr auto operator==(const Color& other) const noexcept -> bool = default;

    [[nodiscard]] auto to_fg_ansi(bool truecolor = true) const -> std::string;
    [[nodiscard]] auto to_bg_ansi(bool truecolor = true) const -> std::string;
    [[nodiscard]] auto to_ansi256_index() const noexcept -> std::uint8_t;

   private:
    ColorKind kind_{ColorKind::Default};
    std::uint8_t r_{0};
    std::uint8_t g_{0};
    std::uint8_t b_{0};
    std::uint8_t index_{0};
};

[[nodiscard]] auto terminal_supports_truecolor() noexcept -> bool;

}  // namespace simrv::tui::framework
