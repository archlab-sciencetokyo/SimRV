/**
 * @file ExecuteUnitFp.cpp
 * @brief High-performance RISC-V floating-point execution unit implementation.
 */
#include "simrv/execute/ExecuteUnitFp.hpp"

#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>

#include "simrv/CompilerHints.hpp"
#include "simrv/core/CsrTypes.hpp"
#include "simrv/execute/RoundingRmm.hpp"
#include "simrv/isa/FpConversions.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/RegisterTypes.hpp"
#include "vector/VectorHelpers.hpp"

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
using simrv::isa::resolve_rounding_mode;
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

constexpr auto read_f32(const simrv::xlen::FloatingRegister* freg, simrv::xlen::Word idx) -> float {
    const simrv::xlen::FloatingRegister val = freg[idx];
    if ((val & simrv::xlen::kF32BoxerBits) != simrv::xlen::kF32BoxerBits) {
        return f32_from_bits(0x7fc00000U);
    }
    return f32_from_bits(static_cast<uint32_t>(
        val & static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kLower32Mask)));
}

constexpr auto read_f64(const simrv::xlen::FloatingRegister* freg, simrv::xlen::Word idx)
    -> double {
    return f64_from_bits(freg[idx]);
}

constexpr auto write_f32_boxed(float v) -> simrv::xlen::FloatingRegister {
    return static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kF32BoxerBits) |
           static_cast<simrv::xlen::FloatingRegister>(f32_bits(v));
}

constexpr auto read_f16(const simrv::xlen::FloatingRegister* freg, simrv::xlen::Word idx)
    -> uint16_t {
    const auto val = freg[idx];
    return (val & ~simrv::xlen::FloatingRegister{0xFFFF}) == ~simrv::xlen::FloatingRegister{0xFFFF}
               ? static_cast<uint16_t>(val)
               : UINT16_C(0x7E00);
}

