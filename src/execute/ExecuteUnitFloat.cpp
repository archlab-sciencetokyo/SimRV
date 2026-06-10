/**
 *@file ExecuteUnitFloat.cpp* @brief SimRV floating - point execution unit implementation.
 */
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>

#include "simrv/Define.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Math.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::execute {

namespace fp {

// ============================================================================
// Bit Manipulation Helpers
// ============================================================================

constexpr auto f32_bits(float v) -> uint32_t { return std::bit_cast<uint32_t>(v); }
constexpr auto f32_from_bits(uint32_t v) -> float { return std::bit_cast<float>(v); }
constexpr auto f64_bits(double v) -> uint64_t { return std::bit_cast<uint64_t>(v); }
constexpr auto f64_from_bits(uint64_t v) -> double { return std::bit_cast<double>(v); }

// ============================================================================
// Register Access Helpers
// ============================================================================

constexpr auto read_f32(const FloatingRegister* freg, Word idx) -> float {
    const FloatingRegister val = freg[idx];
    if ((val & simrv::xlen::kF32BoxerBits) != simrv::xlen::kF32BoxerBits) {
        return f32_from_bits(0x7fc00000U);
    }
    return f32_from_bits(
        static_cast<uint32_t>(val & static_cast<FloatingRegister>(simrv::xlen::kLower32Mask)));
}

constexpr auto read_f64(const FloatingRegister* freg, Word idx) -> double {
    return f64_from_bits(freg[idx]);
}

constexpr auto write_f32_boxed(float v) -> FloatingRegister {
    return static_cast<FloatingRegister>(simrv::xlen::kF32BoxerBits) |
           static_cast<FloatingRegister>(f32_bits(v));
}

// ============================================================================
// Classification Functions
// ============================================================================

constexpr auto fclass32(float v) -> uint32_t {
    const uint32_t bits = f32_bits(v);
    const bool sign = (bits >> simrv::xlen::kF32SignBit) != 0;
    const uint32_t exp = (bits >> simrv::xlen::kF32ExpShift) & simrv::xlen::kF32ExpMask;
    const uint32_t frac = bits & simrv::xlen::kF32FracMask;

    if (exp == simrv::xlen::kF32ExpMask) {
        if (frac == 0) {
            return sign ? (1U << 0) : (1U << 7);
        }
        const bool qnan = (frac & (1U << simrv::xlen::kF32FracQnanBit)) != 0;
        return qnan ? (1U << 9) : (1U << 8);
    }
    if (exp == 0) {
        if (frac == 0) {
            return sign ? (1U << 3) : (1U << 4);
        }
        return sign ? (1U << 2) : (1U << 5);
    }
    return sign ? (1U << 1) : (1U << 6);
}

constexpr auto fclass64(double v) -> uint32_t {
    const uint64_t bits = f64_bits(v);
    const bool sign = (bits >> simrv::xlen::kF64SignBit) != 0;
    const uint64_t exp = (bits >> simrv::xlen::kF64ExpShift) & simrv::xlen::kF64ExpMask;
    const uint64_t frac = bits & simrv::xlen::kF64FracMask;

    if (exp == simrv::xlen::kF64ExpMask) {
        if (frac == 0) {
            return sign ? (1U << 0) : (1U << 7);
        }
        const bool qnan = (frac & (1ULL << simrv::xlen::kF64FracQnanBit)) != 0;
        return qnan ? (1U << 9) : (1U << 8);
    }
    if (exp == 0) {
        if (frac == 0) {
            return sign ? (1U << 3) : (1U << 4);
        }
        return sign ? (1U << 2) : (1U << 5);
    }
    return sign ? (1U << 1) : (1U << 6);
}

// ============================================================================
// Signaling NaN Detection
// ============================================================================

constexpr auto is_snan32(float v) -> bool {
    const uint32_t bits = f32_bits(v);
    const uint32_t exp = (bits >> simrv::xlen::kF32ExpShift) & simrv::xlen::kF32ExpMask;
    const uint32_t frac = bits & simrv::xlen::kF32FracMask;
    return (exp == simrv::xlen::kF32ExpMask) && (frac != 0) &&
           ((frac & (1U << simrv::xlen::kF32FracQnanBit)) == 0);
}

constexpr auto is_snan64(double v) -> bool {
    const uint64_t bits = f64_bits(v);
    const uint64_t exp = (bits >> simrv::xlen::kF64ExpShift) & simrv::xlen::kF64ExpMask;
    const uint64_t frac = bits & simrv::xlen::kF64FracMask;
    return (exp == simrv::xlen::kF64ExpMask) && (frac != 0) &&
           ((frac & (1ULL << simrv::xlen::kF64FracQnanBit)) == 0);
}

// ============================================================================
// Min/Max Functions
// ============================================================================

static auto fmin32_riscv(float a, float b) -> FloatingRegister {
    const bool a_nan = std::isnan(a);
    const bool b_nan = std::isnan(b);
    if (is_snan32(a) || is_snan32(b)) {
        std::feraiseexcept(FE_INVALID);
    }
    if (a_nan && b_nan) {
        return write_f32_boxed(f32_from_bits(simrv::xlen::kF32Qnan));
    }
    if (a_nan) {
        return write_f32_boxed(b);
    }
    if (b_nan) {
        return write_f32_boxed(a);
    }
    if (a == 0.0F && b == 0.0F) {
        const bool neg = std::signbit(a) || std::signbit(b);
        return write_f32_boxed(neg ? -0.0F : 0.0F);
    }
    return write_f32_boxed(std::fmin(a, b));
}

static auto fmax32_riscv(float a, float b) -> FloatingRegister {
    const bool a_nan = std::isnan(a);
    const bool b_nan = std::isnan(b);
    if (is_snan32(a) || is_snan32(b)) {
        std::feraiseexcept(FE_INVALID);
    }
    if (a_nan && b_nan) {
        return write_f32_boxed(f32_from_bits(simrv::xlen::kF32Qnan));
    }
    if (a_nan) {
        return write_f32_boxed(b);
    }
    if (b_nan) {
        return write_f32_boxed(a);
    }
    if (a == 0.0F && b == 0.0F) {
        const bool pos = !std::signbit(a) || !std::signbit(b);
        return write_f32_boxed(pos ? 0.0F : -0.0F);
    }
    return write_f32_boxed(std::fmax(a, b));
}

static auto fmin64_riscv(double a, double b) -> FloatingRegister {
    const bool a_nan = std::isnan(a);
    const bool b_nan = std::isnan(b);
    if (is_snan64(a) || is_snan64(b)) {
        std::feraiseexcept(FE_INVALID);
    }
    if (a_nan && b_nan) {
        return static_cast<FloatingRegister>(simrv::xlen::kF64Qnan);
    }
    if (a_nan) {
        return f64_bits(b);
    }
    if (b_nan) {
        return f64_bits(a);
    }
    if (a == 0.0 && b == 0.0) {
        const bool neg = std::signbit(a) || std::signbit(b);
        return f64_bits(neg ? -0.0 : 0.0);
    }
    return f64_bits(std::fmin(a, b));
}

static auto fmax64_riscv(double a, double b) -> FloatingRegister {
    const bool a_nan = std::isnan(a);
    const bool b_nan = std::isnan(b);
    if (is_snan64(a) || is_snan64(b)) {
        std::feraiseexcept(FE_INVALID);
    }
    if (a_nan && b_nan) {
        return static_cast<FloatingRegister>(simrv::xlen::kF64Qnan);
    }
    if (a_nan) {
        return f64_bits(b);
    }
    if (b_nan) {
        return f64_bits(a);
    }
    if (a == 0.0 && b == 0.0) {
        const bool pos = !std::signbit(a) || !std::signbit(b);
        return f64_bits(pos ? 0.0 : -0.0);
    }
    return f64_bits(std::fmax(a, b));
}

// ============================================================================
// Exception Handling
// ============================================================================

static auto host_except_to_fflags(int ex) -> uint32_t {
    uint32_t flags = 0;
    if ((ex & FE_INEXACT) != 0) {
        flags |= enum_mask(FflagsBit::Nx);
    }
    if ((ex & FE_UNDERFLOW) != 0) {
        flags |= enum_mask(FflagsBit::Uf);
    }
    if ((ex & FE_OVERFLOW) != 0) {
        flags |= enum_mask(FflagsBit::Of);
    }
    if ((ex & FE_DIVBYZERO) != 0) {
        flags |= enum_mask(FflagsBit::Dz);
    }
    if ((ex & FE_INVALID) != 0) {
        flags |= enum_mask(FflagsBit::Nv);
    }
    return flags;
}

static void accumulate_fp_flags(CSRValue& fcsr) {
    const uint32_t old_flags = fcsr & kFflagsMask;
    const uint32_t new_flags = host_except_to_fflags(std::fetestexcept(FE_ALL_EXCEPT));
    fcsr = (fcsr & ~static_cast<CSRValue>(kFflagsMask)) | ((old_flags | new_flags) & kFflagsMask);
}

// ============================================================================
// Rounding Mode Management
// ============================================================================

static auto rm_to_fe_round(Word rm, CSRValue fcsr) -> int {
    Word effective_rm = rm;
    if (effective_rm == enum_mask(RoundingMode::Dyn)) {
        effective_rm = (fcsr >> 5) & 0x7;
    }

    switch (effective_rm) {
        case enum_mask(RoundingMode::Rne):
            return FE_TONEAREST;
        case enum_mask(RoundingMode::Rtz):
            return FE_TOWARDZERO;
        case enum_mask(RoundingMode::Rdn):
            return FE_DOWNWARD;
        case enum_mask(RoundingMode::Rup):
            return FE_UPWARD;
        default:
            return FE_TONEAREST;  // Host fallback for unsupported modes (including RMM).
    }
}

class ScopedRoundingMode {
   public:
    ScopedRoundingMode(Word rm, CSRValue fcsr) : old_mode_(std::fegetround()) {
        std::fesetround(rm_to_fe_round(rm, fcsr));
    }
    ~ScopedRoundingMode() { std::fesetround(old_mode_); }
    ScopedRoundingMode(const ScopedRoundingMode&) = delete;
    auto operator=(const ScopedRoundingMode&) -> ScopedRoundingMode& = delete;
    ScopedRoundingMode(ScopedRoundingMode&&) = delete;
    auto operator=(ScopedRoundingMode&&) -> ScopedRoundingMode& = delete;

