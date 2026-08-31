/**
 * @file ExecuteUnitFloat.cpp
 * @brief High-performance RISC-V floating-point execution unit implementation.
 */
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>

#include "simrv/Define.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace simrv::execute {

using simrv::core::kFflagsMask;
using simrv::isa::FflagsBit;
using simrv::isa::Funct3;
using simrv::isa::Funct3Fp;
using simrv::isa::Funct7Fp;
using simrv::isa::Opcode;
using simrv::isa::RoundingMode;

namespace fp {

// ============================================================================
// Bit Manipulation & NaN Helpers
// ============================================================================

constexpr auto f32_bits(float v) -> uint32_t { return std::bit_cast<uint32_t>(v); }
constexpr auto f32_from_bits(uint32_t v) -> float { return std::bit_cast<float>(v); }
constexpr auto f64_bits(double v) -> uint64_t { return std::bit_cast<uint64_t>(v); }
constexpr auto f64_from_bits(uint64_t v) -> double { return std::bit_cast<double>(v); }

// ============================================================================
// Register Access Helpers (Branchless NaN-boxing)
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
// FP Traits & Generic Helpers
// ============================================================================

template <typename FloatT>
struct FpTraits;

template <>
struct FpTraits<float> {
    using Bits = uint32_t;
    static constexpr unsigned kSignBit = simrv::xlen::kF32SignBit;
    static constexpr unsigned kExpShift = simrv::xlen::kF32ExpShift;
    static constexpr Bits kExpMask = simrv::xlen::kF32ExpMask;
    static constexpr Bits kFracMask = simrv::xlen::kF32FracMask;
    static constexpr unsigned kFracQnanBit = simrv::xlen::kF32FracQnanBit;
    static constexpr Bits kQnanBits = simrv::xlen::kF32Qnan;
    static constexpr auto from_bits(Bits b) -> float { return f32_from_bits(b); }
    static constexpr auto to_bits(float v) -> Bits { return f32_bits(v); }
    static constexpr auto to_boxed(float v) -> FloatingRegister { return write_f32_boxed(v); }
    static constexpr auto read(const FloatingRegister* freg, Word idx) -> float {
        return read_f32(freg, idx);
    }
};

template <>
struct FpTraits<double> {
    using Bits = uint64_t;
    static constexpr unsigned kSignBit = simrv::xlen::kF64SignBit;
    static constexpr unsigned kExpShift = simrv::xlen::kF64ExpShift;
    static constexpr Bits kExpMask = simrv::xlen::kF64ExpMask;
    static constexpr Bits kFracMask = simrv::xlen::kF64FracMask;
    static constexpr unsigned kFracQnanBit = simrv::xlen::kF64FracQnanBit;
    static constexpr Bits kQnanBits = simrv::xlen::kF64Qnan;
    static constexpr auto from_bits(Bits b) -> double { return f64_from_bits(b); }
    static constexpr auto to_bits(double v) -> Bits { return f64_bits(v); }
    static constexpr auto to_boxed(double v) -> FloatingRegister { return f64_bits(v); }
    static constexpr auto read(const FloatingRegister* freg, Word idx) -> double {
        return read_f64(freg, idx);
    }
};

// ============================================================================
// Classification Functions
// ============================================================================

template <typename FloatT>
constexpr auto fclass(FloatT v) -> uint32_t {
    using T = FpTraits<FloatT>;
    const typename T::Bits bits = T::to_bits(v);
    const bool sign = (bits >> T::kSignBit) != 0;
    const typename T::Bits exp = (bits >> T::kExpShift) & T::kExpMask;
    const typename T::Bits frac = bits & T::kFracMask;

    if (exp == T::kExpMask) {
        if (frac == 0) return sign ? (1U << 0) : (1U << 7);
        const bool qnan = (frac & (typename T::Bits{1} << T::kFracQnanBit)) != 0;
        return qnan ? (1U << 9) : (1U << 8);
    }
    if (exp == 0) {
        if (frac == 0) return sign ? (1U << 3) : (1U << 4);
        return sign ? (1U << 2) : (1U << 5);
    }
    return sign ? (1U << 1) : (1U << 6);
}

constexpr auto fclass32(float v) -> uint32_t { return fclass(v); }
constexpr auto fclass64(double v) -> uint32_t { return fclass(v); }

// ============================================================================
// Signaling NaN Detection
// ============================================================================

template <typename FloatT>
constexpr auto is_snan(FloatT v) -> bool {
    using T = FpTraits<FloatT>;
    const typename T::Bits bits = T::to_bits(v);
    const typename T::Bits exp = (bits >> T::kExpShift) & T::kExpMask;
    const typename T::Bits frac = bits & T::kFracMask;
    return (exp == T::kExpMask) && (frac != 0) &&
           ((frac & (typename T::Bits{1} << T::kFracQnanBit)) == 0);
}

constexpr auto is_snan32(float v) -> bool { return is_snan(v); }
constexpr auto is_snan64(double v) -> bool { return is_snan(v); }

// ============================================================================
// Exception & Rounding Mode Management (Lazy MXCSR State Machine)
// ============================================================================

#if defined(__x86_64__) || defined(_M_X64)
class FpEnvironment {
   public:
    static inline void set_rounding_mode(Word rm, CSRValue fcsr) noexcept {
        Word effective_rm = rm;
        if (effective_rm == enum_mask(RoundingMode::Dyn)) {
            effective_rm = (fcsr >> 5) & 0x7;
        }

        uint32_t mxcsr_rm = 0;
        switch (effective_rm) {
            case enum_mask(RoundingMode::Rne):
                mxcsr_rm = 0x0000;
                break;
            case enum_mask(RoundingMode::Rdn):
                mxcsr_rm = 0x2000;
                break;
            case enum_mask(RoundingMode::Rup):
                mxcsr_rm = 0x4000;
                break;
            case enum_mask(RoundingMode::Rtz):
                mxcsr_rm = 0x6000;
                break;
            default:
                mxcsr_rm = 0x0000;  // RMM or fallback to RNE
                break;
        }

        const uint32_t current_mxcsr = _mm_getcsr();
        if ((current_mxcsr & 0x6000u) != mxcsr_rm) {
            _mm_setcsr((current_mxcsr & ~0x6000u) | mxcsr_rm);
        }
    }