constexpr auto write_f16_boxed(uint16_t value) -> simrv::xlen::FloatingRegister {
    return ~simrv::xlen::FloatingRegister{0xFFFF} | value;
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
    static constexpr auto to_boxed(float v) -> simrv::xlen::FloatingRegister {
        return write_f32_boxed(v);
    }
    static constexpr auto read(const simrv::xlen::FloatingRegister* freg, simrv::xlen::Word idx)
        -> float {
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
    static constexpr auto to_boxed(double v) -> simrv::xlen::FloatingRegister {
        return f64_bits(v);
    }
    static constexpr auto read(const simrv::xlen::FloatingRegister* freg, simrv::xlen::Word idx)
        -> double {
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
    static inline void set_rounding_mode(simrv::xlen::Word rm,
                                         simrv::xlen::CsrValue fcsr) noexcept {
        const RoundingMode effective_rm = resolve_rounding_mode(rm, fcsr);

        uint32_t mxcsr_rm = 0;
        switch (effective_rm) {
            case RoundingMode::Rne:
                mxcsr_rm = 0x0000;
                break;
            case RoundingMode::Rdn:
                mxcsr_rm = 0x2000;
                break;
            case RoundingMode::Rup:
                mxcsr_rm = 0x4000;
                break;
            case RoundingMode::Rtz:
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

    static inline void accumulate_exceptions(simrv::xlen::CsrValue& fcsr) noexcept {
        const uint32_t mxcsr = _mm_getcsr();
        simrv::xlen::CsrValue flags = 0;
        if ((mxcsr & 0x01u) != 0) flags |= FflagsBit::Nv;
        if ((mxcsr & 0x04u) != 0) flags |= FflagsBit::Dz;
        if ((mxcsr & 0x08u) != 0) flags |= FflagsBit::Of;
        if ((mxcsr & 0x10u) != 0) flags |= FflagsBit::Uf;
        if ((mxcsr & 0x20u) != 0) flags |= FflagsBit::Nx;
        fcsr |= flags;
    }

    static inline void raise_flags(simrv::xlen::CsrValue flags) noexcept {
        uint32_t mxcsr_flags = 0;
        if ((flags & FflagsBit::Nv) != 0) mxcsr_flags |= 0x01u;
        if ((flags & FflagsBit::Dz) != 0) mxcsr_flags |= 0x04u;
        if ((flags & FflagsBit::Of) != 0) mxcsr_flags |= 0x08u;
        if ((flags & FflagsBit::Uf) != 0) mxcsr_flags |= 0x10u;
        if ((flags & FflagsBit::Nx) != 0) mxcsr_flags |= 0x20u;
        _mm_setcsr(_mm_getcsr() | mxcsr_flags);
    }

    static inline void raise_invalid() noexcept { _mm_setcsr(_mm_getcsr() | 0x01u); }
};
#else
class FpEnvironment {
   public:
    static inline void set_rounding_mode(simrv::xlen::Word rm,
                                         simrv::xlen::CsrValue fcsr) noexcept {
        const RoundingMode effective_rm = resolve_rounding_mode(rm, fcsr);
        int fe_round = FE_TONEAREST;
        switch (effective_rm) {
            case RoundingMode::Rne:
                fe_round = FE_TONEAREST;
                break;
            case RoundingMode::Rtz:
                fe_round = FE_TOWARDZERO;
                break;
            case RoundingMode::Rdn:
                fe_round = FE_DOWNWARD;
                break;
            case RoundingMode::Rup:
                fe_round = FE_UPWARD;
                break;
            default:
                fe_round = FE_TONEAREST;
                break;
        }
        std::fesetround(fe_round);
    }

    static inline void clear_exceptions() noexcept { std::feclearexcept(FE_ALL_EXCEPT); }

    static inline void accumulate_exceptions(simrv::xlen::CsrValue& fcsr) noexcept {
        const int ex = std::fetestexcept(FE_ALL_EXCEPT);
        simrv::xlen::CsrValue flags = 0;
        if ((ex & FE_INVALID) != 0) flags |= FflagsBit::Nv;
        if ((ex & FE_DIVBYZERO) != 0) flags |= FflagsBit::Dz;
        if ((ex & FE_OVERFLOW) != 0) flags |= FflagsBit::Of;
        if ((ex & FE_UNDERFLOW) != 0) flags |= FflagsBit::Uf;
        if ((ex & FE_INEXACT) != 0) flags |= FflagsBit::Nx;
        fcsr |= flags;
    }

    static inline void raise_flags(simrv::xlen::CsrValue flags) noexcept {
        int exceptions = 0;
        if ((flags & FflagsBit::Nv) != 0) exceptions |= FE_INVALID;
        if ((flags & FflagsBit::Dz) != 0) exceptions |= FE_DIVBYZERO;
        if ((flags & FflagsBit::Of) != 0) exceptions |= FE_OVERFLOW;
        if ((flags & FflagsBit::Uf) != 0) exceptions |= FE_UNDERFLOW;
        if ((flags & FflagsBit::Nx) != 0) exceptions |= FE_INEXACT;
        if (exceptions != 0) std::feraiseexcept(exceptions);
    }

    static inline void raise_invalid() noexcept { std::feraiseexcept(FE_INVALID); }
};
#endif

// ============================================================================
// Min/Max Functions
// ============================================================================

template <typename FloatT, bool IsMax>
static auto fminmax_riscv(FloatT a, FloatT b) -> simrv::xlen::FloatingRegister {
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

static auto fmin32_riscv(float a, float b) -> simrv::xlen::FloatingRegister {
    return fminmax_riscv<float, false>(a, b);
}
static auto fmax32_riscv(float a, float b) -> simrv::xlen::FloatingRegister {
    return fminmax_riscv<float, true>(a, b);
}
static auto fmin64_riscv(double a, double b) -> simrv::xlen::FloatingRegister {
    return fminmax_riscv<double, false>(a, b);
}
static auto fmax64_riscv(double a, double b) -> simrv::xlen::FloatingRegister {
    return fminmax_riscv<double, true>(a, b);
}

// ============================================================================
// Floating-Point to Integer Conversion
// ============================================================================

static auto fcvt_to_int(double value, simrv::xlen::Word op_mode, RoundingMode effective_rm,
                        simrv::xlen::CsrValue& fcsr) -> simrv::xlen::Register {
    const bool is_unsigned = (op_mode & 1) != 0;
    const bool is_64 = (op_mode >= 2);

    auto saturate = [&](bool pos_inf) -> simrv::xlen::Register {
        fcsr |= FflagsBit::Nv;
        if (pos_inf) {
            if (is_64) {
                return is_unsigned ? static_cast<simrv::xlen::Register>(
                                         std::numeric_limits<uint64_t>::max())
                                   : static_cast<simrv::xlen::Register>(
                                         std::numeric_limits<int64_t>::max());
            } else {
                const uint32_t max_val =
                    is_unsigned ? std::numeric_limits<uint32_t>::max()
                                : static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
                return static_cast<simrv::xlen::Register>(
                    static_cast<simrv::xlen::SignedWord>(static_cast<int32_t>(max_val)));
            }
        } else {
            if (is_64) {
                return is_unsigned ? 0ULL
                                   : static_cast<simrv::xlen::Register>(
                                         std::numeric_limits<int64_t>::min());
            } else {
                const uint32_t min_val =
                    is_unsigned ? 0U : static_cast<uint32_t>(std::numeric_limits<int32_t>::min());
                return static_cast<simrv::xlen::Register>(
                    static_cast<simrv::xlen::SignedWord>(static_cast<int32_t>(min_val)));
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
    if (effective_rm == RoundingMode::Rtz) {
        rounded = std::trunc(value);
    } else if (effective_rm == RoundingMode::Rdn) {
        rounded = std::floor(value);
    } else if (effective_rm == RoundingMode::Rup) {
        rounded = std::ceil(value);
    } else if (effective_rm == RoundingMode::Rmm) {
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
            if (rounded != value) fcsr |= FflagsBit::Nx;
            return static_cast<simrv::xlen::Register>(static_cast<uint64_t>(rounded));
        } else {
            if (rounded < -9223372036854775808.0) {
                return saturate(false);
            }
            if (rounded >= 9223372036854775808.0) {
                return saturate(true);
            }
            if (rounded != value) fcsr |= FflagsBit::Nx;
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(rounded));
        }
    } else {
        if (is_unsigned) {
            if (rounded < 0.0) {
                return saturate(false);
            }
            if (rounded >= 4294967296.0) {
                return saturate(true);
            }
            if (rounded != value) fcsr |= FflagsBit::Nx;
            return static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                static_cast<int32_t>(static_cast<uint32_t>(rounded))));
        } else {
            if (rounded < -2147483648.0) {
                return saturate(false);
            }
            if (rounded >= 2147483648.0) {
                return saturate(true);
            }
            if (rounded != value) fcsr |= FflagsBit::Nx;
            return static_cast<simrv::xlen::Register>(
                static_cast<simrv::xlen::SignedWord>(static_cast<int32_t>(rounded)));
        }
    }
}

}  // namespace fp

// ============================================================================
// Public ExecuteUnit Methods
// ============================================================================

using namespace fp;

auto ExecuteUnitFp::fusedFp(Opcode opcode, FpFmt fmt, simrv::xlen::FpRegId rs1,
                            simrv::xlen::FpRegId rs2, simrv::xlen::FpRegId rs3, RoundingMode rm,
                            const simrv::xlen::FloatingRegister* freg, simrv::xlen::CsrValue& fcsr)
    -> FpExecResult {
    FpExecResult out;
    const auto effective_rm = resolve_rounding_mode(std::to_underlying(rm), fcsr);
    const bool use_rmm = effective_rm == RoundingMode::Rmm;
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
                value = use_rmm ? rmm::fma(a, b, c) : __builtin_fmaf(a, b, c);
                break;
            case Opcode::MSub:
                value = use_rmm ? rmm::fma(a, b, -c) : __builtin_fmaf(a, b, -c);
                break;
            case Opcode::NMAdd:
                value = use_rmm ? rmm::fma(-a, b, -c) : __builtin_fmaf(-a, b, -c);
                break;
            case Opcode::NMSub:
                value = use_rmm ? rmm::fma(-a, b, c) : __builtin_fmaf(-a, b, c);
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
                value = use_rmm ? rmm::fma(a, b, c) : __builtin_fma(a, b, c);
                break;
            case Opcode::MSub:
                value = use_rmm ? rmm::fma(a, b, -c) : __builtin_fma(a, b, -c);
                break;
            case Opcode::NMAdd:
                value = use_rmm ? rmm::fma(-a, b, -c) : __builtin_fma(-a, b, -c);
                break;
            case Opcode::NMSub:
                value = use_rmm ? rmm::fma(-a, b, c) : __builtin_fma(-a, b, c);
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
inline auto fp_exec_binary(FpExecResult& out, simrv::xlen::Word rm, simrv::xlen::Word rs1,
                           simrv::xlen::Word rs2, const simrv::xlen::FloatingRegister* freg,
                           simrv::xlen::CsrValue& fcsr, Op&& op) -> bool {
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
inline auto fp_exec_unary(FpExecResult& out, simrv::xlen::Word rm, simrv::xlen::Word rs1,
                          const simrv::xlen::FloatingRegister* freg, simrv::xlen::CsrValue& fcsr,
                          Op&& op) -> bool {
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

bool fp_exec_add_sub(FpExecResult& out, simrv::xlen::Word funct7, simrv::xlen::Word rm,
                     simrv::xlen::Word rs1, simrv::xlen::Word rs2,
                     const simrv::xlen::FloatingRegister* freg, simrv::xlen::CsrValue& fcsr) {
    const bool rmm_mode = resolve_rounding_mode(rm, fcsr) == RoundingMode::Rmm;
    if (funct7 == Funct7Fp::FaddS)
        return fp_exec_binary<float>(out, rm, rs1, rs2, freg, fcsr, [=](float a, float b) {
            return rmm_mode ? rmm::add(a, b) : a + b;
        });
    if (funct7 == Funct7Fp::FaddD)
        return fp_exec_binary<double>(out, rm, rs1, rs2, freg, fcsr, [=](double a, double b) {
            return rmm_mode ? rmm::add(a, b) : a + b;
        });
    if (funct7 == Funct7Fp::FsubS)
        return fp_exec_binary<float>(out, rm, rs1, rs2, freg, fcsr, [=](float a, float b) {
            return rmm_mode ? rmm::sub(a, b) : a - b;
        });
    if (funct7 == Funct7Fp::FsubD)
        return fp_exec_binary<double>(out, rm, rs1, rs2, freg, fcsr, [=](double a, double b) {
            return rmm_mode ? rmm::sub(a, b) : a - b;
        });
    return false;
}

bool fp_exec_mul_div_sqrt(FpExecResult& out, simrv::xlen::Word funct7, simrv::xlen::Word rm,
                          simrv::xlen::Word rs1, simrv::xlen::Word rs2,
                          const simrv::xlen::FloatingRegister* freg, simrv::xlen::CsrValue& fcsr) {
    const bool rmm_mode = resolve_rounding_mode(rm, fcsr) == RoundingMode::Rmm;
    if (funct7 == Funct7Fp::FmulS)
        return fp_exec_binary<float>(out, rm, rs1, rs2, freg, fcsr, [=](float a, float b) {
            return rmm_mode ? rmm::mul(a, b) : a * b;
        });
    if (funct7 == Funct7Fp::FmulD)
        return fp_exec_binary<double>(out, rm, rs1, rs2, freg, fcsr, [=](double a, double b) {
            return rmm_mode ? rmm::mul(a, b) : a * b;
        });
    if (funct7 == Funct7Fp::FdivS)
        return fp_exec_binary<float>(out, rm, rs1, rs2, freg, fcsr, [=](float a, float b) {
            return rmm_mode ? rmm::div(a, b) : a / b;
        });
    if (funct7 == Funct7Fp::FdivD)
        return fp_exec_binary<double>(out, rm, rs1, rs2, freg, fcsr, [=](double a, double b) {
            return rmm_mode ? rmm::div(a, b) : a / b;
        });
    if (funct7 == Funct7Fp::FsqrtS)
        return fp_exec_unary<float>(out, rm, rs1, freg, fcsr, [=](float x) {
            return rmm_mode ? rmm::sqrt(x) : std::sqrt(x);
        });
    if (funct7 == Funct7Fp::FsqrtD)
        return fp_exec_unary<double>(out, rm, rs1, freg, fcsr, [=](double x) {
            return rmm_mode ? rmm::sqrt(x) : std::sqrt(x);
        });
    return false;
}

bool fp_exec_sgnj_minmax(FpExecResult& out, simrv::xlen::Word funct7, Funct3 funct3,
                         simrv::xlen::Word rs1, simrv::xlen::Word rs2,
                         const simrv::xlen::FloatingRegister* freg, simrv::xlen::CsrValue& fcsr) {
    if (funct7 == Funct7Fp::FsgnjS || funct7 == Funct7Fp::FsgnjD) {
        const bool is_d = (funct7 == Funct7Fp::FsgnjD);
        simrv::xlen::FloatingRegister a = freg[rs1];
        simrv::xlen::FloatingRegister b = freg[rs2];
        if (!is_d) {
            if ((a & simrv::xlen::kF32BoxerBits) != simrv::xlen::kF32BoxerBits) {
                a = static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kF32BoxerBits) |
                    0x7fc00000U;
            }
            if ((b & simrv::xlen::kF32BoxerBits) != simrv::xlen::kF32BoxerBits) {
                b = static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kF32BoxerBits) |
                    0x7fc00000U;
            }
        }
        simrv::xlen::FloatingRegister value = 0;
        const uint64_t sign_bit = is_d ? (1ULL << 63U) : (1ULL << 31U);
        if (funct3 == Funct3Fp::Sgnj) value = (a & (~sign_bit)) | (b & sign_bit);
        if (funct3 == Funct3Fp::Sgnjn) value = (a & (~sign_bit)) | ((~b) & sign_bit);
        if (funct3 == Funct3Fp::Sgnjx) value = a ^ (b & sign_bit);
        out.fp_wb_data =
            is_d
                ? value
                : (static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kF32BoxerBits) |
                   (value & static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kLower32Mask)));
        out.fp_wb_enable = true;
        return true;
    }
    if (funct7 == Funct7Fp::FminmaxS) {
        FpEnvironment::clear_exceptions();
        const float a = read_f32(freg, rs1);
        const float b = read_f32(freg, rs2);
        if (funct3 == 2 || funct3 == 3) {
            out.fp_wb_data = std::isnan(a) || std::isnan(b)
                                 ? write_f32_boxed(f32_from_bits(simrv::xlen::kF32Qnan))
                                 : (funct3 == 3 ? fmax32_riscv(a, b) : fmin32_riscv(a, b));
            if (is_snan32(a) || is_snan32(b)) FpEnvironment::raise_invalid();
        } else {
            out.fp_wb_data = (funct3 == Funct3Fp::Max) ? fmax32_riscv(a, b) : fmin32_riscv(a, b);
        }
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    if (funct7 == Funct7Fp::FminmaxD) {
        FpEnvironment::clear_exceptions();
        const double a = read_f64(freg, rs1);
        const double b = read_f64(freg, rs2);
        if (funct3 == 2 || funct3 == 3) {
            out.fp_wb_data = std::isnan(a) || std::isnan(b)
                                 ? static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kF64Qnan)
                                 : (funct3 == 3 ? fmax64_riscv(a, b) : fmin64_riscv(a, b));
            if (is_snan64(a) || is_snan64(b)) FpEnvironment::raise_invalid();
        } else {
            out.fp_wb_data = (funct3 == Funct3Fp::Max) ? fmax64_riscv(a, b) : fmin64_riscv(a, b);
        }
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    return false;
}

bool fp_exec_cvt(FpExecResult& out, simrv::xlen::Word funct7, simrv::xlen::Word rm,
                 simrv::xlen::Word rs1, simrv::xlen::Word rs2, simrv::xlen::Register rrs1,
                 const simrv::xlen::FloatingRegister* freg, simrv::xlen::CsrValue& fcsr) {
    if ((funct7 == 0x20 || funct7 == 0x21) && (rs2 == 4 || rs2 == 5)) {
        const bool is_d = funct7 == 0x21;
        const bool signaling_nan =
            is_d ? is_snan64(read_f64(freg, rs1)) : is_snan32(read_f32(freg, rs1));
        const double source = is_d ? read_f64(freg, rs1) : static_cast<double>(read_f32(freg, rs1));
        const auto mode = resolve_rounding_mode(rm, fcsr);
        FpEnvironment::set_rounding_mode(rm, fcsr);
        const double rounded = std::isnan(source) || std::isinf(source) || source == 0.0 ? source
                               : mode == RoundingMode::Rtz ? std::trunc(source)
                               : mode == RoundingMode::Rdn ? std::floor(source)
                               : mode == RoundingMode::Rup ? std::ceil(source)
                               : mode == RoundingMode::Rmm ? std::round(source)
                                                           : std::nearbyint(source);
        FpEnvironment::clear_exceptions();
        if (signaling_nan) FpEnvironment::raise_invalid();
        if (rs2 == 5 && !std::isnan(source) && rounded != source)
            FpEnvironment::raise_flags(std::to_underlying(simrv::isa::FflagsBit::Nx));
        out.fp_wb_data = is_d ? f64_bits(rounded) : write_f32_boxed(static_cast<float>(rounded));
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    if (funct7 == 0x61 && rs2 == 8 && rm == 1) {
        const double source = read_f64(freg, rs1);
        const auto legacy = fcvt_to_int(source, 0, RoundingMode::Rtz, fcsr);
        (void)legacy;
        double reduced = std::isfinite(source) ? std::fmod(std::trunc(source), 4294967296.0) : 0.0;
        if (reduced < 0.0) reduced += 4294967296.0;
        const uint32_t low = static_cast<uint32_t>(reduced);
        out.int_wb_data = static_cast<simrv::xlen::Register>(static_cast<int32_t>(low));
        out.int_wb_enable = true;
        return true;
    }
    if (funct7 == 0x20 && rs2 == 2) {  // FCVT.S.H (exact widening conversion)
        FpEnvironment::clear_exceptions();
        const uint16_t raw = read_f16(freg, rs1);
        const float src = simrv::execute::vector::fp16_to_fp32(raw);
        if ((raw & UINT16_C(0x7C00)) == UINT16_C(0x7C00) && (raw & UINT16_C(0x03FF)) != 0 &&
            (raw & UINT16_C(0x0200)) == 0) {
            FpEnvironment::raise_invalid();
        }
        out.fp_wb_data =
            write_f32_boxed(std::isnan(src) ? f32_from_bits(simrv::xlen::kF32Qnan) : src);
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    if (funct7 == 0x22 && rs2 == 0) {  // FCVT.H.S
        const float src = read_f32(freg, rs1);
        FpEnvironment::clear_exceptions();
        const auto converted = isa::fp::to_binary16(src, resolve_rounding_mode(rm, fcsr));
        FpEnvironment::raise_flags(converted.flags);
        const bool is_nan = (converted.bits & UINT16_C(0x7FFF)) > UINT16_C(0x7C00);
        out.fp_wb_data = write_f16_boxed(is_nan ? UINT16_C(0x7E00) : converted.bits);
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    if (funct7 == 0x21 && rs2 == 2) {  // FCVT.D.H (exact widening conversion)
        FpEnvironment::clear_exceptions();
        const uint16_t raw = read_f16(freg, rs1);
        const float src = simrv::execute::vector::fp16_to_fp32(raw);
        if ((raw & UINT16_C(0x7C00)) == UINT16_C(0x7C00) && (raw & UINT16_C(0x03FF)) != 0 &&
            (raw & UINT16_C(0x0200)) == 0) {
            FpEnvironment::raise_invalid();
        }
        out.fp_wb_data = std::isnan(src)
                             ? static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kF64Qnan)
                             : f64_bits(static_cast<double>(src));
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    if (funct7 == 0x22 && rs2 == 1) {  // FCVT.H.D
        const double src = read_f64(freg, rs1);
        FpEnvironment::clear_exceptions();
        const auto converted = isa::fp::to_binary16(src, resolve_rounding_mode(rm, fcsr));
        FpEnvironment::raise_flags(converted.flags);
        const bool is_nan = (converted.bits & UINT16_C(0x7FFF)) > UINT16_C(0x7C00);
        out.fp_wb_data = write_f16_boxed(is_nan ? UINT16_C(0x7E00) : converted.bits);
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    if (funct7 == Funct7Fp::FcvtSD) {
        const auto mode = resolve_rounding_mode(rm, fcsr);
        FpEnvironment::set_rounding_mode(rm, fcsr);
        FpEnvironment::clear_exceptions();
        const double src = read_f64(freg, rs1);
        if (is_snan64(src)) FpEnvironment::raise_invalid();
        const float converted =
            mode == RoundingMode::Rmm ? rmm::narrow_f32(src) : static_cast<float>(src);
        out.fp_wb_data = std::isnan(src) ? write_f32_boxed(f32_from_bits(simrv::xlen::kF32Qnan))
                                         : write_f32_boxed(converted);
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    if (funct7 == Funct7Fp::FcvtDS) {
        FpEnvironment::set_rounding_mode(rm, fcsr);
        FpEnvironment::clear_exceptions();
        const float src = read_f32(freg, rs1);
        if (is_snan32(src)) FpEnvironment::raise_invalid();
        out.fp_wb_data = std::isnan(src)
                             ? static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kF64Qnan)
                             : f64_bits(static_cast<double>(src));
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    if (funct7 == Funct7Fp::FcvtWS || funct7 == Funct7Fp::FcvtWD) {
        const bool is_d = (funct7 == Funct7Fp::FcvtWD);
        const double a = !is_d ? static_cast<double>(read_f32(freg, rs1)) : read_f64(freg, rs1);
        const RoundingMode effective_rm = resolve_rounding_mode(rm, fcsr);
        out.int_wb_data = fcvt_to_int(a, rs2, effective_rm, fcsr);
        out.int_wb_enable = true;
        return true;
    }
    if (funct7 == Funct7Fp::FcvtSW || funct7 == Funct7Fp::FcvtDW) {
        const bool is_d = (funct7 == Funct7Fp::FcvtDW);
        FpEnvironment::set_rounding_mode(rm, fcsr);
        FpEnvironment::clear_exceptions();
        const bool rmm_mode = resolve_rounding_mode(rm, fcsr) == RoundingMode::Rmm;
        double val64 = 0;
        float val32 = 0;
        const auto convert_integer = [&](auto value) {
            if (is_d)
                val64 = rmm_mode ? rmm::from_integer_f64(value) : static_cast<double>(value);
            else
                val32 = rmm_mode ? rmm::from_integer_f32(value) : static_cast<float>(value);
        };
        if (rs2 == 0)
            convert_integer(static_cast<int32_t>(rrs1));
        else if (rs2 == 1)
            convert_integer(static_cast<uint32_t>(rrs1));
        else if (rs2 == 2)
            convert_integer(static_cast<int64_t>(rrs1));
        else if (rs2 == 3)
            convert_integer(static_cast<uint64_t>(rrs1));

        out.fp_wb_data = is_d ? f64_bits(val64) : write_f32_boxed(val32);
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    return false;
}

bool fp_exec_cmp(FpExecResult& out, simrv::xlen::Word funct7, Funct3 funct3, simrv::xlen::Word rs1,
                 simrv::xlen::Word rs2, const simrv::xlen::FloatingRegister* freg,
                 simrv::xlen::CsrValue& fcsr) {
    if (funct7 == Funct7Fp::FcmpS || funct7 == Funct7Fp::FcmpD) {
        const bool is_d = (funct7 == Funct7Fp::FcmpD);
        FpEnvironment::clear_exceptions();
        if (!is_d) {
            const float a = read_f32(freg, rs1);
            const float b = read_f32(freg, rs2);
            if (std::isnan(a) || std::isnan(b)) {
                const bool quiet_ordered = funct3 == 4 || funct3 == 5;
                if ((!quiet_ordered && funct3 != Funct3Fp::Eq) || is_snan32(a) || is_snan32(b)) {
                    FpEnvironment::raise_invalid();
                }
                out.int_wb_data = 0;
            } else {
                if (funct3 == Funct3Fp::Eq)
                    out.int_wb_data = static_cast<simrv::xlen::Register>(a == b);
                if (funct3 == Funct3Fp::Lt || funct3 == 5)
                    out.int_wb_data = static_cast<simrv::xlen::Register>(a < b);
                if (funct3 == Funct3Fp::Leq || funct3 == 4)
                    out.int_wb_data = static_cast<simrv::xlen::Register>(a <= b);
            }
        } else {
            const double a = read_f64(freg, rs1);
            const double b = read_f64(freg, rs2);
            if (std::isnan(a) || std::isnan(b)) {
                const bool quiet_ordered = funct3 == 4 || funct3 == 5;
                if ((!quiet_ordered && funct3 != Funct3Fp::Eq) || is_snan64(a) || is_snan64(b)) {
                    FpEnvironment::raise_invalid();
                }
                out.int_wb_data = 0;
            } else {
                if (funct3 == Funct3Fp::Eq)
                    out.int_wb_data = static_cast<simrv::xlen::Register>(a == b);
                if (funct3 == Funct3Fp::Lt || funct3 == 5)
                    out.int_wb_data = static_cast<simrv::xlen::Register>(a < b);
                if (funct3 == Funct3Fp::Leq || funct3 == 4)
                    out.int_wb_data = static_cast<simrv::xlen::Register>(a <= b);
            }
        }
        out.int_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return true;
    }
    return false;
}

bool fp_exec_mv_class(FpExecResult& out, simrv::xlen::Word funct7, Funct3 funct3,
                      simrv::xlen::Word rs1, simrv::xlen::Word rs2, simrv::xlen::Register rrs1,
                      const simrv::xlen::FloatingRegister* freg) {
    if ((funct7 == 0x78 || funct7 == 0x79) && rs2 == 1 && funct3 == 0) {
        constexpr std::array<double, 32> kValues = {-1.0,
                                                    std::numeric_limits<double>::min(),
                                                    0x1p-16,
                                                    0x1p-15,
                                                    0x1p-8,
                                                    0x1p-7,
                                                    0.0625,
                                                    0.125,
                                                    0.25,
                                                    0.3125,
                                                    0.375,
                                                    0.4375,
                                                    0.5,
                                                    0.625,
                                                    0.75,
                                                    0.875,
                                                    1.0,
                                                    1.25,
                                                    1.5,
                                                    1.75,
                                                    2.0,
                                                    2.5,
                                                    3.0,
                                                    4.0,
                                                    8.0,
                                                    16.0,
                                                    128.0,
                                                    256.0,
                                                    32768.0,
                                                    65536.0,
                                                    std::numeric_limits<double>::infinity(),
                                                    std::numeric_limits<double>::quiet_NaN()};
        if (rs1 >= kValues.size()) return false;
        if (funct7 == 0x78) {
            const float value =
                rs1 == 1 ? std::numeric_limits<float>::min() : static_cast<float>(kValues[rs1]);
            out.fp_wb_data =
                write_f32_boxed(std::isnan(value) ? f32_from_bits(0x7fc00000U) : value);
        } else {
            out.fp_wb_data = rs1 == 31 ? simrv::xlen::kF64Qnan : f64_bits(kValues[rs1]);
        }
        out.fp_wb_enable = true;
        return true;
    }
    if (funct7 == 0x72 && funct3 == Funct3Fp::FmvXW) {  // FMV.X.H
        out.int_wb_data = static_cast<simrv::xlen::Register>(
            static_cast<int16_t>(static_cast<uint16_t>(freg[rs1])));
        out.int_wb_enable = true;
        return true;
    }
    if (funct7 == 0x7A && funct3 == Funct3Fp::FmvXW) {  // FMV.H.X
        out.fp_wb_data = write_f16_boxed(static_cast<uint16_t>(rrs1));
        out.fp_wb_enable = true;
        return true;
    }
    if (funct7 == Funct7Fp::FmvXW || funct7 == Funct7Fp::FmvXD) {
        const bool is_d = (funct7 == Funct7Fp::FmvXD);
        if (funct3 == Funct3Fp::FmvXW) {
            out.int_wb_data =
                is_d ? static_cast<simrv::xlen::Register>(freg[rs1])
                     : static_cast<simrv::xlen::Register>(
                           static_cast<simrv::xlen::SignedWord>(static_cast<int32_t>(freg[rs1])));
            out.int_wb_enable = true;
        } else if (funct3 == Funct3Fp::Fclass) {
            out.int_wb_data = is_d ? fclass64(read_f64(freg, rs1)) : fclass32(read_f32(freg, rs1));
            out.int_wb_enable = true;
        }
        return true;
    }
    if (funct7 == Funct7Fp::FmvWX || funct7 == Funct7Fp::FmvDX) {
        const bool is_d = (funct7 == Funct7Fp::FmvDX);
        out.fp_wb_data =
            is_d ? static_cast<simrv::xlen::FloatingRegister>(rrs1)
                 : (static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kF32BoxerBits) |
                    static_cast<simrv::xlen::FloatingRegister>(
                        rrs1 & static_cast<simrv::xlen::Register>(simrv::xlen::kLower32Mask)));
        out.fp_wb_enable = true;
        return true;
    }
    return false;
}

}  // namespace fp

auto ExecuteUnitFp::opFp(simrv::xlen::Funct7 funct7, Funct3 funct3, simrv::xlen::FpRegId rs1,
                         simrv::xlen::FpRegId rs2, simrv::xlen::Register rrs1,
                         const simrv::xlen::FloatingRegister* freg, simrv::xlen::CsrValue& fcsr)
    -> FpExecResult {
    FpExecResult out;
    const simrv::xlen::Word rm = std::to_underlying(funct3);
    const auto u_rs1 = std::to_underlying(rs1);
    const auto u_rs2 = std::to_underlying(rs2);

    // Zfa FROUND instructions share the FCVT.S.D / FCVT.D.S encodings and
    // distinguish themselves with rs2=4 or 5.
    if ((funct7 == Funct7Fp::FcvtSD || funct7 == Funct7Fp::FcvtDS) &&
        (u_rs2 == 4U || u_rs2 == 5U)) {
        const bool is_double = funct7 == Funct7Fp::FcvtDS;
        const bool raises_inexact = u_rs2 == 5U;
        const auto effective_rm = resolve_rounding_mode(rm, fcsr);
        FpEnvironment::clear_exceptions();

        const auto round_to_integral = [effective_rm](auto bits, unsigned fraction_bits,
                                                      unsigned exponent_bits, int bias) {
            using UInt = decltype(bits);
            constexpr unsigned kWidth = sizeof(UInt) * 8U;
            const UInt sign_mask = UInt{1} << (kWidth - 1U);
            const UInt exponent_mask = (UInt{1} << exponent_bits) - UInt{1};
            const UInt encoded_exponent = (bits >> fraction_bits) & exponent_mask;
            const bool negative = (bits & sign_mask) != 0;
            if (encoded_exponent == exponent_mask) return bits;

            const int exponent = static_cast<int>(encoded_exponent) - bias;
            if (exponent >= static_cast<int>(fraction_bits)) return bits;
            const UInt sign = bits & sign_mask;
            const UInt magnitude = bits & ~sign_mask;
            const auto should_increment = [effective_rm, negative](bool above_half,
                                                                   bool exactly_half, bool odd) {
                switch (effective_rm) {
                    case RoundingMode::Rdn:
                        return negative;
                    case RoundingMode::Rup:
                        return !negative;
                    case RoundingMode::Rmm:
                        return above_half || exactly_half;
                    case RoundingMode::Rne:
                        return above_half || (exactly_half && odd);
                    case RoundingMode::Rtz:
                        return false;
                    default:
                        return false;
                }
            };

            if (exponent < 0) {
                const UInt half = static_cast<UInt>(bias - 1) << fraction_bits;
                const bool above_half = magnitude > half;
                const bool exactly_half = magnitude == half;
                const bool increment = should_increment(above_half, exactly_half, false);
                return sign | (increment ? (static_cast<UInt>(bias) << fraction_bits) : UInt{0});
            }

            const unsigned discarded_bits = fraction_bits - static_cast<unsigned>(exponent);
            const UInt discarded_mask = (UInt{1} << discarded_bits) - UInt{1};
            const UInt discarded = magnitude & discarded_mask;
            if (discarded == 0) return bits;
            const UInt halfway = UInt{1} << (discarded_bits - 1U);
            const UInt truncated = magnitude & ~discarded_mask;
            const bool increment = should_increment(discarded > halfway, discarded == halfway,
                                                    ((truncated >> discarded_bits) & UInt{1}) != 0);
            return sign | (truncated + (increment ? (UInt{1} << discarded_bits) : UInt{0}));
        };

        if (is_double) {
            const double source = read_f64(freg, u_rs1);
            const uint64_t source_bits = f64_bits(source);
            constexpr uint64_t kF64EncodedExpMask = simrv::xlen::kF64ExpMask
                                                    << simrv::xlen::kF64ExpShift;
            const bool source_is_nan = (source_bits & kF64EncodedExpMask) == kF64EncodedExpMask &&
                                       (source_bits & simrv::xlen::kF64FracMask) != 0;
            if (source_is_nan) {
                if (is_snan64(source)) FpEnvironment::raise_invalid();
                out.fp_wb_data = simrv::xlen::kF64Qnan;
            } else {
                const uint64_t rounded_bits = round_to_integral(source_bits, 52U, 11U, 1023);
                out.fp_wb_data = rounded_bits;
                if (raises_inexact && rounded_bits != source_bits)
                    FpEnvironment::raise_flags(std::to_underlying(FflagsBit::Nx));
            }
        } else {
            const float source = read_f32(freg, u_rs1);
            const uint32_t source_bits = f32_bits(source);
            constexpr uint32_t kF32EncodedExpMask = simrv::xlen::kF32ExpMask
                                                    << simrv::xlen::kF32ExpShift;
            const bool source_is_nan = (source_bits & kF32EncodedExpMask) == kF32EncodedExpMask &&
                                       (source_bits & simrv::xlen::kF32FracMask) != 0;
            if (source_is_nan) {
                if (is_snan32(source)) FpEnvironment::raise_invalid();
                out.fp_wb_data = write_f32_boxed(f32_from_bits(simrv::xlen::kF32Qnan));
            } else {
                const uint32_t rounded_bits = round_to_integral(source_bits, 23U, 8U, 127);
                out.fp_wb_data = simrv::xlen::kF32BoxerBits | rounded_bits;
                if (raises_inexact && rounded_bits != source_bits)
                    FpEnvironment::raise_flags(std::to_underlying(FflagsBit::Nx));
            }
        }
        out.fp_wb_enable = true;
        FpEnvironment::accumulate_exceptions(fcsr);
        return out;
    }

    if (fp::fp_exec_add_sub(out, funct7, rm, u_rs1, u_rs2, freg, fcsr)) return out;
    if (fp::fp_exec_mul_div_sqrt(out, funct7, rm, u_rs1, u_rs2, freg, fcsr)) return out;
    if (fp::fp_exec_sgnj_minmax(out, funct7, funct3, u_rs1, u_rs2, freg, fcsr)) return out;
    if (fp::fp_exec_cvt(out, funct7, rm, u_rs1, u_rs2, rrs1, freg, fcsr)) return out;
    if (fp::fp_exec_cmp(out, funct7, funct3, u_rs1, u_rs2, freg, fcsr)) return out;
    if (fp::fp_exec_mv_class(out, funct7, funct3, u_rs1, u_rs2, rrs1, freg)) return out;

    return out;
}

}  // namespace simrv::execute
