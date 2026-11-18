/**
 * @file FloatTraits.hpp
 * @brief Compile-time IEEE-754 float traits and host rounding-mode policy helpers.
 */
#pragma once

#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <utility>

#include "simrv/isa/FpConversions.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::execute {

struct Binary16 {
    uint16_t bits = 0;
};

struct BFloat16 {
    uint16_t bits = 0;
};

template <typename FloatT>
struct FloatTraits;

template <>
struct FloatTraits<Binary16> {
    using Bits = uint16_t;
    static constexpr Bits kSignMask = UINT16_C(0x8000);
    static constexpr Bits kExpMask = UINT16_C(0x7C00);
    static constexpr Bits kMantMask = UINT16_C(0x03FF);
    static constexpr Bits kQuietBit = UINT16_C(0x0200);
    static constexpr Bits kCanonicalNaN = UINT16_C(0x7E00);
    static constexpr unsigned kWidthBits = 16;
};

template <>
struct FloatTraits<BFloat16> {
    using Bits = uint16_t;
    static constexpr Bits kSignMask = UINT16_C(0x8000);
    static constexpr Bits kExpMask = UINT16_C(0x7F80);
    static constexpr Bits kMantMask = UINT16_C(0x007F);
    static constexpr Bits kQuietBit = UINT16_C(0x0040);
    static constexpr Bits kCanonicalNaN = UINT16_C(0x7FC0);
    static constexpr unsigned kWidthBits = 16;
};

template <>
struct FloatTraits<float> {
    using Bits = uint32_t;
    static constexpr Bits kSignMask = UINT32_C(0x80000000);
    static constexpr Bits kExpMask = UINT32_C(0x7F800000);
    static constexpr Bits kMantMask = UINT32_C(0x007FFFFF);
    static constexpr Bits kQuietBit = UINT32_C(0x00400000);
    static constexpr Bits kCanonicalNaN = UINT32_C(0x7FC00000);
    static constexpr unsigned kWidthBits = 32;
};

template <>
struct FloatTraits<double> {
    using Bits = uint64_t;
    static constexpr Bits kSignMask = UINT64_C(0x8000000000000000);
    static constexpr Bits kExpMask = UINT64_C(0x7FF0000000000000);
    static constexpr Bits kMantMask = UINT64_C(0x000FFFFFFFFFFFFF);
    static constexpr Bits kQuietBit = UINT64_C(0x0008000000000000);
    static constexpr Bits kCanonicalNaN = UINT64_C(0x7FF8000000000000);
    static constexpr unsigned kWidthBits = 64;
};

template <typename FloatT>
[[nodiscard]] constexpr auto canonical_nan() noexcept -> FloatT {
    return std::bit_cast<FloatT>(FloatTraits<FloatT>::kCanonicalNaN);
}

template <typename FloatT>
[[nodiscard]] constexpr auto is_signaling_nan(FloatT value) noexcept -> bool {
    using Traits = FloatTraits<FloatT>;
    const auto bits = std::bit_cast<typename Traits::Bits>(value);
    const bool is_nan =
        ((bits & Traits::kExpMask) == Traits::kExpMask) && ((bits & Traits::kMantMask) != 0);
    return is_nan && ((bits & Traits::kQuietBit) == 0);
}

[[nodiscard]] inline auto frm_to_host_round(simrv::xlen::CsrValue fcsr) noexcept -> int {
    switch (isa::frm_rounding_mode(fcsr)) {
        case isa::RoundingMode::Rtz:
            return FE_TOWARDZERO;
        case isa::RoundingMode::Rdn:
            return FE_DOWNWARD;
        case isa::RoundingMode::Rup:
            return FE_UPWARD;
        default:
            return FE_TONEAREST;
    }
}

template <typename Fn>
inline auto with_host_rounding_mode(int host_round_mode, Fn&& fn) -> decltype(auto) {
    const int saved = std::fegetround();
    if (saved != host_round_mode) {
        std::fesetround(host_round_mode);
    }
    struct Guard {
        int saved_mode;
        int active_mode;
        ~Guard() {
            if (saved_mode != active_mode) {
                std::fesetround(saved_mode);
            }
        }
    } guard{saved, host_round_mode};
    return std::forward<Fn>(fn)();
}

}  // namespace simrv::execute
