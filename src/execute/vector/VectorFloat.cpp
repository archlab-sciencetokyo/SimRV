#include <cfenv>
#include <cmath>
#include <limits>

#include "VectorHelpers.hpp"
#include "simrv/execute/ExecuteUnit.hpp"

namespace simrv::execute {

namespace {

auto read_vector_scalar_fp(core::CPU& cpu, RegId rs1) -> FloatingRegister {
    FloatingRegister bits = cpu.state().regs.read_fp(rs1);
    if (!isa::misa_has_extension(cpu.state().misa, isa::IsaExtension::D)) {
        bits |= simrv::xlen::kF32BoxerBits;
    }
    return bits;
}

template <typename T>
auto canonicalize_nan(T value) -> T {
    if (!std::isnan(value)) return value;
    if constexpr (std::is_same_v<T, float>) {
        return std::bit_cast<float>(simrv::xlen::kF32Qnan);
    } else {
        return std::bit_cast<double>(simrv::xlen::kF64Qnan);
    }
}

auto host_exceptions_to_fflags(int exceptions) -> CSRValue {
    CSRValue flags = 0;
    if ((exceptions & FE_INEXACT) != 0) flags |= enum_mask(isa::FflagsBit::Nx);
    if ((exceptions & FE_UNDERFLOW) != 0) flags |= enum_mask(isa::FflagsBit::Uf);
    if ((exceptions & FE_OVERFLOW) != 0) flags |= enum_mask(isa::FflagsBit::Of);
    if ((exceptions & FE_DIVBYZERO) != 0) flags |= enum_mask(isa::FflagsBit::Dz);
    if ((exceptions & FE_INVALID) != 0) flags |= enum_mask(isa::FflagsBit::Nv);
    return flags;
}

auto frm_to_host_round(CSRValue fcsr) -> int {
    switch ((fcsr >> 5U) & 0x7U) {
        case enum_mask(isa::RoundingMode::Rtz):
            return FE_TOWARDZERO;
        case enum_mask(isa::RoundingMode::Rdn):
            return FE_DOWNWARD;
        case enum_mask(isa::RoundingMode::Rup):
            return FE_UPWARD;
        default:
            // FE_TONEAREST implements RNE. RMM still requires a software
            // tie-breaking path and is recorded in the compliance document.
            return FE_TONEAREST;
    }
}

auto round_rmm_f32(double exact) -> float {
    // A binary64 intermediate has enough precision to identify binary32 halfway cases
    // for the ordinary binary32 arithmetic operations below.
    float rounded = static_cast<float>(exact);
    if (!std::isfinite(exact) || !std::isfinite(rounded) || exact == 0.0) return rounded;
    if (static_cast<double>(rounded) == exact) return rounded;

    // Incrementing the binary32 magnitude is side-effect free; nextafter would raise
    // host underflow/overflow flags while merely inspecting a neighboring value.
    const float away = std::bit_cast<float>(std::bit_cast<uint32_t>(rounded) + 1U);
    if (!std::isfinite(away)) return rounded;
    const double midpoint = (static_cast<double>(rounded) + static_cast<double>(away)) / 2.0;
    if (exact == midpoint) rounded = away;
    return rounded;
}

auto round_rmm_f64_sum(double lhs, double rhs) -> double {
    const double rounded = lhs + rhs;
    if (!std::isfinite(lhs) || !std::isfinite(rhs) || !std::isfinite(rounded) || rounded == 0.0)
        return rounded;

    // Knuth's TwoSum recovers the exact error of a rounded binary64 addition.
    // Inspect it with host flags held so only the guest addition accrues flags.
    fenv_t saved_env{};
    std::feholdexcept(&saved_env);
    const double rhs_rounded = rounded - lhs;
    const double residual = (lhs - (rounded - rhs_rounded)) + (rhs - rhs_rounded);
    const uint64_t magnitude_bits = std::bit_cast<uint64_t>(rounded) & UINT64_C(0x7FFFFFFFFFFFFFFF);
    const double magnitude = std::bit_cast<double>(magnitude_bits);
    const double away = std::bit_cast<double>(magnitude_bits + 1U);
    const bool tie_rounded_toward_zero = std::isfinite(away) && residual != 0.0 &&
                                         std::signbit(residual) == std::signbit(rounded) &&
                                         std::abs(residual) * 2.0 == away - magnitude;
    std::fesetenv(&saved_env);
    return tie_rounded_toward_zero
               ? std::bit_cast<double>((magnitude_bits + 1U) |
                                       (std::signbit(rounded) ? UINT64_C(0x8000000000000000) : 0))
               : rounded;
}

__extension__ using UnsignedDoubleWord = unsigned __int128;

struct Binary64Magnitude {
    UnsignedDoubleWord significand;
    int exponent;
};

auto decode_binary64_magnitude(uint64_t bits) -> Binary64Magnitude {
    constexpr uint64_t fraction_mask = UINT64_C(0x000FFFFFFFFFFFFF);
    const unsigned exponent = static_cast<unsigned>((bits >> 52U) & 0x7FFU);
    const uint64_t fraction = bits & fraction_mask;
    if (exponent == 0) return {fraction, -1074};
    return {(UINT64_C(1) << 52U) | fraction, static_cast<int>(exponent) - 1023 - 52};
}

void normalize_binary_magnitude(Binary64Magnitude& value) {
    while (value.significand != 0 && (value.significand & 1U) == 0) {
        value.significand >>= 1U;
        ++value.exponent;
    }
}

auto equal_binary_magnitude(Binary64Magnitude lhs, Binary64Magnitude rhs) -> bool {
    normalize_binary_magnitude(lhs);
    normalize_binary_magnitude(rhs);
    return lhs.significand == rhs.significand && lhs.exponent == rhs.exponent;
}

auto binary64_midpoint(uint64_t near_bits, uint64_t away_bits) -> Binary64Magnitude {
    const auto near = decode_binary64_magnitude(near_bits);
    const auto away = decode_binary64_magnitude(away_bits);
    const int common_exponent = std::min(near.exponent, away.exponent);
    return {(near.significand << (near.exponent - common_exponent)) +
                (away.significand << (away.exponent - common_exponent)),
            common_exponent - 1};
}

auto round_rmm_f64_product(double lhs, double rhs) -> double {
    const double rounded = lhs * rhs;
    if (!std::isfinite(lhs) || !std::isfinite(rhs) || !std::isfinite(rounded) || lhs == 0.0 ||
        rhs == 0.0)
        return rounded;

    constexpr uint64_t sign_bit = UINT64_C(0x8000000000000000);
    const uint64_t rounded_bits = std::bit_cast<uint64_t>(rounded);
    const uint64_t magnitude_bits = rounded_bits & ~sign_bit;
    const uint64_t away_bits = magnitude_bits + 1U;
    if ((away_bits & UINT64_C(0x7FF0000000000000)) == UINT64_C(0x7FF0000000000000)) return rounded;

    const auto lhs_value = decode_binary64_magnitude(std::bit_cast<uint64_t>(lhs) & ~sign_bit);
    const auto rhs_value = decode_binary64_magnitude(std::bit_cast<uint64_t>(rhs) & ~sign_bit);
    const Binary64Magnitude exact{lhs_value.significand * rhs_value.significand,
                                  lhs_value.exponent + rhs_value.exponent};
    if (!equal_binary_magnitude(exact, binary64_midpoint(magnitude_bits, away_bits)))
        return rounded;
    return std::bit_cast<double>(away_bits | (rounded_bits & sign_bit));
}

auto round_rmm_f64_quotient(double lhs, double rhs) -> double {
    const double rounded = lhs / rhs;
    if (!std::isfinite(lhs) || !std::isfinite(rhs) || !std::isfinite(rounded) || lhs == 0.0 ||
        rhs == 0.0)
        return rounded;

    constexpr uint64_t sign_bit = UINT64_C(0x8000000000000000);
    const uint64_t rounded_bits = std::bit_cast<uint64_t>(rounded);
    const uint64_t magnitude_bits = rounded_bits & ~sign_bit;
    const uint64_t away_bits = magnitude_bits + 1U;
    if ((away_bits & UINT64_C(0x7FF0000000000000)) == UINT64_C(0x7FF0000000000000)) return rounded;

    const auto dividend = decode_binary64_magnitude(std::bit_cast<uint64_t>(lhs) & ~sign_bit);
    const auto divisor = decode_binary64_magnitude(std::bit_cast<uint64_t>(rhs) & ~sign_bit);
    const auto midpoint = binary64_midpoint(magnitude_bits, away_bits);
    const Binary64Magnitude midpoint_product{midpoint.significand * divisor.significand,
                                             midpoint.exponent + divisor.exponent};
    if (!equal_binary_magnitude(dividend, midpoint_product)) return rounded;
    return std::bit_cast<double>(away_bits | (rounded_bits & sign_bit));
}

auto decode_binary32_magnitude(uint32_t bits) -> Binary64Magnitude {
    const unsigned exponent = (bits >> 23U) & 0xFFU;
    const uint32_t fraction = bits & UINT32_C(0x007FFFFF);
    if (exponent == 0) return {fraction, -149};
    return {(UINT32_C(1) << 23U) | fraction, static_cast<int>(exponent) - 127 - 23};
}

auto binary32_midpoint(uint32_t near_bits, uint32_t away_bits) -> Binary64Magnitude {
    const auto near = decode_binary32_magnitude(near_bits);
    const auto away = decode_binary32_magnitude(away_bits);
    const int common_exponent = std::min(near.exponent, away.exponent);
    return {(near.significand << (near.exponent - common_exponent)) +
                (away.significand << (away.exponent - common_exponent)),
            common_exponent - 1};
}

auto round_rmm_f32_quotient(float lhs, float rhs) -> float {
    const float rounded = lhs / rhs;
    if (!std::isfinite(lhs) || !std::isfinite(rhs) || !std::isfinite(rounded) || lhs == 0.0F ||
        rhs == 0.0F)
        return rounded;

    constexpr uint32_t sign_bit = UINT32_C(0x80000000);
    const uint32_t rounded_bits = std::bit_cast<uint32_t>(rounded);
    const uint32_t near_bits = rounded_bits & ~sign_bit;
    const uint32_t away_bits = near_bits + 1U;
    if ((away_bits & UINT32_C(0x7F800000)) == UINT32_C(0x7F800000)) return rounded;

    const auto dividend = decode_binary32_magnitude(std::bit_cast<uint32_t>(lhs) & ~sign_bit);
    const auto divisor = decode_binary32_magnitude(std::bit_cast<uint32_t>(rhs) & ~sign_bit);
    const auto midpoint = binary32_midpoint(near_bits, away_bits);
    const Binary64Magnitude midpoint_product{midpoint.significand * divisor.significand,
                                             midpoint.exponent + divisor.exponent};
    if (!equal_binary_magnitude(dividend, midpoint_product)) return rounded;
    return std::bit_cast<float>(away_bits | (rounded_bits & sign_bit));
}

template <typename Float>
auto round_rmm_sqrt(Float input) -> Float {
    static_assert(std::is_same_v<Float, float> || std::is_same_v<Float, double>);
    const Float rounded = std::sqrt(input);
    if (!std::isfinite(input) || !std::isfinite(rounded) || rounded == Float{0}) return rounded;

    using Bits = std::conditional_t<std::is_same_v<Float, float>, uint32_t, uint64_t>;
    constexpr Bits exponent_mask = std::is_same_v<Float, float>
                                       ? static_cast<Bits>(UINT32_C(0x7F800000))
                                       : static_cast<Bits>(UINT64_C(0x7FF0000000000000));
    const Bits near_bits = std::bit_cast<Bits>(rounded);
    const Bits away_bits = near_bits + 1U;
    if ((away_bits & exponent_mask) == exponent_mask) return rounded;

    const Binary64Magnitude operand = [&] {
        if constexpr (std::is_same_v<Float, float>)
            return decode_binary32_magnitude(std::bit_cast<uint32_t>(input));
        else
            return decode_binary64_magnitude(std::bit_cast<uint64_t>(input));
    }();
    const Binary64Magnitude midpoint = [&] {
        if constexpr (std::is_same_v<Float, float>)
            return binary32_midpoint(near_bits, away_bits);
        else
            return binary64_midpoint(near_bits, away_bits);
    }();
    const Binary64Magnitude midpoint_square{midpoint.significand * midpoint.significand,
                                            midpoint.exponent * 2};
    return equal_binary_magnitude(operand, midpoint_square) ? std::bit_cast<Float>(away_bits)
                                                            : rounded;
}

using ExactFmaWords = std::array<uint64_t, 80>;

void add_exact_fma_term(ExactFmaWords& sum, UnsignedDoubleWord significand, unsigned shift) {
    const size_t first = shift / 64U;
    const unsigned offset = shift % 64U;
    const uint64_t low = static_cast<uint64_t>(significand);
    const uint64_t high = static_cast<uint64_t>(significand >> 64U);
    const std::array<uint64_t, 3> words = {
        low << offset,
        (high << offset) | (offset == 0 ? 0 : low >> (64U - offset)),
        offset == 0 ? 0 : high >> (64U - offset),
    };
    uint64_t carry = 0;
    size_t index = first;
    for (const uint64_t word : words) {
        const UnsignedDoubleWord total = static_cast<UnsignedDoubleWord>(sum[index]) + word + carry;
        sum[index++] = static_cast<uint64_t>(total);
        carry = static_cast<uint64_t>(total >> 64U);
    }
    while (carry != 0) {
        const UnsignedDoubleWord total = static_cast<UnsignedDoubleWord>(sum[index]) + carry;
        sum[index++] = static_cast<uint64_t>(total);
        carry = static_cast<uint64_t>(total >> 64U);
    }
}

template <typename Float>
auto round_rmm_fma(Float multiplicand, Float multiplier, Float addend) -> Float {
    static_assert(std::is_same_v<Float, float> || std::is_same_v<Float, double>);
    const Float rounded = std::fma(multiplicand, multiplier, addend);
    if (!std::isfinite(multiplicand) || !std::isfinite(multiplier) || !std::isfinite(addend) ||
        !std::isfinite(rounded))
        return rounded;

    using Bits = std::conditional_t<std::is_same_v<Float, float>, uint32_t, uint64_t>;
    constexpr Bits sign_bit = Bits{1} << (sizeof(Bits) * 8U - 1U);
    constexpr Bits exponent_mask = std::is_same_v<Float, float>
                                       ? static_cast<Bits>(UINT32_C(0x7F800000))
                                       : static_cast<Bits>(UINT64_C(0x7FF0000000000000));
    const Bits rounded_bits = std::bit_cast<Bits>(rounded);
    const Bits near_bits = rounded_bits & ~sign_bit;
    const Bits away_bits = near_bits + 1U;
    if ((away_bits & exponent_mask) == exponent_mask) return rounded;

    const auto lhs = decode_binary64_magnitude(
        std::bit_cast<uint64_t>(static_cast<double>(multiplicand)) & ~UINT64_C(0x8000000000000000));
    const auto rhs = decode_binary64_magnitude(
        std::bit_cast<uint64_t>(static_cast<double>(multiplier)) & ~UINT64_C(0x8000000000000000));
    const auto added = decode_binary64_magnitude(
        std::bit_cast<uint64_t>(static_cast<double>(addend)) & ~UINT64_C(0x8000000000000000));
    const Binary64Magnitude product{lhs.significand * rhs.significand, lhs.exponent + rhs.exponent};
    const Binary64Magnitude midpoint = [&] {
        if constexpr (std::is_same_v<Float, float>)
            return binary32_midpoint(near_bits, away_bits);
        else
            return binary64_midpoint(near_bits, away_bits);
    }();
    const int common_exponent = std::min({product.exponent, added.exponent, midpoint.exponent});
    ExactFmaWords positives{};
    ExactFmaWords negatives{};
    add_exact_fma_term(
        std::signbit(multiplicand) != std::signbit(multiplier) ? negatives : positives,
        product.significand, static_cast<unsigned>(product.exponent - common_exponent));
    add_exact_fma_term(std::signbit(addend) ? negatives : positives, added.significand,
                       static_cast<unsigned>(added.exponent - common_exponent));
    add_exact_fma_term(std::signbit(rounded) ? positives : negatives, midpoint.significand,
                       static_cast<unsigned>(midpoint.exponent - common_exponent));
    if (positives != negatives) return rounded;
    return std::bit_cast<Float>(away_bits | (rounded_bits & sign_bit));
}

template <typename Float, typename Integer>
auto round_rmm_integer(Integer input) -> Float {
    static_assert(std::is_integral_v<Integer> && sizeof(Integer) <= sizeof(uint64_t));
    static_assert(std::is_same_v<Float, float> || std::is_same_v<Float, double>);
    using Bits = std::conditional_t<std::is_same_v<Float, float>, uint32_t, uint64_t>;
    constexpr unsigned fraction_bits = std::is_same_v<Float, float> ? 23U : 52U;
    constexpr unsigned exponent_bits = std::is_same_v<Float, float> ? 8U : 11U;
    constexpr int exponent_bias = std::is_same_v<Float, float> ? 127 : 1023;
    constexpr Bits sign_bit = Bits{1} << (fraction_bits + exponent_bits);
    constexpr Bits fraction_mask = (Bits{1} << fraction_bits) - 1U;

    const Float rounded = static_cast<Float>(input);
    const bool negative = [&] {
        if constexpr (std::is_signed_v<Integer>) return input < 0;
        return false;
    }();
    const uint64_t encoded = static_cast<uint64_t>(input);
    const uint64_t magnitude = negative ? uint64_t{0} - encoded : encoded;
    if (magnitude <= (uint64_t{1} << std::numeric_limits<Float>::digits)) return rounded;

    const auto integer_magnitude = [=](Bits raw) -> UnsignedDoubleWord {
        const unsigned exponent = (raw >> fraction_bits) & ((1U << exponent_bits) - 1U);
        const UnsignedDoubleWord significand =
            (UnsignedDoubleWord{1} << fraction_bits) | (raw & fraction_mask);
        const unsigned shift = static_cast<unsigned>(static_cast<int>(exponent) - exponent_bias -
                                                     static_cast<int>(fraction_bits));
        return significand << shift;
    };
    const Bits rounded_bits = std::bit_cast<Bits>(rounded) & ~sign_bit;
    const UnsignedDoubleWord rounded_integer = integer_magnitude(rounded_bits);
    // RNE may already have picked the away-from-zero neighbor when its low bit
    // is even. Only a result below the exact magnitude can need adjustment.
    if (rounded_integer >= magnitude) return rounded;

    const Bits away_bits = rounded_bits + 1U;
    const UnsignedDoubleWord away_integer = integer_magnitude(away_bits);
    const UnsignedDoubleWord midpoint = (rounded_integer + away_integer) / 2U;
    if (midpoint != magnitude) return rounded;
    return std::bit_cast<Float>(away_bits | (negative ? sign_bit : Bits{0}));
}

// Vector-Vector Floating-Point Addition
template <typename T>
void execute_vfadd_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;

