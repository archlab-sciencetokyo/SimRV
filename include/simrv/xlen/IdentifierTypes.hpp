/**
 * @file IdentifierTypes.hpp
 * @brief Strong XLEN hart and CSR identifier types.
 */
#pragma once

#include <compare>
#include <concepts>

#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::xlen {

/**
 * @struct HartId
 * @brief Strong zero-overhead semantic wrapper representing hardware thread (Hart) identifiers.
 */
struct HartId {
    uint32_t val{0};

    constexpr HartId() noexcept = default;
    constexpr HartId(uint32_t v) noexcept : val(v) {}

    [[nodiscard]] constexpr auto value() const noexcept -> uint32_t { return val; }
    [[nodiscard]] constexpr auto raw() const noexcept -> uint32_t { return val; }
    [[nodiscard]] constexpr explicit operator uint32_t() const noexcept { return val; }

    constexpr auto operator=(uint32_t v) noexcept -> HartId& {
        val = v;
        return *this;
    }

    constexpr auto operator<=>(const HartId&) const noexcept = default;
    constexpr bool operator==(const HartId&) const noexcept = default;
    template <std::integral I>
    constexpr bool operator==(I rhs) const noexcept {
        return val == static_cast<uint32_t>(rhs);
    }
    template <std::integral I>
    constexpr auto operator<=>(I rhs) const noexcept {
        return val <=> static_cast<uint32_t>(rhs);
    }

    template <std::integral I>
    constexpr auto operator+(I offset) const noexcept -> HartId {
        return HartId{val + static_cast<uint32_t>(offset)};
    }
    template <std::integral I>
    friend constexpr auto operator+(I lhs, HartId rhs) noexcept -> HartId {
        return HartId{static_cast<uint32_t>(lhs) + rhs.val};
    }
    template <std::integral I>
    constexpr auto operator-(I offset) const noexcept -> HartId {
        return HartId{val - static_cast<uint32_t>(offset)};
    }
    template <std::integral I>
    friend constexpr auto operator-(I lhs, HartId rhs) noexcept -> HartId {
        return HartId{static_cast<uint32_t>(lhs) - rhs.val};
    }
    template <std::integral I>
    friend constexpr auto operator<<(I lhs, HartId rhs) noexcept -> I {
        return static_cast<I>(lhs << rhs.val);
    }
    template <std::integral I>
    constexpr auto operator+=(I offset) noexcept -> HartId& {
        val += static_cast<uint32_t>(offset);
        return *this;
    }
    template <std::integral I>
    constexpr auto operator-=(I offset) noexcept -> HartId& {
        val -= static_cast<uint32_t>(offset);
        return *this;
    }
};

/**
 * @struct CsrNumber
 * @brief Strong semantic type for CSR (Control and Status Register) index numbers.
 */
struct CsrNumber {
    uint16_t val{0};

    constexpr CsrNumber() noexcept = default;
    constexpr CsrNumber(uint16_t v) noexcept : val(v) {}
    template <typename T>
        requires(std::is_enum_v<T> || (std::is_integral_v<T> && !std::is_same_v<T, uint16_t>))
    constexpr CsrNumber(T v) noexcept : val(static_cast<uint16_t>(v)) {}

    [[nodiscard]] constexpr auto value() const noexcept -> uint16_t { return val; }
    [[nodiscard]] constexpr auto raw() const noexcept -> uint16_t { return val; }
    [[nodiscard]] constexpr explicit operator uint16_t() const noexcept { return val; }

    constexpr auto operator=(uint16_t v) noexcept -> CsrNumber& {
        val = v;
        return *this;
    }

    constexpr auto operator<=>(const CsrNumber&) const noexcept = default;
    constexpr bool operator==(const CsrNumber&) const noexcept = default;
    template <std::integral I>
    constexpr bool operator==(I rhs) const noexcept {
        return val == static_cast<uint16_t>(rhs);
    }
    template <std::integral I>
    constexpr auto operator<=>(I rhs) const noexcept {
        return val <=> static_cast<uint16_t>(rhs);
    }
    template <std::integral I>
    constexpr auto operator&(I rhs) const noexcept -> uint16_t {
        return static_cast<uint16_t>(val & static_cast<uint16_t>(rhs));
    }

    [[nodiscard]] constexpr auto is_read_only() const noexcept -> bool {
        return ((val >> 10U) & 0x3U) == 0x3U;
    }
    [[nodiscard]] constexpr auto privilege_level() const noexcept -> PrivilegeLevel {
        return static_cast<PrivilegeLevel>((val >> 8U) & 0x3U);
    }
};

}  // namespace simrv::xlen