   private:
    int old_mode_ = FE_TONEAREST;
};

// ============================================================================
// Floating-Point to Integer Conversion
// ============================================================================

static auto fcvt_to_int(double value, Word op_mode) -> Register {
    const bool is_unsigned = (op_mode & 1) != 0;
    const bool is_64 = (op_mode >= 2);

    auto saturate = [&](bool nan, bool pos_inf) -> Register {
        std::feraiseexcept(FE_INVALID);
        if (nan || pos_inf) {
            if (is_64) {
                return is_unsigned ? static_cast<Register>(std::numeric_limits<uint64_t>::max())
                                   : static_cast<Register>(std::numeric_limits<int64_t>::max());
            } else {
                const uint32_t max_val =
                    is_unsigned ? std::numeric_limits<uint32_t>::max()
                                : static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
                return static_cast<Register>(
                    static_cast<SignedWord>(static_cast<int32_t>(max_val)));
            }
        } else {
            if (is_64) {
                return is_unsigned ? 0ULL
                                   : static_cast<Register>(std::numeric_limits<int64_t>::min());
            } else {
                const uint32_t min_val =
                    is_unsigned ? 0U : static_cast<uint32_t>(std::numeric_limits<int32_t>::min());
                return static_cast<Register>(
                    static_cast<SignedWord>(static_cast<int32_t>(min_val)));
            }
        }
    };

    if (std::isnan(value)) {
        return saturate(true, false);
    }

    if (std::isinf(value)) {
        return saturate(false, value > 0);
    }

    double rounded = std::rint(value);
    int const rmode = std::fegetround();

    if (rmode == FE_TOWARDZERO) {
        rounded = std::trunc(value);
    } else if (rmode == FE_DOWNWARD) {
        rounded = std::floor(value);
    } else if (rmode == FE_UPWARD) {
        rounded = std::ceil(value);
    }

    if (rounded != value) {
        std::feraiseexcept(FE_INEXACT);
    }

    if (is_64) {
        if (is_unsigned) {
            if (rounded < 0.0) {
                return saturate(false, false);
            }
            if (rounded >= 18446744073709551616.0) {
                return saturate(false, true);
            }
            return static_cast<Register>(static_cast<uint64_t>(rounded));
        } else {
            if (rounded < -9223372036854775808.0) {
                return saturate(false, false);
            }
            if (rounded >= 9223372036854775808.0) {
                return saturate(false, true);
            }
            return static_cast<Register>(static_cast<int64_t>(rounded));
        }
    } else {
        if (is_unsigned) {
            if (rounded < 0.0) {
                return saturate(false, false);
            }
            if (rounded >= 4294967296.0) {
                return saturate(false, true);
            }
            return static_cast<Register>(
                static_cast<SignedWord>(static_cast<int32_t>(static_cast<uint32_t>(rounded))));
        } else {
            if (rounded < -2147483648.0) {
                return saturate(false, false);
            }
            if (rounded >= 2147483648.0) {
                return saturate(false, true);
            }
            return static_cast<Register>(static_cast<SignedWord>(static_cast<int32_t>(rounded)));
        }
    }
}

}  // namespace fp