        if constexpr (std::is_same_v<T, uint16_t>) {
            auto val1_raw = vector::get_group_element<uint16_t>(cpu.state().regs, rs1, i);
            auto val2_raw = vector::get_group_element<uint16_t>(cpu.state().regs, rs2, i);
            float val1 = vector::fp16_to_fp32(val1_raw);
            float val2 = vector::fp16_to_fp32(val2_raw);
            float res = canonicalize_nan(val2 + val1);
            vector::set_group_element<uint16_t>(cpu.state().regs, rd, i, vector::fp32_to_fp16(res));
        } else {
            T val1 = vector::get_group_element<T>(cpu.state().regs, rs1, i);
            T val2 = vector::get_group_element<T>(cpu.state().regs, rs2, i);
            T res;
            if constexpr (std::is_same_v<T, float>) {
                res = ((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm)
                          ? round_rmm_f32(static_cast<double>(val2) + static_cast<double>(val1))
                          : val2 + val1;
            } else if constexpr (std::is_same_v<T, double>) {
                res = ((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm)
                          ? round_rmm_f64_sum(val2, val1)
                          : val2 + val1;
            } else {
                res = val2 + val1;
            }
            res = canonicalize_nan(res);
            vector::set_group_element<T>(cpu.state().regs, rd, i, res);
        }
    }
}

// Vector-Scalar Floating-Point Addition
template <typename T>
void execute_vfadd_vf(core::CPU& cpu, RegId rd, FloatingRegister rs1_val, RegId rs2, bool vm,
                      uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;

        if constexpr (std::is_same_v<T, uint16_t>) {
            auto val1_raw = static_cast<uint16_t>(rs1_val & 0xFFFFULL);
            auto val2_raw = vector::get_group_element<uint16_t>(cpu.state().regs, rs2, i);
            float val1 = vector::fp16_to_fp32(val1_raw);
            float val2 = vector::fp16_to_fp32(val2_raw);
            float res = canonicalize_nan(val2 + val1);
            vector::set_group_element<uint16_t>(cpu.state().regs, rd, i, vector::fp32_to_fp16(res));
        } else if constexpr (std::is_same_v<T, float>) {
            float val1 = 0;
            if ((rs1_val & simrv::xlen::kF32BoxerBits) != simrv::xlen::kF32BoxerBits) {
                val1 = std::bit_cast<float>(0x7fc00000U);
            } else {
                val1 = std::bit_cast<float>(static_cast<uint32_t>(rs1_val & 0xFFFFFFFFULL));
            }
            auto val2 = vector::get_group_element<float>(cpu.state().regs, rs2, i);
            float res = canonicalize_nan(
                ((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm)
                    ? round_rmm_f32(static_cast<double>(val2) + static_cast<double>(val1))
                    : val2 + val1);
            vector::set_group_element<float>(cpu.state().regs, rd, i, res);
        } else {
            double val1 = std::bit_cast<double>(rs1_val);
            auto val2 = vector::get_group_element<double>(cpu.state().regs, rs2, i);
            double res = canonicalize_nan(((cpu.state().fcsr >> 5U) & 0x7U) ==
                                                  enum_mask(isa::RoundingMode::Rmm)
                                              ? round_rmm_f64_sum(val2, val1)
                                              : val2 + val1);
            vector::set_group_element<double>(cpu.state().regs, rd, i, res);
        }
    }
}

template <typename T, typename Op>
void execute_vf_binary_vv(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl,
                          Op op, bool subtraction = false, bool multiplication = false,
                          bool division = false) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        if constexpr (std::is_same_v<T, uint16_t>) {
            const float rhs =
                vector::fp16_to_fp32(vector::get_group_element<uint16_t>(cpu.state().regs, rs1, i));
            const float lhs =
                vector::fp16_to_fp32(vector::get_group_element<uint16_t>(cpu.state().regs, rs2, i));
            const float result = canonicalize_nan(op(lhs, rhs));
            vector::set_group_element<uint16_t>(cpu.state().regs, rd, i,
                                                vector::fp32_to_fp16(result));
        } else {
            const T rhs = vector::get_group_element<T>(cpu.state().regs, rs1, i);
            const T lhs = vector::get_group_element<T>(cpu.state().regs, rs2, i);
            T result;
            if constexpr (std::is_same_v<T, float>) {
                if (division &&
                    ((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm))
                    result = round_rmm_f32_quotient(lhs, rhs);
                else
                    result =
                        ((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm)
                            ? round_rmm_f32(op(static_cast<double>(lhs), static_cast<double>(rhs)))
                            : op(lhs, rhs);
            } else if constexpr (std::is_same_v<T, double>) {
                if (((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm) &&
                    subtraction)
                    result = round_rmm_f64_sum(lhs, -rhs);
                else if (((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm) &&
                         multiplication)
                    result = round_rmm_f64_product(lhs, rhs);
                else if (((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm) &&
                         division)
                    result = round_rmm_f64_quotient(lhs, rhs);
                else
                    result = op(lhs, rhs);
            } else {
                result = op(lhs, rhs);
            }
            vector::set_group_element<T>(cpu.state().regs, rd, i, canonicalize_nan(result));
        }
    }
}

template <typename T, typename Op>
void execute_vf_binary_vf(core::CPU& cpu, RegId rd, FloatingRegister rs1_val, RegId rs2, bool vm,
                          uint32_t vl, Op op, bool subtraction = false, bool reverse = false,
                          bool multiplication = false, bool division = false) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    if constexpr (std::is_same_v<T, uint16_t>) {
        const float rhs = vector::fp16_to_fp32(static_cast<uint16_t>(rs1_val));
        for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
            if (!vector::is_element_active(mask_reg, i, vm)) continue;
            const float lhs =
                vector::fp16_to_fp32(vector::get_group_element<uint16_t>(cpu.state().regs, rs2, i));
            const float result = canonicalize_nan(op(lhs, rhs));
            vector::set_group_element<uint16_t>(cpu.state().regs, rd, i,
                                                vector::fp32_to_fp16(result));
        }
    } else {
        T rhs{};
        if constexpr (std::is_same_v<T, float>) {
            rhs = (rs1_val & simrv::xlen::kF32BoxerBits) == simrv::xlen::kF32BoxerBits
                      ? std::bit_cast<float>(static_cast<uint32_t>(rs1_val))
                      : std::bit_cast<float>(simrv::xlen::kF32Qnan);
        } else {
            rhs = std::bit_cast<double>(rs1_val);
        }
        for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
            if (!vector::is_element_active(mask_reg, i, vm)) continue;
            const T lhs = vector::get_group_element<T>(cpu.state().regs, rs2, i);
            T result;
            if constexpr (std::is_same_v<T, float>) {
                if (division &&
                    ((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm))
                    result = reverse ? round_rmm_f32_quotient(rhs, lhs)
                                     : round_rmm_f32_quotient(lhs, rhs);
                else
                    result =
                        ((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm)
                            ? round_rmm_f32(op(static_cast<double>(lhs), static_cast<double>(rhs)))
                            : op(lhs, rhs);
            } else if constexpr (std::is_same_v<T, double>) {
                if (((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm) &&
                    subtraction)
                    result = reverse ? round_rmm_f64_sum(rhs, -lhs) : round_rmm_f64_sum(lhs, -rhs);
                else if (((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm) &&
                         multiplication)
                    result = round_rmm_f64_product(lhs, rhs);
                else if (((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm) &&
                         division)
                    result = reverse ? round_rmm_f64_quotient(rhs, lhs)
                                     : round_rmm_f64_quotient(lhs, rhs);
                else
                    result = op(lhs, rhs);
            } else {
                result = op(lhs, rhs);
            }
            vector::set_group_element<T>(cpu.state().regs, rd, i, canonicalize_nan(result));
        }
    }
}

template <typename T>
void execute_vfsqrt(core::CPU& cpu, RegId rd, RegId rs2, bool vm, uint32_t vl) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        if constexpr (std::is_same_v<T, uint16_t>) {
            const float value =
                vector::fp16_to_fp32(vector::get_group_element<uint16_t>(cpu.state().regs, rs2, i));
            vector::set_group_element<uint16_t>(
                cpu.state().regs, rd, i, vector::fp32_to_fp16(canonicalize_nan(std::sqrt(value))));
        } else {
            const T value = vector::get_group_element<T>(cpu.state().regs, rs2, i);
            const T result = ((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm)
                                 ? round_rmm_sqrt(value)
                                 : std::sqrt(value);
            vector::set_group_element<T>(cpu.state().regs, rd, i, canonicalize_nan(result));
        }
    }
}

template <typename Fn>
void dispatch_vector_fp_sew(uint32_t sew, Fn&& fn) {
    if (sew == 16)
        std::forward<Fn>(fn).template operator()<uint16_t>();
    else if (sew == 32)
        std::forward<Fn>(fn).template operator()<float>();
    else if (sew == 64)
        std::forward<Fn>(fn).template operator()<double>();
}

enum class FpCompareKind : uint8_t { Eq, Ne, Lt, Le, Gt, Ge };

template <typename T>
struct VectorFpElement;

template <>
struct VectorFpElement<uint16_t> {
    using Raw = uint16_t;
    using Value = float;
    static constexpr Raw kSign = UINT16_C(0x8000);
    static constexpr Raw kCanonicalNan = UINT16_C(0x7E00);
    static auto value(Raw raw) -> Value { return vector::fp16_to_fp32(raw); }
    static auto scalar(FloatingRegister raw) -> Raw { return static_cast<Raw>(raw); }
    static auto signaling_nan(Raw raw) -> bool {
        return (raw & UINT16_C(0x7C00)) == UINT16_C(0x7C00) && (raw & UINT16_C(0x03FF)) != 0 &&
               (raw & UINT16_C(0x0200)) == 0;
    }
};

template <>
struct VectorFpElement<float> {
    using Raw = uint32_t;
    using Value = float;
    static constexpr Raw kSign = UINT32_C(0x80000000);
    static constexpr Raw kCanonicalNan = UINT32_C(0x7FC00000);
    static auto value(Raw raw) -> Value { return std::bit_cast<float>(raw); }
    static auto scalar(FloatingRegister raw) -> Raw {
        return (raw & simrv::xlen::kF32BoxerBits) == simrv::xlen::kF32BoxerBits
                   ? static_cast<Raw>(raw)
                   : simrv::xlen::kF32Qnan;
    }
    static auto signaling_nan(Raw raw) -> bool {
        return (raw & UINT32_C(0x7F800000)) == UINT32_C(0x7F800000) &&
               (raw & UINT32_C(0x007FFFFF)) != 0 && (raw & UINT32_C(0x00400000)) == 0;
    }
};

template <>
struct VectorFpElement<double> {
    using Raw = uint64_t;
    using Value = double;
    static constexpr Raw kSign = UINT64_C(0x8000000000000000);
    static constexpr Raw kCanonicalNan = UINT64_C(0x7FF8000000000000);
    static auto value(Raw raw) -> Value { return std::bit_cast<double>(raw); }
    static auto scalar(FloatingRegister raw) -> Raw { return raw; }
    static auto signaling_nan(Raw raw) -> bool {
        return (raw & UINT64_C(0x7FF0000000000000)) == UINT64_C(0x7FF0000000000000) &&
               (raw & UINT64_C(0x000FFFFFFFFFFFFF)) != 0 &&
               (raw & UINT64_C(0x0008000000000000)) == 0;
    }
};

template <typename Raw>
constexpr auto fp_classify_raw(Raw raw) -> Raw {
    constexpr unsigned kBits = sizeof(Raw) * 8U;
    constexpr unsigned kFracBits = kBits == 16 ? 10U : (kBits == 32 ? 23U : 52U);
    constexpr unsigned kExpBits = kBits == 16 ? 5U : (kBits == 32 ? 8U : 11U);
    constexpr Raw kFracMask = (Raw{1} << kFracBits) - 1;
    constexpr Raw kExpMask = (Raw{1} << kExpBits) - 1;
    const bool sign = (raw >> (kBits - 1U)) != 0;
    const Raw exponent = (raw >> kFracBits) & kExpMask;
    const Raw fraction = raw & kFracMask;
    if (exponent == kExpMask) {
        if (fraction == 0) return Raw{1} << (sign ? 0U : 7U);
        const bool quiet = (fraction & (Raw{1} << (kFracBits - 1U))) != 0;
        return Raw{1} << (quiet ? 9U : 8U);
    }
    if (exponent == 0) {
        if (fraction == 0) return Raw{1} << (sign ? 3U : 4U);
        return Raw{1} << (sign ? 2U : 5U);
    }
    return Raw{1} << (sign ? 1U : 6U);
}

template <typename T>
void execute_vfclass(core::CPU& cpu, RegId rd, RegId rs2, bool vm, uint32_t vl) {
    using Raw = typename VectorFpElement<T>::Raw;
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    std::vector<bool> active(vl);
    for (uint32_t i = 0; i < vl; ++i) active[i] = vector::is_element_active(mask_reg, i, vm);
    std::vector<Raw> source(vl);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (active[i]) source[i] = vector::get_group_element<Raw>(cpu.state().regs, rs2, i);
    }
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (active[i])
            vector::set_group_element<Raw>(cpu.state().regs, rd, i, fp_classify_raw(source[i]));
    }
}

template <typename Raw>
constexpr auto bit_mask(unsigned pos, unsigned length) -> uint64_t {
    return ((UINT64_C(1) << length) - 1U) << pos;
}

template <typename T>
auto fp_estimate7_raw(typename VectorFpElement<T>::Raw input, bool reciprocal_sqrt, Word rm,
                      CSRValue& flags) -> typename VectorFpElement<T>::Raw {
    using E = VectorFpElement<T>;
    using Raw = typename E::Raw;
    constexpr unsigned kBits = sizeof(Raw) * 8U;
    constexpr unsigned kSigBits = kBits == 16 ? 10U : (kBits == 32 ? 23U : 52U);
    constexpr unsigned kExpBits = kBits == 16 ? 5U : (kBits == 32 ? 8U : 11U);
    constexpr uint64_t kExpMask = bit_mask<Raw>(0, kExpBits);
    constexpr uint64_t kSignMask = UINT64_C(1) << (kSigBits + kExpBits);
    constexpr uint64_t kInfinity = kExpMask << kSigBits;
    constexpr std::array<uint8_t, 128> kRsqrtTable = {
        52,  51,  50,  48,  47,  46, 44, 43,  42,  41,  40,  39,  38,  36,  35,  34,  33,  32,  31,
        30,  30,  29,  28,  27,  26, 25, 24,  23,  23,  22,  21,  20,  19,  19,  18,  17,  16,  16,
        15,  14,  14,  13,  12,  12, 11, 10,  10,  9,   9,   8,   7,   7,   6,   6,   5,   4,   4,
        3,   3,   2,   2,   1,   1,  0,  127, 125, 123, 121, 119, 118, 116, 114, 113, 111, 109, 108,
        106, 105, 103, 102, 100, 99, 97, 96,  95,  93,  92,  91,  90,  88,  87,  86,  85,  84,  83,
        82,  80,  79,  78,  77,  76, 75, 74,  73,  72,  71,  70,  70,  69,  68,  67,  66,  65,  64,
        63,  63,  62,  61,  60,  59, 59, 58,  57,  56,  56,  55,  54,  53};
    constexpr std::array<uint8_t, 128> kRecipTable = {
        127, 125, 123, 121, 119, 117, 116, 114, 112, 110, 109, 107, 105, 104, 102, 100, 99, 97, 96,
        94,  93,  91,  90,  88,  87,  85,  84,  83,  81,  80,  79,  77,  76,  75,  74,  72, 71, 70,
        69,  68,  66,  65,  64,  63,  62,  61,  60,  59,  58,  57,  56,  55,  54,  53,  52, 51, 50,
        49,  48,  47,  46,  45,  44,  43,  42,  41,  40,  40,  39,  38,  37,  36,  35,  35, 34, 33,
        32,  31,  31,  30,  29,  28,  28,  27,  26,  25,  25,  24,  23,  23,  22,  21,  21, 20, 19,
        19,  18,  17,  17,  16,  15,  15,  14,  14,  13,  12,  12,  11,  11,  10,  9,   9,  8,  8,
        7,   7,   6,   5,   5,   4,   4,   3,   3,   2,   2,   1,   1,   0};

    const Raw classification = fp_classify_raw(input);
    const uint64_t raw = input;
    const uint64_t sign = (raw & kSignMask) != 0;
    if (reciprocal_sqrt) {
        if ((classification &
             ((Raw{1} << 0U) | (Raw{1} << 1U) | (Raw{1} << 2U) | (Raw{1} << 8U))) != 0) {
            flags |= enum_mask(isa::FflagsBit::Nv);
            return E::kCanonicalNan;
        }
        if ((classification & (Raw{1} << 9U)) != 0) return E::kCanonicalNan;
        if ((classification & ((Raw{1} << 3U) | (Raw{1} << 4U))) != 0) {
            flags |= enum_mask(isa::FflagsBit::Dz);
            return static_cast<Raw>((sign << (kSigBits + kExpBits)) | kInfinity);
        }
        if ((classification & (Raw{1} << 7U)) != 0) return Raw{0};
    } else {
        if ((classification & (Raw{1} << 8U)) != 0) {
            flags |= enum_mask(isa::FflagsBit::Nv);
            return E::kCanonicalNan;
        }
        if ((classification & (Raw{1} << 9U)) != 0) return E::kCanonicalNan;
        if ((classification & ((Raw{1} << 0U) | (Raw{1} << 7U))) != 0)
            return static_cast<Raw>(sign << (kSigBits + kExpBits));
        if ((classification & ((Raw{1} << 3U) | (Raw{1} << 4U))) != 0) {
            flags |= enum_mask(isa::FflagsBit::Dz);
            return static_cast<Raw>((sign << (kSigBits + kExpBits)) | kInfinity);
        }
    }

    uint64_t exponent = (raw >> kSigBits) & kExpMask;
    uint64_t significand = raw & bit_mask<Raw>(0, kSigBits);
    const bool subnormal = (classification & ((Raw{1} << 2U) | (Raw{1} << 5U))) != 0;
    if (subnormal) {
        while (((significand >> (kSigBits - 1U)) & 1U) == 0) {
            --exponent;
            significand <<= 1U;
        }
        significand = (significand << 1U) & bit_mask<Raw>(0, kSigBits);
        if (!reciprocal_sqrt && exponent != 0 && exponent != UINT64_MAX) {
            flags |= enum_mask(isa::FflagsBit::Nx) | enum_mask(isa::FflagsBit::Of);
            const bool maximum = rm == enum_mask(isa::RoundingMode::Rtz) ||
                                 (rm == enum_mask(isa::RoundingMode::Rdn) && !sign) ||
                                 (rm == enum_mask(isa::RoundingMode::Rup) && sign);
            return static_cast<Raw>(maximum ? ((sign << (kSigBits + kExpBits)) | kInfinity) - 1U
                                            : (sign << (kSigBits + kExpBits)) | kInfinity);
        }
    }

    uint64_t output_significand{};
    uint64_t output_exponent{};
    if (reciprocal_sqrt) {
        const size_t index = ((exponent & 1U) << 6U) | (significand >> (kSigBits - 6U));
        output_significand = uint64_t{kRsqrtTable[index]} << (kSigBits - 7U);
        output_exponent = (3U * bit_mask<Raw>(0, kExpBits - 1U) + ~exponent) / 2U;
    } else {
        const size_t index = significand >> (kSigBits - 7U);
        output_significand = uint64_t{kRecipTable[index]} << (kSigBits - 7U);
        output_exponent = 2U * bit_mask<Raw>(0, kExpBits - 1U) + ~exponent;
        if (output_exponent == 0 || output_exponent == UINT64_MAX) {
            output_significand = (output_significand >> 1U) | bit_mask<Raw>(kSigBits - 1U, 1U);
            if (output_exponent == UINT64_MAX) {
                output_significand >>= 1U;
                output_exponent = 0;
            }
        }
    }
    return static_cast<Raw>((sign << (kSigBits + kExpBits)) | (output_exponent << kSigBits) |
                            output_significand);
}

template <typename T>
void execute_vf_estimate7(core::CPU& cpu, RegId rd, RegId rs2, bool vm, uint32_t vl,
                          bool reciprocal_sqrt, CSRValue& flags) {
    using Raw = typename VectorFpElement<T>::Raw;
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        const Raw input = vector::get_group_element<Raw>(cpu.state().regs, rs2, i);
        const Raw output =
            fp_estimate7_raw<T>(input, reciprocal_sqrt, (cpu.state().fcsr >> 5U) & 0x7U, flags);
        vector::set_group_element<Raw>(cpu.state().regs, rd, i, output);
    }
}

template <typename T>
void execute_vfsgnj(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1, RegId rs2,
                    bool scalar, bool vm, uint32_t vl) {
    using E = VectorFpElement<T>;
    using Raw = typename E::Raw;
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    std::vector<bool> active(vl);
    for (uint32_t i = 0; i < vl; ++i) active[i] = vector::is_element_active(mask_reg, i, vm);
    const Raw scalar_raw = scalar ? E::scalar(read_vector_scalar_fp(cpu, rs1)) : Raw{0};
    std::vector<Raw> lhs_values(vl);
    std::vector<Raw> sign_values(vl);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        lhs_values[i] = vector::get_group_element<Raw>(cpu.state().regs, rs2, i);
        sign_values[i] =
            scalar ? scalar_raw : vector::get_group_element<Raw>(cpu.state().regs, rs1, i);
    }
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        const Raw lhs = lhs_values[i];
        const Raw magnitude = lhs & ~E::kSign;
        Raw sign = sign_values[i] & E::kSign;
        if (op_id == isa::OperationId::VFSGNJN_VV || op_id == isa::OperationId::VFSGNJN_VF)
            sign ^= E::kSign;
        else if (op_id == isa::OperationId::VFSGNJX_VV || op_id == isa::OperationId::VFSGNJX_VF)
            sign ^= lhs & E::kSign;
        vector::set_group_element<Raw>(cpu.state().regs, rd, i, magnitude | sign);
    }
}

template <typename T>
void execute_vfminmax(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool scalar, bool vm,
                      uint32_t vl, bool maximum) {
    using E = VectorFpElement<T>;
    using Raw = typename E::Raw;
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    std::vector<bool> active(vl);
    for (uint32_t i = 0; i < vl; ++i) active[i] = vector::is_element_active(mask_reg, i, vm);
    const Raw scalar_raw = scalar ? E::scalar(read_vector_scalar_fp(cpu, rs1)) : Raw{0};
    std::vector<Raw> lhs_values(vl);
    std::vector<Raw> rhs_values(vl);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        lhs_values[i] = vector::get_group_element<Raw>(cpu.state().regs, rs2, i);
        rhs_values[i] =
            scalar ? scalar_raw : vector::get_group_element<Raw>(cpu.state().regs, rs1, i);
    }
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        const Raw lhs_raw = lhs_values[i];
        const Raw rhs_raw = rhs_values[i];
        const auto lhs = E::value(lhs_raw);
        const auto rhs = E::value(rhs_raw);
        const bool lhs_nan = std::isnan(lhs);
        const bool rhs_nan = std::isnan(rhs);
        if (E::signaling_nan(lhs_raw) || E::signaling_nan(rhs_raw)) std::feraiseexcept(FE_INVALID);

