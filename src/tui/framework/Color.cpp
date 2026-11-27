/**
 * @file Color.cpp
 * @brief Color conversion, TrueColor detection, and ANSI escape formatting.
 */
#include "simrv/tui/framework/Color.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string_view>

namespace simrv::tui::framework {

namespace {

[[nodiscard]] auto rgb_to_256(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept
    -> std::uint8_t {
    // Check if close to grayscale
    const int diff_rg = std::abs(static_cast<int>(r) - static_cast<int>(g));
    const int diff_gb = std::abs(static_cast<int>(g) - static_cast<int>(b));
    const int diff_rb = std::abs(static_cast<int>(r) - static_cast<int>(b));
    if (diff_rg < 8 && diff_gb < 8 && diff_rb < 8) {
        if (r < 8) return 16;     // pure black
        if (r > 248) return 231;  // pure white
        return static_cast<std::uint8_t>(232 + ((static_cast<int>(r) - 8) * 24 / 240));
    }

    // Map each component to 0..5
    constexpr auto map_cube = [](std::uint8_t val) noexcept -> std::uint8_t {
        if (val < 48) return 0;
        if (val < 115) return 1;
        return static_cast<std::uint8_t>((val - 35) / 40);
    };

    const std::uint8_t r_idx = map_cube(r);
    const std::uint8_t g_idx = map_cube(g);
    const std::uint8_t b_idx = map_cube(b);

    return static_cast<std::uint8_t>(16 + 36 * r_idx + 6 * g_idx + b_idx);
}

}  // namespace

auto terminal_supports_truecolor() noexcept -> bool {
    static const bool supported = []() noexcept -> bool {
        const char* colorterm = std::getenv("COLORTERM");
        if (colorterm != nullptr) {
            std::string_view ct(colorterm);
            if (ct == "truecolor" || ct == "24bit") {
                return true;
            }
        }
        const char* term = std::getenv("TERM");
        if (term != nullptr) {
            std::string_view t(term);
            if (t.find("direct") != std::string_view::npos ||
                t.find("truecolor") != std::string_view::npos) {
                return true;
            }
        }
        return false;
    }();
    return supported;
}

auto Color::to_ansi256_index() const noexcept -> std::uint8_t {
    switch (kind_) {
        case ColorKind::Ansi16:
        case ColorKind::Ansi256:
            return index_;
        case ColorKind::Rgb:
            return rgb_to_256(r_, g_, b_);
        case ColorKind::Default:
        default:
            return 0;
    }
}

auto Color::to_fg_ansi(bool truecolor) const -> std::string {
    switch (kind_) {
        case ColorKind::Default:
            return "\033[39m";
        case ColorKind::Ansi16:
            if (index_ < 8) return std::format("\033[{}m", 30 + index_);
            return std::format("\033[{}m", 90 + (index_ - 8));
        case ColorKind::Ansi256:
            return std::format("\033[38;5;{}m", index_);
        case ColorKind::Rgb:
            if (truecolor) return std::format("\033[38;2;{};{};{}m", r_, g_, b_);
            return std::format("\033[38;5;{}m", to_ansi256_index());
    }
    return "\033[39m";
}

auto Color::to_bg_ansi(bool truecolor) const -> std::string {
    switch (kind_) {
        case ColorKind::Default:
            return "\033[49m";
        case ColorKind::Ansi16:
            if (index_ < 8) return std::format("\033[{}m", 40 + index_);
            return std::format("\033[{}m", 100 + (index_ - 8));
        case ColorKind::Ansi256:
            return std::format("\033[48;5;{}m", index_);
        case ColorKind::Rgb:
            if (truecolor) return std::format("\033[48;2;{};{};{}m", r_, g_, b_);
            return std::format("\033[48;5;{}m", to_ansi256_index());
    }
    return "\033[49m";
}

}  // namespace simrv::tui::framework