    static inline void clear_exceptions() noexcept { _mm_setcsr(_mm_getcsr() & ~0x3fu); }

    static inline void accumulate_exceptions(CSRValue& fcsr) noexcept {
        const uint32_t mxcsr = _mm_getcsr();
        uint32_t flags = 0;
        if ((mxcsr & 0x01u) != 0) flags |= enum_mask(FflagsBit::Nv);
        if ((mxcsr & 0x04u) != 0) flags |= enum_mask(FflagsBit::Dz);
        if ((mxcsr & 0x08u) != 0) flags |= enum_mask(FflagsBit::Of);
        if ((mxcsr & 0x10u) != 0) flags |= enum_mask(FflagsBit::Uf);
        if ((mxcsr & 0x20u) != 0) flags |= enum_mask(FflagsBit::Nx);
        fcsr |= flags;
    }

    static inline void raise_invalid() noexcept { _mm_setcsr(_mm_getcsr() | 0x01u); }
};
#else
class FpEnvironment {
   public:
    static inline void set_rounding_mode(Word rm, CSRValue fcsr) noexcept {
        Word effective_rm = rm;
        if (effective_rm == enum_mask(RoundingMode::Dyn)) {
            effective_rm = (fcsr >> 5) & 0x7;
        }
        int fe_round = FE_TONEAREST;
        switch (effective_rm) {
            case enum_mask(RoundingMode::Rne):
                fe_round = FE_TONEAREST;
                break;
            case enum_mask(RoundingMode::Rtz):
                fe_round = FE_TOWARDZERO;
                break;
            case enum_mask(RoundingMode::Rdn):
                fe_round = FE_DOWNWARD;
                break;
            case enum_mask(RoundingMode::Rup):
                fe_round = FE_UPWARD;
                break;
            default:
                fe_round = FE_TONEAREST;
                break;
        }
        std::fesetround(fe_round);
    }

    static inline void clear_exceptions() noexcept { std::feclearexcept(FE_ALL_EXCEPT); }