        Raw result = E::kCanonicalNan;
        if (lhs_nan != rhs_nan)
            result = lhs_nan ? rhs_raw : lhs_raw;
        else if (!lhs_nan) {
            if (lhs == rhs) {
                // IEEE minimumNumber selects -0 and maximumNumber selects +0.
                result = maximum ? (lhs_raw & rhs_raw) : (lhs_raw | rhs_raw);
            } else {
                result = (maximum ? lhs > rhs : lhs < rhs) ? lhs_raw : rhs_raw;
            }
        }
        vector::set_group_element<Raw>(cpu.state().regs, rd, i, result);
    }
}

template <typename T>
auto fp_result_raw(typename VectorFpElement<T>::Value value) -> typename VectorFpElement<T>::Raw {
    if constexpr (std::is_same_v<T, uint16_t>)
        return vector::fp32_to_fp16(value);
    else
        return std::bit_cast<typename VectorFpElement<T>::Raw>(value);
}

template <typename T>
void execute_vfred(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1, RegId rs2, bool vm,
                   uint32_t vl) {
    if (vl == 0) return;
    using E = VectorFpElement<T>;
    using Raw = typename E::Raw;
    using Value = typename E::Value;
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    std::vector<bool> active(vl);
    std::vector<Raw> source(vl);
    bool any_active = false;
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        active[i] = vector::is_element_active(mask_reg, i, vm);
        if (!active[i]) continue;
        any_active = true;
        source[i] = vector::get_group_element<Raw>(cpu.state().regs, rs2, i);
    }
    Raw accumulator = vector::get_group_element<Raw>(cpu.state().regs, rs1, 0);
    if (!any_active) {
        vector::set_group_element<Raw>(cpu.state().regs, rd, 0, accumulator);
        return;
    }

    const bool sum =
        op_id == isa::OperationId::VFREDUSUM_VS || op_id == isa::OperationId::VFREDOSUM_VS;
    const bool maximum = op_id == isa::OperationId::VFREDMAX_VS;
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        const Raw rhs_raw = source[i];
        const Value lhs = E::value(accumulator);
        const Value rhs = E::value(rhs_raw);
        if (sum) {
            Value result;
            if constexpr (std::is_same_v<T, float>) {
                result = ((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm)
                             ? round_rmm_f32(static_cast<double>(lhs) + static_cast<double>(rhs))
                             : lhs + rhs;
            } else if constexpr (std::is_same_v<T, double>) {
                result = ((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm)
                             ? round_rmm_f64_sum(lhs, rhs)
                             : lhs + rhs;
            } else {
                result = lhs + rhs;
            }
            accumulator = fp_result_raw<T>(canonicalize_nan(result));
            continue;
        }

        const bool lhs_nan = std::isnan(lhs);
        const bool rhs_nan = std::isnan(rhs);
        if (E::signaling_nan(accumulator) || E::signaling_nan(rhs_raw))
            std::feraiseexcept(FE_INVALID);
        if (lhs_nan && rhs_nan)
            accumulator = E::kCanonicalNan;
        else if (lhs_nan != rhs_nan)
            accumulator = lhs_nan ? rhs_raw : accumulator;
        else if (lhs == rhs)
            accumulator = maximum ? (accumulator & rhs_raw) : (accumulator | rhs_raw);
        else if ((maximum && rhs > lhs) || (!maximum && rhs < lhs))
            accumulator = rhs_raw;
    }
    vector::set_group_element<Raw>(cpu.state().regs, rd, 0, accumulator);
}

