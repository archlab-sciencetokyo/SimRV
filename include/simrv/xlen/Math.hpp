/**
 * @file Math.hpp
 * @brief Multiplication helpers and bit-extension algorithms.
 */
#pragma once

#include <concepts>

#include "simrv/xlen/Types.hpp"

template <typename T>
concept IntegralWord = std::integral<T> && (sizeof(T) == sizeof(Word));

namespace simrv::xlen {

// =========================================================================
// Mathematics & Multiplications
// =========================================================================
namespace detail {
constexpr auto unsigned_abs(SignedWord value) -> std::make_unsigned_t<SignedWord> {
    using UnsignedWord = std::make_unsigned_t<SignedWord>;
    const auto uval = static_cast<UnsignedWord>(value);
    return (value < 0) ? (static_cast<UnsignedWord>(~uval) + 1u) : uval;
}

constexpr auto mul_high_unsigned32(std::uint32_t lhs, std::uint32_t rhs) -> std::uint32_t {
    const auto product = static_cast<std::uint64_t>(lhs) * static_cast<std::uint64_t>(rhs);
    return static_cast<std::uint32_t>(product >> 32u);
}

constexpr auto mul_high_unsigned64(std::uint64_t lhs, std::uint64_t rhs) -> std::uint64_t {
#if defined(__SIZEOF_INT128__)
    const auto product = static_cast<unsigned __int128>(lhs) * static_cast<unsigned __int128>(rhs);
    return static_cast<std::uint64_t>(product >> 64u);
#else
    constexpr std::uint64_t mask32 = 0xffffffffULL;
    const std::uint64_t lhs_low = lhs & mask32;
    const std::uint64_t lhs_high = lhs >> 32u;
    const std::uint64_t rhs_low = rhs & mask32;
    const std::uint64_t rhs_high = rhs >> 32u;

    const std::uint64_t p0 = lhs_low * rhs_low;
    const std::uint64_t p1 = lhs_low * rhs_high;
    const std::uint64_t p2 = lhs_high * rhs_low;
    const std::uint64_t p3 = lhs_high * rhs_high;

    const std::uint64_t carry = ((p0 >> 32u) + (p1 & mask32) + (p2 & mask32)) >> 32u;
    return p3 + (p1 >> 32u) + (p2 >> 32u) + carry;
#endif
}

constexpr auto negate_high_low(std::uint64_t high, std::uint64_t low) -> std::uint64_t {
    return ~high + (low == 0 ? 1ULL : 0ULL);
}
}  // namespace detail

[[nodiscard]] inline constexpr auto mul_low(Register lhs, Register rhs) -> Register {
    return lhs * rhs;
}

[[nodiscard]] inline constexpr auto mul_high_signed(Register lhs, Register rhs) -> Register {
    if constexpr (kIsXLen64) {
        const bool lhs_negative = static_cast<SignedWord>(lhs) < 0;
        const bool rhs_negative = static_cast<SignedWord>(rhs) < 0;
        const auto lhs_abs = detail::unsigned_abs(static_cast<SignedWord>(lhs));
        const auto rhs_abs = detail::unsigned_abs(static_cast<SignedWord>(rhs));
        const auto low = lhs_abs * rhs_abs;
        const auto high = detail::mul_high_unsigned64(lhs_abs, rhs_abs);
        const auto result =
            (lhs_negative ^ rhs_negative) ? detail::negate_high_low(high, low) : high;
        return static_cast<Register>(result);
    } else {
        const auto product = static_cast<std::int64_t>(static_cast<SignedWord>(lhs)) *
                             static_cast<std::int64_t>(static_cast<SignedWord>(rhs));
        return static_cast<Register>(static_cast<std::uint64_t>(product) >> 32u);
    }
}

[[nodiscard]] inline constexpr auto mul_high_signed_unsigned(Register lhs, Register rhs)
    -> Register {
    if constexpr (kIsXLen64) {
        const bool lhs_negative = static_cast<SignedWord>(lhs) < 0;
        const auto lhs_abs = detail::unsigned_abs(static_cast<SignedWord>(lhs));
        const auto rhs_value = static_cast<std::uint64_t>(rhs);
        const auto low = lhs_abs * rhs_value;
        const auto high = detail::mul_high_unsigned64(lhs_abs, rhs_value);
        const auto result = lhs_negative ? detail::negate_high_low(high, low) : high;
        return static_cast<Register>(result);
    } else {
        const auto product = static_cast<std::int64_t>(static_cast<SignedWord>(lhs)) *
                             static_cast<std::int64_t>(rhs);
        return static_cast<Register>(static_cast<std::uint64_t>(product) >> 32u);
    }
}

[[nodiscard]] inline constexpr auto mul_high_unsigned(Register lhs, Register rhs) -> Register {
    if constexpr (kIsXLen64) {
        return static_cast<Register>(detail::mul_high_unsigned64(static_cast<std::uint64_t>(lhs),
                                                                 static_cast<std::uint64_t>(rhs)));
    } else {
        return detail::mul_high_unsigned32(static_cast<std::uint32_t>(lhs),
                                           static_cast<std::uint32_t>(rhs));
    }
}

}  // namespace simrv::xlen

// =========================================================================
// Extension Utilities (C++20 Guaranteed Two's Complement Shifts)
// =========================================================================

template <std::integral T>
[[nodiscard]] constexpr auto zero_extend(T value, unsigned bits) -> std::make_unsigned_t<T> {
    using UnsignedT = std::make_unsigned_t<T>;
    if (bits >= (sizeof(T) * 8u)) {
        return static_cast<UnsignedT>(value);
    }
    return static_cast<UnsignedT>(value) & ((static_cast<UnsignedT>(1u) << bits) - 1u);
}

template <std::integral T>
[[nodiscard]] constexpr auto sign_extend(T value, unsigned bits) -> T {
    using SignedT = std::make_signed_t<T>;
    if (bits >= (sizeof(T) * 8u)) {
        return static_cast<T>(value);
    }
    const unsigned shift = (sizeof(T) * 8u) - bits;
    // C++20 guarantees arithmetic right shift for signed integers (Two's complement)
    return static_cast<T>(
        static_cast<SignedT>(static_cast<std::make_unsigned_t<T>>(value) << shift) >> shift);
}