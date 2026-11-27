#pragma once

#include <concepts>
#include <cstdint>
#include <utility>

// PMP domain types
enum class PmpMode : uint8_t { Off = 0x00, Tor = 0x08, Na4 = 0x10, Napot = 0x18 };
enum class PmpPermission : uint8_t { None = 0, Read = 1, Write = 2, Execute = 4, Lock = 0x80 };
using PmpIndex = uint8_t;

constexpr auto operator|(PmpPermission lhs, PmpPermission rhs) noexcept -> PmpPermission {
    return static_cast<PmpPermission>(std::to_underlying(lhs) | std::to_underlying(rhs));
}
constexpr auto operator&(PmpPermission lhs, PmpPermission rhs) noexcept -> PmpPermission {
    return static_cast<PmpPermission>(std::to_underlying(lhs) & std::to_underlying(rhs));
}
constexpr auto operator~(PmpPermission p) noexcept -> PmpPermission {
    return static_cast<PmpPermission>(~std::to_underlying(p) & 0x87u);
}
constexpr auto operator|=(PmpPermission& lhs, PmpPermission rhs) noexcept -> PmpPermission& {
    lhs = lhs | rhs;
    return lhs;
}

template <std::integral I>
constexpr auto operator&(I lhs, PmpPermission rhs) noexcept -> I {
    return lhs & static_cast<I>(std::to_underlying(rhs));
}
template <std::integral I>
constexpr auto operator&(PmpPermission lhs, I rhs) noexcept -> I {
    return rhs & lhs;
}
template <std::integral I>
constexpr auto operator|(I lhs, PmpPermission rhs) noexcept -> I {
    return lhs | static_cast<I>(std::to_underlying(rhs));
}
template <std::integral I>
constexpr auto operator|(PmpPermission lhs, I rhs) noexcept -> I {
    return rhs | lhs;
}
constexpr auto operator&=(PmpPermission& lhs, PmpPermission rhs) noexcept -> PmpPermission& {
    lhs = lhs & rhs;
    return lhs;
}