template <typename NarrowT, typename WideT>
void execute_vfwred(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool vm, uint32_t vl) {
    if (vl == 0) return;
    using NarrowE = VectorFpElement<NarrowT>;
    using WideE = VectorFpElement<WideT>;
    using NarrowRaw = typename NarrowE::Raw;
    using WideRaw = typename WideE::Raw;
    using WideValue = typename WideE::Value;
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    std::vector<bool> active(vl);
    std::vector<NarrowRaw> source(vl);
    bool any_active = false;
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        active[i] = vector::is_element_active(mask_reg, i, vm);
        if (!active[i]) continue;
        any_active = true;
        source[i] = vector::get_group_element<NarrowRaw>(cpu.state().regs, rs2, i);
    }
    WideRaw accumulator = vector::get_group_element<WideRaw>(cpu.state().regs, rs1, 0);
    if (!any_active) {
        vector::set_group_element<WideRaw>(cpu.state().regs, rd, 0, accumulator);
        return;
    }
    WideValue value = WideE::value(accumulator);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        const WideValue rhs = static_cast<WideValue>(NarrowE::value(source[i]));
        if constexpr (std::is_same_v<WideT, float>) {
            value = canonicalize_nan(
                ((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm)
                    ? round_rmm_f32(static_cast<double>(value) + static_cast<double>(rhs))
                    : value + rhs);
        } else if constexpr (std::is_same_v<WideT, double>) {
            value = canonicalize_nan(((cpu.state().fcsr >> 5U) & 0x7U) ==
                                             enum_mask(isa::RoundingMode::Rmm)
                                         ? round_rmm_f64_sum(value, rhs)
                                         : value + rhs);
        }
        accumulator = fp_result_raw<WideT>(value);
        value = WideE::value(accumulator);
    }
    vector::set_group_element<WideRaw>(cpu.state().regs, rd, 0, accumulator);
}

