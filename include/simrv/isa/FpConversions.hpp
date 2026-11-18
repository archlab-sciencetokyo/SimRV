#pragma once

#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <type_traits>

#include "simrv/isa/Fp.hpp"

namespace simrv::isa::fp {

struct Binary16Result {
    uint16_t bits = 0;
    simrv::xlen::CsrValue flags = 0;
};

namespace detail {

template <typename UInt, unsigned FracBits, unsigned ExpBits, int Bias>
[[nodiscard]] constexpr auto to_binary16(UInt raw, RoundingMode rm) noexcept -> Binary16Result {
    constexpr UInt kFracMask = (UInt{1} << FracBits) - 1;
    constexpr UInt kExpMask = (UInt{1} << ExpBits) - 1;
    const bool sign = ((raw >> (FracBits + ExpBits)) & UInt{1}) != 0;
    const auto exp_field = static_cast<unsigned>((raw >> FracBits) & kExpMask);
    const UInt fraction = raw & kFracMask;
    const uint16_t half_sign = sign ? UINT16_C(0x8000) : 0;

    if (exp_field == kExpMask) {
        if (fraction == 0) return {static_cast<uint16_t>(half_sign | UINT16_C(0x7C00)), 0};
        const UInt quiet_bit = UInt{1} << (FracBits - 1);
        const auto flags = (fraction & quiet_bit) == 0
                               ? static_cast<simrv::xlen::CsrValue>(FflagsBit::Nv)
                               : simrv::xlen::CsrValue{0};
        return {static_cast<uint16_t>(half_sign | UINT16_C(0x7E00)), flags};
    }
    if (exp_field == 0 && fraction == 0) return {half_sign, 0};

    const int exponent = (exp_field == 0 ? 1 - Bias : static_cast<int>(exp_field) - Bias);
    const UInt significand = exp_field == 0 ? fraction : (UInt{1} << FracBits) | fraction;
    const auto round_up = [&](UInt quotient, UInt remainder, unsigned shift) constexpr -> bool {
        if (remainder == 0) return false;
        switch (rm) {
            case RoundingMode::Rtz:
                return false;
            case RoundingMode::Rdn:
                return sign;
            case RoundingMode::Rup:
                return !sign;
            case RoundingMode::Rmm:
            case RoundingMode::Rne:
            default:
                if (shift >= sizeof(UInt) * 8) return false;
                {
                    const UInt halfway = UInt{1} << (shift - 1);
                    return remainder > halfway ||
                           (remainder == halfway &&
                            (rm == RoundingMode::Rmm || (quotient & UInt{1}) != 0));
                }
        }
    };

    if (exponent > 15) {
        const bool to_infinity = rm == RoundingMode::Rne || rm == RoundingMode::Rmm ||
                                 (rm == RoundingMode::Rup && !sign) ||
                                 (rm == RoundingMode::Rdn && sign);
        return {
            static_cast<uint16_t>(half_sign | (to_infinity ? UINT16_C(0x7C00) : UINT16_C(0x7BFF))),
            static_cast<simrv::xlen::CsrValue>(FflagsBit::Of) |
                static_cast<simrv::xlen::CsrValue>(FflagsBit::Nx)};
    }

    if (exponent >= -14) {
        constexpr unsigned shift = FracBits - 10;
        UInt quotient = significand >> shift;
        const UInt remainder = significand & ((UInt{1} << shift) - 1);
        const bool inexact = remainder != 0;
        if (round_up(quotient, remainder, shift)) ++quotient;
        int result_exponent = exponent;
        if (quotient == UINT32_C(0x800)) {
            quotient = UINT32_C(0x400);
            ++result_exponent;
        }
        if (result_exponent > 15) {
            const bool to_infinity = rm == RoundingMode::Rne || rm == RoundingMode::Rmm ||
                                     (rm == RoundingMode::Rup && !sign) ||
                                     (rm == RoundingMode::Rdn && sign);
            return {static_cast<uint16_t>(half_sign |
                                          (to_infinity ? UINT16_C(0x7C00) : UINT16_C(0x7BFF))),
                    static_cast<simrv::xlen::CsrValue>(FflagsBit::Of) |
                        static_cast<simrv::xlen::CsrValue>(FflagsBit::Nx)};
        }
        const uint16_t result =
            static_cast<uint16_t>(half_sign | (static_cast<uint16_t>(result_exponent + 15) << 10) |
                                  (static_cast<uint16_t>(quotient) & 0x3FF));
        return {result, inexact ? static_cast<simrv::xlen::CsrValue>(FflagsBit::Nx) : 0};
    }

    const int shift_signed = static_cast<int>(FracBits) - 24 - exponent;
    const unsigned shift = static_cast<unsigned>(shift_signed);
    UInt quotient = 0;
    UInt remainder = significand;
    if (shift < sizeof(UInt) * 8) {
        quotient = significand >> shift;
        remainder = significand & ((UInt{1} << shift) - 1);
    }
    const bool inexact = remainder != 0;
    if (round_up(quotient, remainder, shift)) ++quotient;
    const uint16_t result = static_cast<uint16_t>(half_sign | static_cast<uint16_t>(quotient));
    simrv::xlen::CsrValue flags = 0;
    if (inexact) {
        flags |= static_cast<simrv::xlen::CsrValue>(FflagsBit::Nx);
        if (quotient < UINT16_C(0x400)) flags |= static_cast<simrv::xlen::CsrValue>(FflagsBit::Uf);
    }
    return {result, flags};
}

}  // namespace detail

[[nodiscard]] inline auto to_binary16(float value, RoundingMode rm) noexcept -> Binary16Result {
    return detail::to_binary16<uint32_t, 23, 8, 127>(std::bit_cast<uint32_t>(value), rm);
}

[[nodiscard]] inline auto to_binary16(double value, RoundingMode rm) noexcept -> Binary16Result {
    return detail::to_binary16<uint64_t, 52, 11, 1023>(std::bit_cast<uint64_t>(value), rm);
}

[[nodiscard]] constexpr auto binary16_is_nan(uint16_t bits) noexcept -> bool {
    return (bits & UINT16_C(0x7FFF)) > UINT16_C(0x7C00);
}

[[nodiscard]] constexpr auto binary16_is_snan(uint16_t bits) noexcept -> bool {
    return binary16_is_nan(bits) && ((bits & UINT16_C(0x0200)) == 0);
}

[[nodiscard]] inline auto binary16_to_double(uint16_t bits) noexcept -> double {
    const bool sign = (bits & UINT16_C(0x8000)) != 0;
    const unsigned exp = (bits >> 10U) & 0x1FU;
    const unsigned frac = bits & 0x03FFU;
    if (exp == 0x1FU) {
        if (frac == 0) {
            return std::bit_cast<double>(sign ? UINT64_C(0xFFF0000000000000)
                                              : UINT64_C(0x7FF0000000000000));
        }
        return std::bit_cast<double>(UINT64_C(0x7FF8000000000000));
    }
    if (exp == 0) {
        if (frac == 0) {
            return std::bit_cast<double>(sign ? UINT64_C(0x8000000000000000) : UINT64_C(0));
        }
        const double mag = std::ldexp(static_cast<double>(frac), -24);
        return sign ? -mag : mag;
    }
    const double mag = std::ldexp(static_cast<double>(0x400U | frac), static_cast<int>(exp) - 25);
    return sign ? -mag : mag;
}

[[nodiscard]] inline auto binary16_to_float(uint16_t bits) noexcept -> float {
    const bool sign = (bits & UINT16_C(0x8000)) != 0;
    const unsigned exp = (bits >> 10U) & 0x1FU;
    const unsigned frac = bits & 0x03FFU;
    if (exp == 0x1FU) {
        if (frac == 0) {
            return std::bit_cast<float>(sign ? UINT32_C(0xFF800000) : UINT32_C(0x7F800000));
        }
        return std::bit_cast<float>(UINT32_C(0x7FC00000));
    }
    if (exp == 0) {
        if (frac == 0) {
            return std::bit_cast<float>(sign ? UINT32_C(0x80000000) : UINT32_C(0));
        }
        const float mag = std::ldexp(static_cast<float>(frac), -24);
        return sign ? -mag : mag;
    }
    const float mag = std::ldexp(static_cast<float>(0x400U | frac), static_cast<int>(exp) - 25);
    return sign ? -mag : mag;
}

[[nodiscard]] constexpr auto bf16_is_nan(uint16_t bits) noexcept -> bool {
    return (bits & UINT16_C(0x7FFF)) > UINT16_C(0x7F80);
}

[[nodiscard]] constexpr auto bf16_is_snan(uint16_t bits) noexcept -> bool {
    return bf16_is_nan(bits) && ((bits & UINT16_C(0x0040)) == 0);
}

[[nodiscard]] constexpr auto bf16_to_float(uint16_t bits) noexcept -> float {
    if (bf16_is_nan(bits)) {
        return std::bit_cast<float>(UINT32_C(0x7FC00000));
    }
    return std::bit_cast<float>(static_cast<uint32_t>(bits) << 16U);
}

[[nodiscard]] constexpr auto to_bfloat16(float value, RoundingMode rm) noexcept -> Binary16Result {
    const uint32_t raw = std::bit_cast<uint32_t>(value);
    const uint32_t abs_raw = raw & UINT32_C(0x7FFFFFFF);
    const bool sign = (raw >> 31U) != 0;
    const uint16_t sign_bit = sign ? UINT16_C(0x8000) : 0;

    if (abs_raw > UINT32_C(0x7F800000)) {
        const auto flags = (raw & UINT32_C(0x00400000)) == 0
                               ? static_cast<simrv::xlen::CsrValue>(FflagsBit::Nv)
                               : simrv::xlen::CsrValue{0};
        return {UINT16_C(0x7FC0), flags};
    }
    if (abs_raw == UINT32_C(0x7F800000) || abs_raw == 0) {
        return {static_cast<uint16_t>(raw >> 16U), 0};
    }

    uint16_t abs_q = static_cast<uint16_t>(abs_raw >> 16U);
    const uint32_t rem = abs_raw & UINT32_C(0xFFFF);
    bool round_up = false;
    if (rem != 0) {
        switch (rm) {
            case RoundingMode::Rtz:
                round_up = false;
                break;
            case RoundingMode::Rdn:
                round_up = sign;
                break;
            case RoundingMode::Rup:
                round_up = !sign;
                break;
            case RoundingMode::Rmm:
                round_up = rem >= UINT32_C(0x8000);
                break;
            case RoundingMode::Rne:
            default:
                round_up =
                    (rem > UINT32_C(0x8000)) || (rem == UINT32_C(0x8000) && (abs_q & 1U) != 0U);
                break;
        }
    }
    if (round_up) {
        ++abs_q;
    }
    simrv::xlen::CsrValue flags = 0;
    if (abs_q >= UINT16_C(0x7F80)) {
        const bool to_inf = (rm == RoundingMode::Rne) || (rm == RoundingMode::Rmm) ||
                            (rm == RoundingMode::Rup && !sign) || (rm == RoundingMode::Rdn && sign);
        abs_q = to_inf ? UINT16_C(0x7F80) : UINT16_C(0x7F7F);
        flags = static_cast<simrv::xlen::CsrValue>(FflagsBit::Of) |
                static_cast<simrv::xlen::CsrValue>(FflagsBit::Nx);
        return {static_cast<uint16_t>(sign_bit | abs_q), flags};
    }
    if (rem != 0) {
        flags |= static_cast<simrv::xlen::CsrValue>(FflagsBit::Nx);
        if ((abs_q & UINT16_C(0x7F80)) == 0) {
            flags |= static_cast<simrv::xlen::CsrValue>(FflagsBit::Uf);
        }
    }
    return {static_cast<uint16_t>(sign_bit | abs_q), flags};
}

}  // namespace simrv::isa::fp