// ============================================================================
// Public ExecuteUnit Methods
// ============================================================================

using namespace fp;

auto ExecuteUnit::fusedFp(Opcode opcode, Word fmt, Word rs1, Word rs2, Word rs3, Word rm,
                          const FloatingRegister* freg, CSRValue& fcsr) -> FpExecResult {
    FpExecResult out;
    std::feclearexcept(FE_ALL_EXCEPT);
    {
        ScopedRoundingMode const scope(rm, fcsr);
        if (fmt == 0) {
            const float a = read_f32(freg, rs1);
            const float b = read_f32(freg, rs2);
            const float c = read_f32(freg, rs3);
            float value = 0.0F;
            if (opcode == Opcode::MAdd) {
                value = std::fma(a, b, c);
            }
            if (opcode == Opcode::MSub) {
                value = std::fma(a, b, -c);
            }
            if (opcode == Opcode::NMAdd) {
                value = -std::fma(a, b, c);
            }
            if (opcode == Opcode::NMSub) {
                value = -std::fma(a, b, -c);
            }
            if (std::isnan(value)) {
                value = f32_from_bits(simrv::xlen::kF32Qnan);
            }
            out.fp_wb_data = write_f32_boxed(value);
            out.fp_wb_enable = true;
        } else if (fmt == 1) {
            const double a = read_f64(freg, rs1);
            const double b = read_f64(freg, rs2);
            const double c = read_f64(freg, rs3);
            double value = 0.0;
            if (opcode == Opcode::MAdd) {
                value = std::fma(a, b, c);
            }
            if (opcode == Opcode::MSub) {
                value = std::fma(a, b, -c);
            }
            if (opcode == Opcode::NMAdd) {
                value = -std::fma(a, b, c);
            }
            if (opcode == Opcode::NMSub) {
                value = -std::fma(a, b, -c);
            }
            if (std::isnan(value)) {
                value = f64_from_bits(simrv::xlen::kF64Qnan);
            }
            out.fp_wb_data = f64_bits(value);
            out.fp_wb_enable = true;
        }
    }
    accumulate_fp_flags(fcsr);
    return out;
}