template <typename NarrowT, typename WideT>
void execute_vfw_binary(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1, RegId rs2,
                        bool scalar, bool wide_lhs, bool vm, uint32_t vl) {
    using NarrowE = VectorFpElement<NarrowT>;
    using WideE = VectorFpElement<WideT>;
    using NarrowRaw = typename NarrowE::Raw;
    using WideRaw = typename WideE::Raw;
    using WideValue = typename WideE::Value;
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    std::vector<bool> active(vl);
    for (uint32_t i = 0; i < vl; ++i) active[i] = vector::is_element_active(mask_reg, i, vm);
    const WideValue scalar_value = scalar ? static_cast<WideValue>(NarrowE::value(
                                                NarrowE::scalar(read_vector_scalar_fp(cpu, rs1))))
                                          : WideValue{};
    std::vector<WideValue> lhs_values(vl);
    std::vector<WideValue> rhs_values(vl);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        lhs_values[i] =
            wide_lhs ? WideE::value(vector::get_group_element<WideRaw>(cpu.state().regs, rs2, i))
                     : static_cast<WideValue>(NarrowE::value(
                           vector::get_group_element<NarrowRaw>(cpu.state().regs, rs2, i)));
        rhs_values[i] = scalar
                            ? scalar_value
                            : static_cast<WideValue>(NarrowE::value(
                                  vector::get_group_element<NarrowRaw>(cpu.state().regs, rs1, i)));
    }
    const bool subtract =
        op_id == isa::OperationId::VFWSUB_VV || op_id == isa::OperationId::VFWSUB_VF ||
        op_id == isa::OperationId::VFWSUB_WV || op_id == isa::OperationId::VFWSUB_WF;
    const bool multiply =
        op_id == isa::OperationId::VFWMUL_VV || op_id == isa::OperationId::VFWMUL_VF;
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        WideValue result;
        if (multiply) {
            if constexpr (std::is_same_v<WideT, double>) {
                result = ((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm)
                             ? round_rmm_f64_product(lhs_values[i], rhs_values[i])
                             : lhs_values[i] * rhs_values[i];
            } else {
                result = lhs_values[i] * rhs_values[i];
            }
        } else if constexpr (std::is_same_v<WideT, double>) {
            if (((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm))
                result =
                    round_rmm_f64_sum(lhs_values[i], subtract ? -rhs_values[i] : rhs_values[i]);
            else
                result = subtract ? lhs_values[i] - rhs_values[i] : lhs_values[i] + rhs_values[i];
        } else {
            result = subtract ? lhs_values[i] - rhs_values[i] : lhs_values[i] + rhs_values[i];
        }
        vector::set_group_element<WideRaw>(cpu.state().regs, rd, i,
                                           fp_result_raw<WideT>(canonicalize_nan(result)));
    }
}

template <typename Value>
auto fp_compare(Value lhs, Value rhs, FpCompareKind kind) -> bool {
    switch (kind) {
        case FpCompareKind::Eq:
            return lhs == rhs;
        case FpCompareKind::Ne:
            return lhs != rhs;
        case FpCompareKind::Lt:
            return lhs < rhs;
        case FpCompareKind::Le:
            return lhs <= rhs;
        case FpCompareKind::Gt:
            return lhs > rhs;
        case FpCompareKind::Ge:
            return lhs >= rhs;
    }
    return false;
}

template <typename T>
void execute_vmf_compare(core::CPU& cpu, RegId rd, RegId rs1, RegId rs2, bool scalar, bool vm,
                         uint32_t vl, FpCompareKind kind) {
    using E = VectorFpElement<T>;
    using Raw = typename E::Raw;
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    std::vector<bool> active(vl);
    for (uint32_t i = 0; i < vl; ++i) active[i] = vector::is_element_active(mask_reg, i, vm);
    const Raw scalar_raw = scalar ? E::scalar(read_vector_scalar_fp(cpu, rs1)) : Raw{0};
    std::vector<Raw> lhs_values(vl);
    std::vector<Raw> rhs_values(vl);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        lhs_values[i] = vector::get_group_element<Raw>(cpu.state().regs, rs2, i);
        rhs_values[i] =
            scalar ? scalar_raw : vector::get_group_element<Raw>(cpu.state().regs, rs1, i);
    }
    auto& dest = cpu.state().regs.read_vector(rd);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        const Raw lhs_raw = lhs_values[i];
        const Raw rhs_raw = rhs_values[i];
        const auto lhs = E::value(lhs_raw);
        const auto rhs = E::value(rhs_raw);
        const bool unordered = std::isnan(lhs) || std::isnan(rhs);
        const bool quiet = kind == FpCompareKind::Eq || kind == FpCompareKind::Ne;
        if ((!quiet && unordered) || E::signaling_nan(lhs_raw) || E::signaling_nan(rhs_raw)) {
            std::feraiseexcept(FE_INVALID);
        }
        vector::set_mask_bit(dest, i, fp_compare(lhs, rhs, kind));
    }
}

