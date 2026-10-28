#pragma once

#include <bit>
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
        return {static_cast<uint16_t>(half_sign | (to_infinity ? UINT16_C(0x7C00)
                                                               : UINT16_C(0x7BFF))),
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
            return {static_cast<uint16_t>(half_sign | (to_infinity ? UINT16_C(0x7C00)
                                                                   : UINT16_C(0x7BFF))),
                    static_cast<simrv::xlen::CsrValue>(FflagsBit::Of) |
                        static_cast<simrv::xlen::CsrValue>(FflagsBit::Nx)};
        }
        const uint16_t result = static_cast<uint16_t>(half_sign |
                                                     (static_cast<uint16_t>(result_exponent + 15)
                                                      << 10) |
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

}  // namespace simrv::isa::fp