    static inline void accumulate_exceptions(CSRValue& fcsr) noexcept {
        const int ex = std::fetestexcept(FE_ALL_EXCEPT);
        uint32_t flags = 0;
        if ((ex & FE_INVALID) != 0) flags |= enum_mask(FflagsBit::Nv);
        if ((ex & FE_DIVBYZERO) != 0) flags |= enum_mask(FflagsBit::Dz);
        if ((ex & FE_OVERFLOW) != 0) flags |= enum_mask(FflagsBit::Of);
        if ((ex & FE_UNDERFLOW) != 0) flags |= enum_mask(FflagsBit::Uf);
        if ((ex & FE_INEXACT) != 0) flags |= enum_mask(FflagsBit::Nx);
        fcsr |= flags;
    }

    static inline void raise_invalid() noexcept { std::feraiseexcept(FE_INVALID); }
};
#endif

// ============================================================================
// Min/Max Functions
// ============================================================================

template <typename FloatT, bool IsMax>
static auto fminmax_riscv(FloatT a, FloatT b) -> FloatingRegister {
    using T = FpTraits<FloatT>;
    const bool a_nan = std::isnan(a);
    const bool b_nan = std::isnan(b);
    if (is_snan(a) || is_snan(b)) {
        FpEnvironment::raise_invalid();
    }
    if (a_nan && b_nan) {
        return T::to_boxed(T::from_bits(T::kQnanBits));
    }
    if (a_nan) return T::to_boxed(b);
    if (b_nan) return T::to_boxed(a);
    if (a == FloatT{0} && b == FloatT{0}) {
        if constexpr (IsMax) {
            const bool pos = !std::signbit(a) || !std::signbit(b);
            return T::to_boxed(pos ? FloatT{0} : -FloatT{0});
        } else {
            const bool neg = std::signbit(a) || std::signbit(b);
            return T::to_boxed(neg ? -FloatT{0} : FloatT{0});
        }
    }
    return T::to_boxed(IsMax ? std::fmax(a, b) : std::fmin(a, b));
}

static auto fmin32_riscv(float a, float b) -> FloatingRegister {
    return fminmax_riscv<float, false>(a, b);
}
static auto fmax32_riscv(float a, float b) -> FloatingRegister {
    return fminmax_riscv<float, true>(a, b);
}
static auto fmin64_riscv(double a, double b) -> FloatingRegister {
    return fminmax_riscv<double, false>(a, b);
}
static auto fmax64_riscv(double a, double b) -> FloatingRegister {
    return fminmax_riscv<double, true>(a, b);
}

// ============================================================================
// Floating-Point to Integer Conversion
// ============================================================================

static auto fcvt_to_int(double value, Word op_mode, Word effective_rm, CSRValue& fcsr) -> Register {
    const bool is_unsigned = (op_mode & 1) != 0;
    const bool is_64 = (op_mode >= 2);

    auto saturate = [&](bool pos_inf) -> Register {
        fcsr |= enum_mask(FflagsBit::Nv);
        if (pos_inf) {
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
        return saturate(true);
    }

    if (std::isinf(value)) {
        return saturate(value > 0);
    }

    double rounded = 0.0;
    if (effective_rm == enum_mask(RoundingMode::Rtz)) {
        rounded = std::trunc(value);
    } else if (effective_rm == enum_mask(RoundingMode::Rdn)) {
        rounded = std::floor(value);
    } else if (effective_rm == enum_mask(RoundingMode::Rup)) {
        rounded = std::ceil(value);
    } else if (effective_rm == enum_mask(RoundingMode::Rmm)) {
        rounded = std::round(value);
    } else {
        // Round to nearest, ties to even (Rne / dynamic default fallback)
        rounded = std::rint(value);
    }

    if (is_64) {
        if (is_unsigned) {
            if (rounded < 0.0) {
                return saturate(false);
            }
            if (rounded >= 18446744073709551616.0) {
                return saturate(true);
            }
            if (rounded != value) fcsr |= enum_mask(FflagsBit::Nx);
            return static_cast<Register>(static_cast<uint64_t>(rounded));
        } else {
            if (rounded < -9223372036854775808.0) {
                return saturate(false);
            }
            if (rounded >= 9223372036854775808.0) {
                return saturate(true);
            }
            if (rounded != value) fcsr |= enum_mask(FflagsBit::Nx);
            return static_cast<Register>(static_cast<int64_t>(rounded));
        }
    } else {
        if (is_unsigned) {
            if (rounded < 0.0) {
                return saturate(false);
            }
            if (rounded >= 4294967296.0) {
                return saturate(true);
            }
            if (rounded != value) fcsr |= enum_mask(FflagsBit::Nx);
            return static_cast<Register>(
                static_cast<SignedWord>(static_cast<int32_t>(static_cast<uint32_t>(rounded))));
        } else {
            if (rounded < -2147483648.0) {
                return saturate(false);
            }
            if (rounded >= 2147483648.0) {
                return saturate(true);
            }
            if (rounded != value) fcsr |= enum_mask(FflagsBit::Nx);
            return static_cast<Register>(static_cast<SignedWord>(static_cast<int32_t>(rounded)));
        }
    }
}

}  // namespace fp

// ============================================================================
// Public ExecuteUnit Methods
// ============================================================================

using namespace fp;

auto ExecuteUnit::fusedFp(Opcode opcode, FpFmt fmt, FpRegId rs1, FpRegId rs2, FpRegId rs3,
                          RoundingMode rm, const FloatingRegister* freg, CSRValue& fcsr)
    -> FpExecResult {
    FpExecResult out;
    FpEnvironment::set_rounding_mode(std::to_underlying(rm), fcsr);
    FpEnvironment::clear_exceptions();
    const auto u_rs1 = std::to_underlying(rs1);
    const auto u_rs2 = std::to_underlying(rs2);
    const auto u_rs3 = std::to_underlying(rs3);
    if (fmt == FpFmt::Single) {
        const float a = read_f32(freg, u_rs1);
        const float b = read_f32(freg, u_rs2);
        const float c = read_f32(freg, u_rs3);
        float value = 0.0F;
        switch (opcode) {
            case Opcode::MAdd:
                value = __builtin_fmaf(a, b, c);
                break;
            case Opcode::MSub:
                value = __builtin_fmaf(a, b, -c);
                break;
            case Opcode::NMAdd:
                value = -__builtin_fmaf(a, b, c);
                break;
            case Opcode::NMSub:
                value = -__builtin_fmaf(a, b, -c);
                break;
            default:
                break;
        }
        if (simrv::compiler::unlikely(std::isnan(value))) {
            value = f32_from_bits(simrv::xlen::kF32Qnan);
        }
        out.fp_wb_data = write_f32_boxed(value);
        out.fp_wb_enable = true;
    } else if (fmt == FpFmt::Double) {
        const double a = read_f64(freg, u_rs1);
        const double b = read_f64(freg, u_rs2);
        const double c = read_f64(freg, u_rs3);
        double value = 0.0;
        switch (opcode) {
            case Opcode::MAdd:
                value = __builtin_fma(a, b, c);
                break;
            case Opcode::MSub:
                value = __builtin_fma(a, b, -c);
                break;
            case Opcode::NMAdd:
                value = -__builtin_fma(a, b, c);
                break;
            case Opcode::NMSub:
                value = -__builtin_fma(a, b, -c);
                break;
            default:
                break;
        }
        if (simrv::compiler::unlikely(std::isnan(value))) {
            value = f64_from_bits(simrv::xlen::kF64Qnan);
        }
        out.fp_wb_data = f64_bits(value);
        out.fp_wb_enable = true;
    }
    FpEnvironment::accumulate_exceptions(fcsr);
    return out;
}

namespace fp {

template <typename FloatT, typename Op>
inline auto fp_exec_binary(FpExecResult& out, Word rm, Word rs1, Word rs2,
                           const FloatingRegister* freg, CSRValue& fcsr, Op&& op) -> bool {
    using T = FpTraits<FloatT>;
    FpEnvironment::set_rounding_mode(rm, fcsr);
    FpEnvironment::clear_exceptions();
    FloatT result = op(T::read(freg, rs1), T::read(freg, rs2));
    if (simrv::compiler::unlikely(std::isnan(result))) {
        result = T::from_bits(T::kQnanBits);
    }
    out.fp_wb_data = T::to_boxed(result);
    out.fp_wb_enable = true;
    FpEnvironment::accumulate_exceptions(fcsr);
    return true;
}

template <typename FloatT, typename Op>
inline auto fp_exec_unary(FpExecResult& out, Word rm, Word rs1, const FloatingRegister* freg,
                          CSRValue& fcsr, Op&& op) -> bool {
    using T = FpTraits<FloatT>;
    FpEnvironment::set_rounding_mode(rm, fcsr);
    FpEnvironment::clear_exceptions();
    FloatT result = op(T::read(freg, rs1));
    if (simrv::compiler::unlikely(std::isnan(result))) {
        result = T::from_bits(T::kQnanBits);
    }
    out.fp_wb_data = T::to_boxed(result);
    out.fp_wb_enable = true;
    FpEnvironment::accumulate_exceptions(fcsr);
    return true;
}

bool fp_exec_add_sub(FpExecResult& out, Word funct7, Word rm, Word rs1, Word rs2,
                     const FloatingRegister* freg, CSRValue& fcsr) {
    if (funct7 == enum_mask(Funct7Fp::FaddS))
        return fp_exec_binary<float>(out, rm, rs1, rs2, freg, fcsr, std::plus<float>{});
    if (funct7 == enum_mask(Funct7Fp::FaddD))
        return fp_exec_binary<double>(out, rm, rs1, rs2, freg, fcsr, std::plus<double>{});
    if (funct7 == enum_mask(Funct7Fp::FsubS))
        return fp_exec_binary<float>(out, rm, rs1, rs2, freg, fcsr, std::minus<float>{});
    if (funct7 == enum_mask(Funct7Fp::FsubD))
        return fp_exec_binary<double>(out, rm, rs1, rs2, freg, fcsr, std::minus<double>{});
    return false;
}

bool fp_exec_mul_div_sqrt(FpExecResult& out, Word funct7, Word rm, Word rs1, Word rs2,
                          const FloatingRegister* freg, CSRValue& fcsr) {
    if (funct7 == enum_mask(Funct7Fp::FmulS))
        return fp_exec_binary<float>(out, rm, rs1, rs2, freg, fcsr, std::multiplies<float>{});
    if (funct7 == enum_mask(Funct7Fp::FmulD))
        return fp_exec_binary<double>(out, rm, rs1, rs2, freg, fcsr, std::multiplies<double>{});
    if (funct7 == enum_mask(Funct7Fp::FdivS))
        return fp_exec_binary<float>(out, rm, rs1, rs2, freg, fcsr, std::divides<float>{});
    if (funct7 == enum_mask(Funct7Fp::FdivD))
        return fp_exec_binary<double>(out, rm, rs1, rs2, freg, fcsr, std::divides<double>{});
    if (funct7 == enum_mask(Funct7Fp::FsqrtS))
        return fp_exec_unary<float>(out, rm, rs1, freg, fcsr, [](float x) { return std::sqrt(x); });
    if (funct7 == enum_mask(Funct7Fp::FsqrtD))
        return fp_exec_unary<double>(out, rm, rs1, freg, fcsr,
                                     [](double x) { return std::sqrt(x); });
    return false;
}

bool fp_exec_sgnj_minmax(FpExecResult& out, Word funct7, Funct3 funct3, Word rs1, Word rs2,
                         const FloatingRegister* freg, CSRValue& fcsr) {
    if (funct7 == enum_mask(Funct7Fp::FsgnjS) || funct7 == enum_mask(Funct7Fp::FsgnjD)) {
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
        if (enum_mask(funct3) == 0) value = (a & (~sign_bit)) | (b & sign_bit);
        if (enum_mask(funct3) == 1) value = (a & (~sign_bit)) | ((~b) & sign_bit);
        if (enum_mask(funct3) == 2) value = a ^ (b & sign_bit);
        out.fp_wb_data = is_d ? value
                              : (static_cast<FloatingRegister>(simrv::xlen::kF32BoxerBits) |
                                 (value & static_cast<FloatingRegister>(kLower32Mask)));
        out.fp_wb_enable = true;
        return true;
    }
    if (funct7 == enum_mask(Funct7Fp::FminmaxS)) {
        FpEnvironment::clear_exceptions();
        const float a = read_f32(freg, rs1);
        const float b = read_f32(freg, rs2);
        out.fp_wb_data = (enum_mask(funct3) == enum_mask(Funct3Fp::Max)) ? fmax32_riscv(a, b)
                                                                         : fmin32_riscv(a, b);
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    if (funct7 == enum_mask(Funct7Fp::FminmaxD)) {
        FpEnvironment::clear_exceptions();
        const double a = read_f64(freg, rs1);
        const double b = read_f64(freg, rs2);
        out.fp_wb_data = (enum_mask(funct3) == enum_mask(Funct3Fp::Max)) ? fmax64_riscv(a, b)
                                                                         : fmin64_riscv(a, b);
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    return false;
}

bool fp_exec_cvt(FpExecResult& out, Word funct7, Word rm, Word rs1, Word rs2, Register rrs1,
                 const FloatingRegister* freg, CSRValue& fcsr) {
    if (funct7 == enum_mask(Funct7Fp::FcvtSD)) {
        FpEnvironment::set_rounding_mode(rm, fcsr);
        FpEnvironment::clear_exceptions();
        const double src = read_f64(freg, rs1);
        if (is_snan64(src)) FpEnvironment::raise_invalid();
        out.fp_wb_data = std::isnan(src) ? write_f32_boxed(f32_from_bits(simrv::xlen::kF32Qnan))
                                         : write_f32_boxed(static_cast<float>(src));
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    if (funct7 == enum_mask(Funct7Fp::FcvtDS)) {
        FpEnvironment::set_rounding_mode(rm, fcsr);
        FpEnvironment::clear_exceptions();
        const float src = read_f32(freg, rs1);
        if (is_snan32(src)) FpEnvironment::raise_invalid();
        out.fp_wb_data = std::isnan(src) ? static_cast<FloatingRegister>(simrv::xlen::kF64Qnan)
                                         : f64_bits(static_cast<double>(src));
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    if (funct7 == enum_mask(Funct7Fp::FcvtWS) || funct7 == enum_mask(Funct7Fp::FcvtWD)) {
        const bool is_d = (funct7 == enum_mask(Funct7Fp::FcvtWD));
        const double a = !is_d ? static_cast<double>(read_f32(freg, rs1)) : read_f64(freg, rs1);
        Word effective_rm = rm;
        if (effective_rm == enum_mask(RoundingMode::Dyn)) {
            effective_rm = (fcsr >> 5) & 0x7;
        }
        out.int_wb_data = fcvt_to_int(a, rs2, effective_rm, fcsr);
        out.int_wb_enable = true;
        return true;
    }
    if (funct7 == enum_mask(Funct7Fp::FcvtSW) || funct7 == enum_mask(Funct7Fp::FcvtDW)) {
        const bool is_d = (funct7 == enum_mask(Funct7Fp::FcvtDW));
        FpEnvironment::set_rounding_mode(rm, fcsr);
        FpEnvironment::clear_exceptions();
        double val = 0;
        if (rs2 == 0)
            val = static_cast<double>(static_cast<int32_t>(rrs1));
        else if (rs2 == 1)
            val = static_cast<double>(static_cast<uint32_t>(rrs1));
        else if (rs2 == 2)
            val = static_cast<double>(static_cast<int64_t>(rrs1));
        else if (rs2 == 3)
            val = static_cast<double>(static_cast<uint64_t>(rrs1));

        out.fp_wb_data = is_d ? f64_bits(val) : write_f32_boxed(static_cast<float>(val));
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    return false;
}

bool fp_exec_cmp(FpExecResult& out, Word funct7, Funct3 funct3, Word rs1, Word rs2,
                 const FloatingRegister* freg, CSRValue& fcsr) {
    if (funct7 == enum_mask(Funct7Fp::FcmpS) || funct7 == enum_mask(Funct7Fp::FcmpD)) {
        const bool is_d = (funct7 == enum_mask(Funct7Fp::FcmpD));
        FpEnvironment::clear_exceptions();
        if (!is_d) {
            const float a = read_f32(freg, rs1);
            const float b = read_f32(freg, rs2);
            if (std::isnan(a) || std::isnan(b)) {
                if (enum_mask(funct3) != enum_mask(Funct3Fp::Eq) || is_snan32(a) || is_snan32(b)) {
                    FpEnvironment::raise_invalid();
                }
                out.int_wb_data = 0;
            } else {
                if (enum_mask(funct3) == enum_mask(Funct3Fp::Eq))
                    out.int_wb_data = static_cast<Register>(a == b);
                if (enum_mask(funct3) == enum_mask(Funct3Fp::Lt))
                    out.int_wb_data = static_cast<Register>(a < b);
                if (enum_mask(funct3) == enum_mask(Funct3Fp::Leq))
                    out.int_wb_data = static_cast<Register>(a <= b);
            }
        } else {
            const double a = read_f64(freg, rs1);
            const double b = read_f64(freg, rs2);
            if (std::isnan(a) || std::isnan(b)) {
                if (enum_mask(funct3) != enum_mask(Funct3Fp::Eq) || is_snan64(a) || is_snan64(b)) {
                    FpEnvironment::raise_invalid();
                }
                out.int_wb_data = 0;
            } else {
                if (enum_mask(funct3) == enum_mask(Funct3Fp::Eq))
                    out.int_wb_data = static_cast<Register>(a == b);
                if (enum_mask(funct3) == enum_mask(Funct3Fp::Lt))
                    out.int_wb_data = static_cast<Register>(a < b);
                if (enum_mask(funct3) == enum_mask(Funct3Fp::Leq))
                    out.int_wb_data = static_cast<Register>(a <= b);
            }
        }
        out.int_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    return false;
}

bool fp_exec_mv_class(FpExecResult& out, Word funct7, Funct3 funct3, Word rs1, Register rrs1,
                      const FloatingRegister* freg) {
    if (funct7 == enum_mask(Funct7Fp::FmvXW) || funct7 == enum_mask(Funct7Fp::FmvXD)) {
        const bool is_d = (funct7 == enum_mask(Funct7Fp::FmvXD));
        if (enum_mask(funct3) == enum_mask(Funct3Fp::FmvXW)) {
            out.int_wb_data = is_d ? static_cast<Register>(freg[rs1])
                                   : static_cast<Register>(
                                         static_cast<SignedWord>(static_cast<int32_t>(freg[rs1])));
            out.int_wb_enable = true;
        } else if (enum_mask(funct3) == enum_mask(Funct3Fp::Fclass)) {
            out.int_wb_data = is_d ? fclass64(read_f64(freg, rs1)) : fclass32(read_f32(freg, rs1));
            out.int_wb_enable = true;
        }
        return true;
    }
    if (funct7 == enum_mask(Funct7Fp::FmvWX) || funct7 == enum_mask(Funct7Fp::FmvDX)) {
        const bool is_d = (funct7 == enum_mask(Funct7Fp::FmvDX));
        out.fp_wb_data =
            is_d ? static_cast<FloatingRegister>(rrs1)
                 : (static_cast<FloatingRegister>(simrv::xlen::kF32BoxerBits) |
                    static_cast<FloatingRegister>(rrs1 & static_cast<Register>(kLower32Mask)));
        out.fp_wb_enable = true;
        return true;
    }
    return false;
}

}  // namespace fp

auto ExecuteUnit::opFp(Funct7 funct7, Funct3 funct3, FpRegId rs1, FpRegId rs2, Register rrs1,
                       const FloatingRegister* freg, CSRValue& fcsr) -> FpExecResult {
    FpExecResult out;
    const Word rm = enum_mask(funct3);
    const auto u_rs1 = std::to_underlying(rs1);
    const auto u_rs2 = std::to_underlying(rs2);

    if (fp::fp_exec_add_sub(out, funct7, rm, u_rs1, u_rs2, freg, fcsr)) return out;
    if (fp::fp_exec_mul_div_sqrt(out, funct7, rm, u_rs1, u_rs2, freg, fcsr)) return out;
    if (fp::fp_exec_sgnj_minmax(out, funct7, funct3, u_rs1, u_rs2, freg, fcsr)) return out;
    if (fp::fp_exec_cvt(out, funct7, rm, u_rs1, u_rs2, rrs1, freg, fcsr)) return out;
    if (fp::fp_exec_cmp(out, funct7, funct3, u_rs1, u_rs2, freg, fcsr)) return out;
    if (fp::fp_exec_mv_class(out, funct7, funct3, u_rs1, rrs1, freg)) return out;

    return out;
}

}  // namespace simrv::execute