auto compare_kind(isa::OperationId op_id) -> FpCompareKind {
    switch (op_id) {
        case isa::OperationId::VMFEQ_VV:
        case isa::OperationId::VMFEQ_VF:
            return FpCompareKind::Eq;
        case isa::OperationId::VMFNE_VV:
        case isa::OperationId::VMFNE_VF:
            return FpCompareKind::Ne;
        case isa::OperationId::VMFLT_VV:
        case isa::OperationId::VMFLT_VF:
            return FpCompareKind::Lt;
        case isa::OperationId::VMFLE_VV:
        case isa::OperationId::VMFLE_VF:
            return FpCompareKind::Le;
        case isa::OperationId::VMFGT_VF:
            return FpCompareKind::Gt;
        case isa::OperationId::VMFGE_VF:
            return FpCompareKind::Ge;
        default:
            return FpCompareKind::Eq;
    }
}

enum class FpFmaKind : uint8_t { MAdd, NMAdd, MSub, NMSub, MAcc, NMAcc, MSac, NMSac };

auto fma_kind(isa::OperationId op_id) -> FpFmaKind {
    switch (op_id) {
        case isa::OperationId::VFMADD_VV:
        case isa::OperationId::VFMADD_VF:
            return FpFmaKind::MAdd;
        case isa::OperationId::VFNMADD_VV:
        case isa::OperationId::VFNMADD_VF:
            return FpFmaKind::NMAdd;
        case isa::OperationId::VFMSUB_VV:
        case isa::OperationId::VFMSUB_VF:
            return FpFmaKind::MSub;
        case isa::OperationId::VFNMSUB_VV:
        case isa::OperationId::VFNMSUB_VF:
            return FpFmaKind::NMSub;
        case isa::OperationId::VFNMACC_VV:
        case isa::OperationId::VFNMACC_VF:
            return FpFmaKind::NMAcc;
        case isa::OperationId::VFMSAC_VV:
        case isa::OperationId::VFMSAC_VF:
            return FpFmaKind::MSac;
        case isa::OperationId::VFNMSAC_VV:
        case isa::OperationId::VFNMSAC_VF:
            return FpFmaKind::NMSac;
        default:
            return FpFmaKind::MAcc;
    }
}

template <typename Value>
auto fused_result(Value multiplicand, Value vd, Value vs2, FpFmaKind kind, bool rmm) -> Value {
    const auto fused = [rmm](Value lhs, Value rhs, Value addend) {
        return rmm ? round_rmm_fma(lhs, rhs, addend) : std::fma(lhs, rhs, addend);
    };
    switch (kind) {
        case FpFmaKind::MAdd:
            return fused(multiplicand, vd, vs2);
        case FpFmaKind::NMAdd:
            return fused(-multiplicand, vd, -vs2);
        case FpFmaKind::MSub:
            return fused(multiplicand, vd, -vs2);
        case FpFmaKind::NMSub:
            return fused(-multiplicand, vd, vs2);
        case FpFmaKind::MAcc:
            return fused(multiplicand, vs2, vd);
        case FpFmaKind::NMAcc:
            return fused(-multiplicand, vs2, -vd);
        case FpFmaKind::MSac:
            return fused(multiplicand, vs2, -vd);
        case FpFmaKind::NMSac:
            return fused(-multiplicand, vs2, vd);
    }
    return vd;
}

template <typename T>
void execute_vf_fma(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1, RegId rs2,
                    bool scalar, bool vm, uint32_t vl) {
    using E = VectorFpElement<T>;
    using Raw = typename E::Raw;
    using Value = typename E::Value;
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    std::vector<bool> active(vl);
    for (uint32_t i = 0; i < vl; ++i) active[i] = vector::is_element_active(mask_reg, i, vm);
    const Value scalar_value =
        scalar ? E::value(E::scalar(read_vector_scalar_fp(cpu, rs1))) : Value{};
    std::vector<Value> multiplicands(vl);
    std::vector<Value> vs2_values(vl);
    std::vector<Value> vd_values(vl);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        multiplicands[i] = scalar
                               ? scalar_value
                               : E::value(vector::get_group_element<Raw>(cpu.state().regs, rs1, i));
        vs2_values[i] = E::value(vector::get_group_element<Raw>(cpu.state().regs, rs2, i));
        vd_values[i] = E::value(vector::get_group_element<Raw>(cpu.state().regs, rd, i));
    }
    const auto kind = fma_kind(op_id);
    const bool rmm = !std::is_same_v<T, uint16_t> &&
                     ((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        const Value result = canonicalize_nan(
            fused_result(multiplicands[i], vd_values[i], vs2_values[i], kind, rmm));
        if constexpr (std::is_same_v<T, uint16_t>)
            vector::set_group_element<uint16_t>(cpu.state().regs, rd, i,
                                                vector::fp32_to_fp16(result));
        else
            vector::set_group_element<T>(cpu.state().regs, rd, i, result);
    }
}

template <typename NarrowT, typename WideT>
void execute_vfw_fma(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1, RegId rs2,
                     bool scalar, bool vm, uint32_t vl) {
    using NarrowE = VectorFpElement<NarrowT>;
    using WideE = VectorFpElement<WideT>;
    using NarrowRaw = typename NarrowE::Raw;
    using WideRaw = typename WideE::Raw;
    using WideValue = typename WideE::Value;
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    std::vector<bool> active(vl);
    for (uint32_t i = 0; i < vl; ++i) active[i] = vector::is_element_active(mask_reg, i, vm);
    const WideValue scalar_value = scalar ? static_cast<WideValue>(NarrowE::value(
                                                NarrowE::scalar(read_vector_scalar_fp(cpu, rs1))))
                                          : WideValue{};
    std::vector<WideValue> lhs_values(vl);
    std::vector<WideValue> rhs_values(vl);
    std::vector<WideValue> vd_values(vl);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        lhs_values[i] = static_cast<WideValue>(
            NarrowE::value(vector::get_group_element<NarrowRaw>(cpu.state().regs, rs2, i)));
        rhs_values[i] = scalar
                            ? scalar_value
                            : static_cast<WideValue>(NarrowE::value(
                                  vector::get_group_element<NarrowRaw>(cpu.state().regs, rs1, i)));
        vd_values[i] = WideE::value(vector::get_group_element<WideRaw>(cpu.state().regs, rd, i));
    }
    FpFmaKind kind = FpFmaKind::MAcc;
    if (op_id == isa::OperationId::VFWNMACC_VV || op_id == isa::OperationId::VFWNMACC_VF)
        kind = FpFmaKind::NMAcc;
    else if (op_id == isa::OperationId::VFWMSAC_VV || op_id == isa::OperationId::VFWMSAC_VF)
        kind = FpFmaKind::MSac;
    else if (op_id == isa::OperationId::VFWNMSAC_VV || op_id == isa::OperationId::VFWNMSAC_VF)
        kind = FpFmaKind::NMSac;
    const bool rmm = ((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        const WideValue result =
            canonicalize_nan(fused_result(rhs_values[i], vd_values[i], lhs_values[i], kind, rmm));
        vector::set_group_element<WideRaw>(cpu.state().regs, rd, i, fp_result_raw<WideT>(result));
    }
}

template <typename Int, typename Float>
auto saturating_fp_to_int(Float value, bool force_rtz, CSRValue fcsr, CSRValue& flags) -> Int {
    const bool invalid_positive = std::isnan(value) || value > 0;
    if (!std::isfinite(value)) {
        flags |= enum_mask(isa::FflagsBit::Nv);
        if constexpr (std::is_unsigned_v<Int>)
            return invalid_positive ? std::numeric_limits<Int>::max() : Int{0};
        else
            return invalid_positive ? std::numeric_limits<Int>::max()
                                    : std::numeric_limits<Int>::min();
    }

    const long double input = static_cast<long double>(value);
    const Word rm = force_rtz ? enum_mask(isa::RoundingMode::Rtz) : ((fcsr >> 5U) & 0x7U);
    long double rounded{};
    if (rm == enum_mask(isa::RoundingMode::Rtz))
        rounded = std::trunc(input);
    else if (rm == enum_mask(isa::RoundingMode::Rdn))
        rounded = std::floor(input);
    else if (rm == enum_mask(isa::RoundingMode::Rup))
        rounded = std::ceil(input);
    else if (rm == enum_mask(isa::RoundingMode::Rmm))
        rounded = std::round(input);
    else
        rounded = std::nearbyint(input);

    const long double minimum = static_cast<long double>(std::numeric_limits<Int>::min());
    const long double maximum = static_cast<long double>(std::numeric_limits<Int>::max());
    if (rounded < minimum || rounded > maximum) {
        flags |= enum_mask(isa::FflagsBit::Nv);
        return rounded < minimum ? std::numeric_limits<Int>::min()
                                 : std::numeric_limits<Int>::max();
    }
    if (rounded != input) flags |= enum_mask(isa::FflagsBit::Nx);
    return static_cast<Int>(rounded);
}

template <typename T>
void execute_vfcvt(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs2, bool vm,
                   uint32_t vl, CSRValue& flags) {
    using Raw = typename VectorFpElement<T>::Raw;
    using Signed = std::make_signed_t<Raw>;
    const bool to_float =
        op_id == isa::OperationId::VFCVT_F_XU_V || op_id == isa::OperationId::VFCVT_F_X_V;
    const bool unsigned_int = op_id == isa::OperationId::VFCVT_XU_F_V ||
                              op_id == isa::OperationId::VFCVT_RTZ_XU_F_V ||
                              op_id == isa::OperationId::VFCVT_F_XU_V;
    const bool force_rtz =
        op_id == isa::OperationId::VFCVT_RTZ_XU_F_V || op_id == isa::OperationId::VFCVT_RTZ_X_F_V;
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        const Raw source = vector::get_group_element<Raw>(cpu.state().regs, rs2, i);
        if (to_float) {
            const long double integer = unsigned_int
                                            ? static_cast<long double>(source)
                                            : static_cast<long double>(static_cast<Signed>(source));
            typename VectorFpElement<T>::Value result;
            if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
                if (((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm)) {
                    result = unsigned_int ? round_rmm_integer<T>(source)
                                          : round_rmm_integer<T>(static_cast<Signed>(source));
                } else {
                    result = static_cast<T>(integer);
                }
            } else {
                result = static_cast<typename VectorFpElement<T>::Value>(integer);
            }
            vector::set_group_element<Raw>(cpu.state().regs, rd, i, fp_result_raw<T>(result));
        } else {
            const auto value = VectorFpElement<T>::value(source);
            const Raw result =
                unsigned_int ? saturating_fp_to_int<Raw>(value, force_rtz, cpu.state().fcsr, flags)
                             : static_cast<Raw>(saturating_fp_to_int<Signed>(
                                   value, force_rtz, cpu.state().fcsr, flags));
            vector::set_group_element<Raw>(cpu.state().regs, rd, i, result);
        }
    }
}