auto ExecuteUnit::opFp(Word funct7, Funct3 funct3, Word rs1, Word rs2, Register rrs1,
                       const FloatingRegister* freg, CSRValue& fcsr) -> FpExecResult {
    FpExecResult out;
    const Word rm = enum_mask(funct3);

    if (funct7 == enum_mask(Funct7Fp::FaddS)) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            float result = read_f32(freg, rs1) + read_f32(freg, rs2);
            if (std::isnan(result)) {
                result = f32_from_bits(simrv::xlen::kF32Qnan);
            }
            out.fp_wb_data = write_f32_boxed(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FaddD)) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            double result = read_f64(freg, rs1) + read_f64(freg, rs2);
            if (std::isnan(result)) {
                result = f64_from_bits(simrv::xlen::kF64Qnan);
            }
            out.fp_wb_data = f64_bits(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FsubS)) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            float result = read_f32(freg, rs1) - read_f32(freg, rs2);
            if (std::isnan(result)) {
                result = f32_from_bits(simrv::xlen::kF32Qnan);
            }
            out.fp_wb_data = write_f32_boxed(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FsubD)) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            double result = read_f64(freg, rs1) - read_f64(freg, rs2);
            if (std::isnan(result)) {
                result = f64_from_bits(simrv::xlen::kF64Qnan);
            }
            out.fp_wb_data = f64_bits(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FmulS)) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            float result = read_f32(freg, rs1) * read_f32(freg, rs2);
            if (std::isnan(result)) {
                result = f32_from_bits(simrv::xlen::kF32Qnan);
            }
            out.fp_wb_data = write_f32_boxed(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FmulD)) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            double result = read_f64(freg, rs1) * read_f64(freg, rs2);
            if (std::isnan(result)) {
                result = f64_from_bits(simrv::xlen::kF64Qnan);
            }
            out.fp_wb_data = f64_bits(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FdivS)) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            float result = read_f32(freg, rs1) / read_f32(freg, rs2);
            if (std::isnan(result)) {
                result = f32_from_bits(simrv::xlen::kF32Qnan);
            }
            out.fp_wb_data = write_f32_boxed(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FdivD)) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            double result = read_f64(freg, rs1) / read_f64(freg, rs2);
            if (std::isnan(result)) {
                result = f64_from_bits(simrv::xlen::kF64Qnan);
            }
            out.fp_wb_data = f64_bits(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FsqrtS)) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            float result = std::sqrt(read_f32(freg, rs1));
            if (std::isnan(result)) {
                result = f32_from_bits(simrv::xlen::kF32Qnan);
            }
            out.fp_wb_data = write_f32_boxed(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FsqrtD)) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            double result = std::sqrt(read_f64(freg, rs1));
            if (std::isnan(result)) {
                result = f64_from_bits(simrv::xlen::kF64Qnan);
            }
            out.fp_wb_data = f64_bits(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FsgnjS) || funct7 == enum_mask(Funct7Fp::FsgnjD)) {
        const bool is_d = (funct7 == enum_mask(Funct7Fp::FsgnjD));
        FloatingRegister a = freg[rs1];
        FloatingRegister b = freg[rs2];
        if (!is_d) {
            if ((a & simrv::xlen::kF32BoxerBits) != simrv::xlen::kF32BoxerBits) {
                a = static_cast<FloatingRegister>(simrv::xlen::kF32BoxerBits) | 0x7fc00000U;
            }
            if ((b & simrv::xlen::kF32BoxerBits) != simrv::xlen::kF32BoxerBits) {
                b = static_cast<FloatingRegister>(simrv::xlen::kF32BoxerBits) | 0x7fc00000U;
            }
        }
        FloatingRegister value = 0;
        const uint64_t sign_bit = is_d ? (1ULL << 63U) : (1ULL << 31U);
        if (enum_mask(funct3) == 0) {
            value = (a & (~sign_bit)) | (b & sign_bit);
        }
        if (enum_mask(funct3) == 1) {
            value = (a & (~sign_bit)) | ((~b) & sign_bit);
        }
        if (enum_mask(funct3) == 2) {
            value = a ^ (b & sign_bit);
        }
        out.fp_wb_data = is_d ? value
                              : (static_cast<FloatingRegister>(simrv::xlen::kF32BoxerBits) |
                                 (value & static_cast<FloatingRegister>(kLower32Mask)));
        out.fp_wb_enable = true;
    } else if (funct7 == enum_mask(Funct7Fp::FminmaxS)) {
        std::feclearexcept(FE_ALL_EXCEPT);
        const float a = read_f32(freg, rs1);
        const float b = read_f32(freg, rs2);
        out.fp_wb_data =
            (enum_mask(funct3) == enum_mask(Funct3Fp::Max)) ? fmax32_riscv(a, b) : fmin32_riscv(a, b);
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FminmaxD)) {
        std::feclearexcept(FE_ALL_EXCEPT);
        const double a = read_f64(freg, rs1);
        const double b = read_f64(freg, rs2);
        out.fp_wb_data =
            (enum_mask(funct3) == enum_mask(Funct3Fp::Max)) ? fmax64_riscv(a, b) : fmin64_riscv(a, b);
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FcvtSD)) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            const double src = read_f64(freg, rs1);
            if (std::isnan(src)) {
                out.fp_wb_data = write_f32_boxed(f32_from_bits(simrv::xlen::kF32Qnan));
            } else {
                out.fp_wb_data = write_f32_boxed(static_cast<float>(src));
            }
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FcvtDS)) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            const float src = read_f32(freg, rs1);
            if (std::isnan(src)) {
                out.fp_wb_data = static_cast<FloatingRegister>(simrv::xlen::kF64Qnan);
            } else {
                out.fp_wb_data = f64_bits(static_cast<double>(src));
            }
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FcmpS) || funct7 == enum_mask(Funct7Fp::FcmpD)) {
        const bool is_d = (funct7 == enum_mask(Funct7Fp::FcmpD));
        std::feclearexcept(FE_ALL_EXCEPT);
        if (!is_d) {
            const float a = read_f32(freg, rs1);
            const float b = read_f32(freg, rs2);
            if (std::isnan(a) || std::isnan(b)) {
                if (enum_mask(funct3) != enum_mask(Funct3Fp::Eq) || is_snan32(a) || is_snan32(b)) {
                    std::feraiseexcept(FE_INVALID);
                }
                out.int_wb_data = 0;
            } else {
                if (enum_mask(funct3) == enum_mask(Funct3Fp::Eq)) {
                    out.int_wb_data = static_cast<Register>(a == b);
                }
                if (enum_mask(funct3) == enum_mask(Funct3Fp::Lt)) {
                    out.int_wb_data = static_cast<Register>(a < b);
                }
                if (enum_mask(funct3) == enum_mask(Funct3Fp::Leq)) {
                    out.int_wb_data = static_cast<Register>(a <= b);
                }
            }
        } else {
            const double a = read_f64(freg, rs1);
            const double b = read_f64(freg, rs2);
            if (std::isnan(a) || std::isnan(b)) {
                if (enum_mask(funct3) != enum_mask(Funct3Fp::Eq) || is_snan64(a) || is_snan64(b)) {
                    std::feraiseexcept(FE_INVALID);
                }
                out.int_wb_data = 0;
            } else {
                if (enum_mask(funct3) == enum_mask(Funct3Fp::Eq)) {
                    out.int_wb_data = static_cast<Register>(a == b);
                }
                if (enum_mask(funct3) == enum_mask(Funct3Fp::Lt)) {
                    out.int_wb_data = static_cast<Register>(a < b);
                }
                if (enum_mask(funct3) == enum_mask(Funct3Fp::Leq)) {
                    out.int_wb_data = static_cast<Register>(a <= b);
                }
            }
        }
        out.int_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FcvtWS) || funct7 == enum_mask(Funct7Fp::FcvtWD)) {
        const bool is_d = (funct7 == enum_mask(Funct7Fp::FcvtWD));
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            const double a = !is_d ? static_cast<double>(read_f32(freg, rs1)) : read_f64(freg, rs1);
            out.int_wb_data = fcvt_to_int(a, rs2);
        }
        out.int_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FcvtSW) || funct7 == enum_mask(Funct7Fp::FcvtDW)) {
        const bool is_d = (funct7 == enum_mask(Funct7Fp::FcvtDW));
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            double val = 0;
            if (rs2 == 0) {
                val = static_cast<double>(static_cast<int32_t>(rrs1));
            } else if (rs2 == 1) {
                val = static_cast<double>(static_cast<uint32_t>(rrs1));
            } else if (rs2 == 2) {
                val = static_cast<double>(static_cast<int64_t>(rrs1));
            } else if (rs2 == 3) {
                val = static_cast<double>(static_cast<uint64_t>(rrs1));
            }

            if (is_d) {
                out.fp_wb_data = f64_bits(val);
            } else {
                out.fp_wb_data = write_f32_boxed(static_cast<float>(val));
            }
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == enum_mask(Funct7Fp::FmvXW) || funct7 == enum_mask(Funct7Fp::FmvXD)) {
        const bool is_d = (funct7 == enum_mask(Funct7Fp::FmvXD));
        if (enum_mask(funct3) == enum_mask(Funct3Fp::FmvXW)) {
            if (is_d) {
                out.int_wb_data = static_cast<Register>(freg[rs1]);
            } else {
                out.int_wb_data =
                    static_cast<Register>(static_cast<SignedWord>(static_cast<int32_t>(freg[rs1])));
            }
            out.int_wb_enable = true;
        } else if (enum_mask(funct3) == enum_mask(Funct3Fp::Fclass)) {
            out.int_wb_data = is_d ? fclass64(read_f64(freg, rs1)) : fclass32(read_f32(freg, rs1));
            out.int_wb_enable = true;
        }
    } else if (funct7 == enum_mask(Funct7Fp::FmvWX) || funct7 == enum_mask(Funct7Fp::FmvDX)) {
        const bool is_d = (funct7 == enum_mask(Funct7Fp::FmvDX));
        if (is_d) {
            out.fp_wb_data = static_cast<FloatingRegister>(rrs1);
        } else {
            out.fp_wb_data =
                static_cast<FloatingRegister>(simrv::xlen::kF32BoxerBits) |
                static_cast<FloatingRegister>(rrs1 & static_cast<Register>(kLower32Mask));
        }
        out.fp_wb_enable = true;
    }
    return out;
}
}  // namespace simrv::execute