template <typename NarrowT, typename WideT>
void execute_vfwcvt(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs2, bool vm,
                    uint32_t vl, CSRValue& flags) {
    using NarrowE = VectorFpElement<NarrowT>;
    using WideE = VectorFpElement<WideT>;
    using NarrowRaw = typename NarrowE::Raw;
    using NarrowSigned = std::make_signed_t<NarrowRaw>;
    using WideRaw = typename WideE::Raw;
    using WideSigned = std::make_signed_t<WideRaw>;
    const bool float_to_int =
        op_id == isa::OperationId::VFWCVT_XU_F_V || op_id == isa::OperationId::VFWCVT_X_F_V ||
        op_id == isa::OperationId::VFWCVT_RTZ_XU_F_V || op_id == isa::OperationId::VFWCVT_RTZ_X_F_V;
    const bool int_to_float =
        op_id == isa::OperationId::VFWCVT_F_XU_V || op_id == isa::OperationId::VFWCVT_F_X_V;
    const bool unsigned_int = op_id == isa::OperationId::VFWCVT_XU_F_V ||
                              op_id == isa::OperationId::VFWCVT_RTZ_XU_F_V ||
                              op_id == isa::OperationId::VFWCVT_F_XU_V;
    const bool force_rtz =
        op_id == isa::OperationId::VFWCVT_RTZ_XU_F_V || op_id == isa::OperationId::VFWCVT_RTZ_X_F_V;
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    std::vector<NarrowRaw> sources(vl);
    std::vector<bool> active(vl);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        active[i] = vector::is_element_active(mask_reg, i, vm);
        if (active[i]) sources[i] = vector::get_group_element<NarrowRaw>(cpu.state().regs, rs2, i);
    }
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        if (float_to_int) {
            const auto value = NarrowE::value(sources[i]);
            const WideRaw result =
                unsigned_int
                    ? saturating_fp_to_int<WideRaw>(value, force_rtz, cpu.state().fcsr, flags)
                    : static_cast<WideRaw>(saturating_fp_to_int<WideSigned>(
                          value, force_rtz, cpu.state().fcsr, flags));
            vector::set_group_element<WideRaw>(cpu.state().regs, rd, i, result);
        } else {
            const auto value = int_to_float
                                   ? (unsigned_int ? static_cast<typename WideE::Value>(sources[i])
                                                   : static_cast<typename WideE::Value>(
                                                         static_cast<NarrowSigned>(sources[i])))
                                   : static_cast<typename WideE::Value>(NarrowE::value(sources[i]));
            vector::set_group_element<WideRaw>(cpu.state().regs, rd, i,
                                               fp_result_raw<WideT>(value));
        }
    }
}

template <typename NarrowT, typename WideT>
void execute_vfncvt(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs2, bool vm,
                    uint32_t vl, CSRValue& flags) {
    using NarrowE = VectorFpElement<NarrowT>;
    using WideE = VectorFpElement<WideT>;
    using NarrowRaw = typename NarrowE::Raw;
    using NarrowSigned = std::make_signed_t<NarrowRaw>;
    using WideRaw = typename WideE::Raw;
    using WideSigned = std::make_signed_t<WideRaw>;
    const bool float_to_int =
        op_id == isa::OperationId::VFNCVT_XU_F_W || op_id == isa::OperationId::VFNCVT_X_F_W ||
        op_id == isa::OperationId::VFNCVT_RTZ_XU_F_W || op_id == isa::OperationId::VFNCVT_RTZ_X_F_W;
    const bool int_to_float =
        op_id == isa::OperationId::VFNCVT_F_XU_W || op_id == isa::OperationId::VFNCVT_F_X_W;
    const bool unsigned_int = op_id == isa::OperationId::VFNCVT_XU_F_W ||
                              op_id == isa::OperationId::VFNCVT_RTZ_XU_F_W ||
                              op_id == isa::OperationId::VFNCVT_F_XU_W;
    const bool force_rtz =
        op_id == isa::OperationId::VFNCVT_RTZ_XU_F_W || op_id == isa::OperationId::VFNCVT_RTZ_X_F_W;
    const bool round_odd = op_id == isa::OperationId::VFNCVT_ROD_F_F_W;
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    std::vector<WideRaw> sources(vl);
    std::vector<bool> active(vl);
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        active[i] = vector::is_element_active(mask_reg, i, vm);
        if (active[i]) sources[i] = vector::get_group_element<WideRaw>(cpu.state().regs, rs2, i);
    }
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; ++i) {
        if (!active[i]) continue;
        if (float_to_int) {
            const auto value = WideE::value(sources[i]);
            const NarrowRaw result =
                unsigned_int
                    ? saturating_fp_to_int<NarrowRaw>(value, force_rtz, cpu.state().fcsr, flags)
                    : static_cast<NarrowRaw>(saturating_fp_to_int<NarrowSigned>(
                          value, force_rtz, cpu.state().fcsr, flags));
            vector::set_group_element<NarrowRaw>(cpu.state().regs, rd, i, result);
            continue;
        }

        typename NarrowE::Value result{};
        if (int_to_float) {
            if constexpr (std::is_same_v<NarrowT, float> && std::is_same_v<WideT, double>) {
                if (((cpu.state().fcsr >> 5U) & 0x7U) == enum_mask(isa::RoundingMode::Rmm)) {
                    result = unsigned_int
                                 ? round_rmm_integer<float>(sources[i])
                                 : round_rmm_integer<float>(static_cast<WideSigned>(sources[i]));
                } else {
                    result = unsigned_int ? static_cast<float>(sources[i])
                                          : static_cast<float>(static_cast<WideSigned>(sources[i]));
                }
            } else {
                result =
                    unsigned_int
                        ? static_cast<typename NarrowE::Value>(sources[i])
                        : static_cast<typename NarrowE::Value>(static_cast<WideSigned>(sources[i]));
            }
        } else {
            const auto value = WideE::value(sources[i]);
            if (round_odd && std::isfinite(value)) {
                const int saved_round = std::fegetround();
                std::fesetround(FE_TOWARDZERO);
                result = static_cast<typename NarrowE::Value>(value);
                std::fesetround(saved_round);
                NarrowRaw raw = fp_result_raw<NarrowT>(result);
                if (static_cast<typename WideE::Value>(result) != value) raw |= NarrowRaw{1};
                vector::set_group_element<NarrowRaw>(cpu.state().regs, rd, i, raw);
                continue;
            }
            if constexpr (std::is_same_v<NarrowT, float> && std::is_same_v<WideT, double>) {
                result = canonicalize_nan(((cpu.state().fcsr >> 5U) & 0x7U) ==
                                                  enum_mask(isa::RoundingMode::Rmm)
                                              ? round_rmm_f32(value)
                                              : static_cast<float>(value));
            } else {
                result = canonicalize_nan(static_cast<typename NarrowE::Value>(value));
            }
        }
        vector::set_group_element<NarrowRaw>(cpu.state().regs, rd, i,
                                             fp_result_raw<NarrowT>(result));
    }
}

}  // namespace

void ExecuteUnit::execute_vector_float(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1,
                                       RegId rs2, bool vm, uint32_t vl, uint32_t sew) {
    const int old_round = std::fegetround();
    std::fesetround(frm_to_host_round(cpu.state().fcsr));
    std::feclearexcept(FE_ALL_EXCEPT);
    CSRValue software_flags = 0;

    switch (op_id) {
        case isa::OperationId::VFMADD_VV:
        case isa::OperationId::VFMADD_VF:
        case isa::OperationId::VFNMADD_VV:
        case isa::OperationId::VFNMADD_VF:
        case isa::OperationId::VFMSUB_VV:
        case isa::OperationId::VFMSUB_VF:
        case isa::OperationId::VFNMSUB_VV:
        case isa::OperationId::VFNMSUB_VF:
        case isa::OperationId::VFMACC_VV:
        case isa::OperationId::VFMACC_VF:
        case isa::OperationId::VFNMACC_VV:
        case isa::OperationId::VFNMACC_VF:
        case isa::OperationId::VFMSAC_VV:
        case isa::OperationId::VFMSAC_VF:
        case isa::OperationId::VFNMSAC_VV:
        case isa::OperationId::VFNMSAC_VF: {
            const bool scalar =
                op_id == isa::OperationId::VFMADD_VF || op_id == isa::OperationId::VFNMADD_VF ||
                op_id == isa::OperationId::VFMSUB_VF || op_id == isa::OperationId::VFNMSUB_VF ||
                op_id == isa::OperationId::VFMACC_VF || op_id == isa::OperationId::VFNMACC_VF ||
                op_id == isa::OperationId::VFMSAC_VF || op_id == isa::OperationId::VFNMSAC_VF;
            dispatch_vector_fp_sew(sew, [&]<typename T>() {
                execute_vf_fma<T>(cpu, op_id, rd, rs1, rs2, scalar, vm, vl);
            });
            break;
        }
        case isa::OperationId::VFADD_VV:
            if (sew == 16)
                execute_vfadd_vv<uint16_t>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 32)
                execute_vfadd_vv<float>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 64)
                execute_vfadd_vv<double>(cpu, rd, rs1, rs2, vm, vl);
            break;
        case isa::OperationId::VFADD_VF: {
            FloatingRegister rs1_fp_val = read_vector_scalar_fp(cpu, rs1);
            if (sew == 16)
                execute_vfadd_vf<uint16_t>(cpu, rd, rs1_fp_val, rs2, vm, vl);
            else if (sew == 32)
                execute_vfadd_vf<float>(cpu, rd, rs1_fp_val, rs2, vm, vl);
            else if (sew == 64)
                execute_vfadd_vf<double>(cpu, rd, rs1_fp_val, rs2, vm, vl);
            break;
        }
        case isa::OperationId::VFSUB_VV:
            dispatch_vector_fp_sew(sew, [&]<typename T>() {
                execute_vf_binary_vv<T>(
                    cpu, rd, rs1, rs2, vm, vl, [](auto lhs, auto rhs) { return lhs - rhs; }, true);
            });
            break;
        case isa::OperationId::VFSUB_VF:
        case isa::OperationId::VFRSUB_VF: {
            const FloatingRegister scalar = read_vector_scalar_fp(cpu, rs1);
            const bool reverse = op_id == isa::OperationId::VFRSUB_VF;
            dispatch_vector_fp_sew(sew, [&]<typename T>() {
                execute_vf_binary_vf<T>(
                    cpu, rd, scalar, rs2, vm, vl,
                    [reverse](auto lhs, auto rhs) { return reverse ? rhs - lhs : lhs - rhs; }, true,
                    reverse);
            });
            break;
        }
        case isa::OperationId::VFMUL_VV:
            dispatch_vector_fp_sew(sew, [&]<typename T>() {
                execute_vf_binary_vv<T>(
                    cpu, rd, rs1, rs2, vm, vl, [](auto lhs, auto rhs) { return lhs * rhs; }, false,
                    true);
            });
            break;
        case isa::OperationId::VFMUL_VF: {
            const FloatingRegister scalar = read_vector_scalar_fp(cpu, rs1);
            dispatch_vector_fp_sew(sew, [&]<typename T>() {
                execute_vf_binary_vf<T>(
                    cpu, rd, scalar, rs2, vm, vl, [](auto lhs, auto rhs) { return lhs * rhs; },
                    false, false, true);
            });
            break;
        }
        case isa::OperationId::VFDIV_VV:
            dispatch_vector_fp_sew(sew, [&]<typename T>() {
                execute_vf_binary_vv<T>(
                    cpu, rd, rs1, rs2, vm, vl, [](auto lhs, auto rhs) { return lhs / rhs; }, false,
                    false, true);
            });
            break;
        case isa::OperationId::VFDIV_VF:
        case isa::OperationId::VFRDIV_VF: {
            const FloatingRegister scalar = read_vector_scalar_fp(cpu, rs1);
            const bool reverse = op_id == isa::OperationId::VFRDIV_VF;
            dispatch_vector_fp_sew(sew, [&]<typename T>() {
                execute_vf_binary_vf<T>(
                    cpu, rd, scalar, rs2, vm, vl,
                    [reverse](auto lhs, auto rhs) { return reverse ? rhs / lhs : lhs / rhs; },
                    false, reverse, false, true);
            });
            break;
        }
        case isa::OperationId::VFSQRT_V:
            dispatch_vector_fp_sew(sew,
                                   [&]<typename T>() { execute_vfsqrt<T>(cpu, rd, rs2, vm, vl); });
            break;
        case isa::OperationId::VFRSQRT7_V:
        case isa::OperationId::VFREC7_V:
            dispatch_vector_fp_sew(sew, [&]<typename T>() {
                execute_vf_estimate7<T>(cpu, rd, rs2, vm, vl, op_id == isa::OperationId::VFRSQRT7_V,
                                        software_flags);
            });
            break;
        case isa::OperationId::VFCLASS_V:
            dispatch_vector_fp_sew(sew,
                                   [&]<typename T>() { execute_vfclass<T>(cpu, rd, rs2, vm, vl); });
            break;
        case isa::OperationId::VFCVT_XU_F_V:
        case isa::OperationId::VFCVT_X_F_V:
        case isa::OperationId::VFCVT_F_XU_V:
        case isa::OperationId::VFCVT_F_X_V:
        case isa::OperationId::VFCVT_RTZ_XU_F_V:
        case isa::OperationId::VFCVT_RTZ_X_F_V:
            dispatch_vector_fp_sew(sew, [&]<typename T>() {
                execute_vfcvt<T>(cpu, op_id, rd, rs2, vm, vl, software_flags);
            });
            break;
        case isa::OperationId::VFWCVT_XU_F_V:
        case isa::OperationId::VFWCVT_X_F_V:
        case isa::OperationId::VFWCVT_F_XU_V:
        case isa::OperationId::VFWCVT_F_X_V:
        case isa::OperationId::VFWCVT_F_F_V:
        case isa::OperationId::VFWCVT_RTZ_XU_F_V:
        case isa::OperationId::VFWCVT_RTZ_X_F_V:
            if (sew == 16)
                execute_vfwcvt<uint16_t, float>(cpu, op_id, rd, rs2, vm, vl, software_flags);
            else if (sew == 32)
                execute_vfwcvt<float, double>(cpu, op_id, rd, rs2, vm, vl, software_flags);
            break;
        case isa::OperationId::VFNCVT_XU_F_W:
        case isa::OperationId::VFNCVT_X_F_W:
        case isa::OperationId::VFNCVT_F_XU_W:
        case isa::OperationId::VFNCVT_F_X_W:
        case isa::OperationId::VFNCVT_F_F_W:
        case isa::OperationId::VFNCVT_ROD_F_F_W:
        case isa::OperationId::VFNCVT_RTZ_XU_F_W:
        case isa::OperationId::VFNCVT_RTZ_X_F_W:
            if (sew == 16)
                execute_vfncvt<uint16_t, float>(cpu, op_id, rd, rs2, vm, vl, software_flags);
            else if (sew == 32)
                execute_vfncvt<float, double>(cpu, op_id, rd, rs2, vm, vl, software_flags);
            break;
        case isa::OperationId::VFSGNJ_VV:
        case isa::OperationId::VFSGNJ_VF:
        case isa::OperationId::VFSGNJN_VV:
        case isa::OperationId::VFSGNJN_VF:
        case isa::OperationId::VFSGNJX_VV:
        case isa::OperationId::VFSGNJX_VF: {
            const bool scalar = op_id == isa::OperationId::VFSGNJ_VF ||
                                op_id == isa::OperationId::VFSGNJN_VF ||
                                op_id == isa::OperationId::VFSGNJX_VF;
            dispatch_vector_fp_sew(sew, [&]<typename T>() {
                execute_vfsgnj<T>(cpu, op_id, rd, rs1, rs2, scalar, vm, vl);
            });
            break;
        }
        case isa::OperationId::VFMIN_VV:
        case isa::OperationId::VFMIN_VF:
        case isa::OperationId::VFMAX_VV:
        case isa::OperationId::VFMAX_VF: {
            const bool scalar =
                op_id == isa::OperationId::VFMIN_VF || op_id == isa::OperationId::VFMAX_VF;
            const bool maximum =
                op_id == isa::OperationId::VFMAX_VV || op_id == isa::OperationId::VFMAX_VF;
            dispatch_vector_fp_sew(sew, [&]<typename T>() {
                execute_vfminmax<T>(cpu, rd, rs1, rs2, scalar, vm, vl, maximum);
            });
            break;
        }
        case isa::OperationId::VFREDUSUM_VS:
        case isa::OperationId::VFREDOSUM_VS:
        case isa::OperationId::VFREDMIN_VS:
        case isa::OperationId::VFREDMAX_VS:
            dispatch_vector_fp_sew(
                sew, [&]<typename T>() { execute_vfred<T>(cpu, op_id, rd, rs1, rs2, vm, vl); });
            break;
        case isa::OperationId::VFWREDUSUM_VS:
        case isa::OperationId::VFWREDOSUM_VS:
            if (sew == 16)
                execute_vfwred<uint16_t, float>(cpu, rd, rs1, rs2, vm, vl);
            else if (sew == 32)
                execute_vfwred<float, double>(cpu, rd, rs1, rs2, vm, vl);
            break;
        case isa::OperationId::VFWMACC_VV:
        case isa::OperationId::VFWMACC_VF:
        case isa::OperationId::VFWNMACC_VV:
        case isa::OperationId::VFWNMACC_VF:
        case isa::OperationId::VFWMSAC_VV:
        case isa::OperationId::VFWMSAC_VF:
        case isa::OperationId::VFWNMSAC_VV:
        case isa::OperationId::VFWNMSAC_VF: {
            const bool scalar =
                op_id == isa::OperationId::VFWMACC_VF || op_id == isa::OperationId::VFWNMACC_VF ||
                op_id == isa::OperationId::VFWMSAC_VF || op_id == isa::OperationId::VFWNMSAC_VF;
            if (sew == 16)
                execute_vfw_fma<uint16_t, float>(cpu, op_id, rd, rs1, rs2, scalar, vm, vl);
            else if (sew == 32)
                execute_vfw_fma<float, double>(cpu, op_id, rd, rs1, rs2, scalar, vm, vl);
            break;
        }
        case isa::OperationId::VFWADD_VV:
        case isa::OperationId::VFWADD_VF:
        case isa::OperationId::VFWSUB_VV:
        case isa::OperationId::VFWSUB_VF:
        case isa::OperationId::VFWADD_WV:
        case isa::OperationId::VFWADD_WF:
        case isa::OperationId::VFWSUB_WV:
        case isa::OperationId::VFWSUB_WF:
        case isa::OperationId::VFWMUL_VV:
        case isa::OperationId::VFWMUL_VF: {
            const bool scalar =
                op_id == isa::OperationId::VFWADD_VF || op_id == isa::OperationId::VFWSUB_VF ||
                op_id == isa::OperationId::VFWADD_WF || op_id == isa::OperationId::VFWSUB_WF ||
                op_id == isa::OperationId::VFWMUL_VF;
            const bool wide_lhs =
                op_id == isa::OperationId::VFWADD_WV || op_id == isa::OperationId::VFWADD_WF ||
                op_id == isa::OperationId::VFWSUB_WV || op_id == isa::OperationId::VFWSUB_WF;
            if (sew == 16)
                execute_vfw_binary<uint16_t, float>(cpu, op_id, rd, rs1, rs2, scalar, wide_lhs, vm,
                                                    vl);
            else if (sew == 32)
                execute_vfw_binary<float, double>(cpu, op_id, rd, rs1, rs2, scalar, wide_lhs, vm,
                                                  vl);
            break;
        }
        case isa::OperationId::VMFEQ_VV:
        case isa::OperationId::VMFEQ_VF:
        case isa::OperationId::VMFNE_VV:
        case isa::OperationId::VMFNE_VF:
        case isa::OperationId::VMFLT_VV:
        case isa::OperationId::VMFLT_VF:
        case isa::OperationId::VMFLE_VV:
        case isa::OperationId::VMFLE_VF:
        case isa::OperationId::VMFGT_VF:
        case isa::OperationId::VMFGE_VF: {
            const bool scalar =
                op_id == isa::OperationId::VMFEQ_VF || op_id == isa::OperationId::VMFNE_VF ||
                op_id == isa::OperationId::VMFLT_VF || op_id == isa::OperationId::VMFLE_VF ||
                op_id == isa::OperationId::VMFGT_VF || op_id == isa::OperationId::VMFGE_VF;
            const auto kind = compare_kind(op_id);
            dispatch_vector_fp_sew(sew, [&]<typename T>() {
                execute_vmf_compare<T>(cpu, rd, rs1, rs2, scalar, vm, vl, kind);
            });
            break;
        }
        default:
            break;
    }

    cpu.state().fcsr |=
        software_flags | host_exceptions_to_fflags(std::fetestexcept(FE_ALL_EXCEPT));
    std::fesetround(old_round);
}

}  // namespace simrv::execute
