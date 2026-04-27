/**
 * @file ExecuteUnitFloat.cpp
 * @brief SimRV floating-point execution unit implementation.
 */
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>

#include "Define.hpp"
#include "ExecuteUnit.hpp"
#include "XLen.hpp"

namespace simrv::fp {

// ============================================================================
// Constants
// ============================================================================

namespace constants {
// IEEE 754 Single-Precision (32-bit) Constants
constexpr uint32_t F32_SIGN_BIT = 31U;
constexpr uint32_t F32_EXP_MASK = 0xffU;
constexpr uint32_t F32_EXP_SHIFT = 23U;
constexpr uint32_t F32_FRAC_MASK = 0x7fffffU;
constexpr uint32_t F32_QNAN = 0x7fc00000U;
constexpr FloatingRegister F32_BOXER_BITS = 0xffffffff00000000ULL;
constexpr uint32_t F32_FRAC_QNAN_BIT = 22U;

// IEEE 754 Double-Precision (64-bit) Constants
constexpr uint64_t F64_SIGN_BIT = 63U;
constexpr uint64_t F64_EXP_MASK = 0x7ffU;
constexpr uint64_t F64_EXP_SHIFT = 52U;
constexpr uint64_t F64_FRAC_MASK = 0xfffffffffffffULL;
constexpr uint64_t F64_QNAN = 0x7ff8000000000000ULL;
constexpr uint64_t F64_FRAC_QNAN_BIT = 51U;

// RISC-V FP Flags
constexpr uint32_t FFLAGS_NX = 0x01;  // Inexact
constexpr uint32_t FFLAGS_UF = 0x02;  // Underflow
constexpr uint32_t FFLAGS_OF = 0x04;  // Overflow
constexpr uint32_t FFLAGS_DZ = 0x08;  // Divide by zero
constexpr uint32_t FFLAGS_NV = 0x10;  // Invalid operation
constexpr uint32_t FFLAGS_MASK = 0x1fU;

// Rounding Modes
constexpr Word RM_RNE = 0x0;  // Round to nearest, ties to even
constexpr Word RM_RTZ = 0x1;  // Round towards zero
constexpr Word RM_RDN = 0x2;  // Round down
constexpr Word RM_RUP = 0x3;  // Round up
constexpr Word RM_DYN = 0x7;  // Dynamic (use FCSR.frm)

// Funct7 encodings for OP_FP
constexpr Word FUNCT7_FADD_S = 0x00;
constexpr Word FUNCT7_FADD_D = 0x01;
constexpr Word FUNCT7_FSUB_S = 0x04;
constexpr Word FUNCT7_FSUB_D = 0x05;
constexpr Word FUNCT7_FMUL_S = 0x08;
constexpr Word FUNCT7_FMUL_D = 0x09;
constexpr Word FUNCT7_FDIV_S = 0x0c;
constexpr Word FUNCT7_FDIV_D = 0x0d;
constexpr Word FUNCT7_FSQRT_S = 0x2c;
constexpr Word FUNCT7_FSQRT_D = 0x2d;
constexpr Word FUNCT7_FSGNJ_S = 0x10;
constexpr Word FUNCT7_FSGNJ_D = 0x11;
constexpr Word FUNCT7_FMINMAX_S = 0x14;
constexpr Word FUNCT7_FMINMAX_D = 0x15;
constexpr Word FUNCT7_FCVT_S_D = 0x20;
constexpr Word FUNCT7_FCVT_D_S = 0x21;
constexpr Word FUNCT7_FCMP_S = 0x50;
constexpr Word FUNCT7_FCMP_D = 0x51;
constexpr Word FUNCT7_FCVT_W_S = 0x60;
constexpr Word FUNCT7_FCVT_W_D = 0x61;
constexpr Word FUNCT7_FCVT_S_W = 0x68;
constexpr Word FUNCT7_FCVT_D_W = 0x69;
constexpr Word FUNCT7_FMV_X_W = 0x70;
constexpr Word FUNCT7_FMV_X_D = 0x71;
constexpr Word FUNCT7_FMV_W_X = 0x78;
constexpr Word FUNCT7_FMV_D_X = 0x79;

// Funct3 encodings
constexpr Word FUNCT3_MIN = 0x0;
constexpr Word FUNCT3_MAX = 0x1;
constexpr Word FUNCT3_LEQ = 0x0;  // FCMP: LE
constexpr Word FUNCT3_LT = 0x1;   // FCMP: LT
constexpr Word FUNCT3_EQ = 0x2;   // FCMP: EQ
constexpr Word FUNCT3_FMV_X_W = 0x0;
constexpr Word FUNCT3_FCLASS = 0x1;

}  // namespace constants

// ============================================================================
// Bit Manipulation Helpers
// ============================================================================

static auto f32_bits(float v) -> uint32_t { return std::bit_cast<uint32_t>(v); }
static auto f32_from_bits(uint32_t v) -> float { return std::bit_cast<float>(v); }
static auto f64_bits(double v) -> uint64_t { return std::bit_cast<uint64_t>(v); }
static auto f64_from_bits(uint64_t v) -> double { return std::bit_cast<double>(v); }

// ============================================================================
// Register Access Helpers
// ============================================================================

static auto read_f32(const FloatingRegister* freg, Word idx) -> float {
    return f32_from_bits(static_cast<uint32_t>(freg[idx] & 0xffffffffU));
}

static auto read_f64(const FloatingRegister* freg, Word idx) -> double {
    return f64_from_bits(freg[idx]);
}

static auto write_f32_boxed(float v) -> FloatingRegister {
    return constants::F32_BOXER_BITS | static_cast<FloatingRegister>(f32_bits(v));
}

// ============================================================================
// Classification Functions
// ============================================================================

static auto fclass32(float v) -> uint32_t {
    const uint32_t bits = f32_bits(v);
    const bool sign = (bits >> constants::F32_SIGN_BIT) != 0;
    const uint32_t exp = (bits >> constants::F32_EXP_SHIFT) & constants::F32_EXP_MASK;
    const uint32_t frac = bits & constants::F32_FRAC_MASK;

    if (exp == constants::F32_EXP_MASK) {
        if (frac == 0) {
            return sign ? (1U << 0) : (1U << 7);
        }
        const bool qnan = (frac & (1U << constants::F32_FRAC_QNAN_BIT)) != 0;
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

static auto fclass64(double v) -> uint32_t {
    const uint64_t bits = f64_bits(v);
    const bool sign = (bits >> constants::F64_SIGN_BIT) != 0;
    const uint64_t exp = (bits >> constants::F64_EXP_SHIFT) & constants::F64_EXP_MASK;
    const uint64_t frac = bits & constants::F64_FRAC_MASK;

    if (exp == constants::F64_EXP_MASK) {
        if (frac == 0) {
            return sign ? (1U << 0) : (1U << 7);
        }
        const bool qnan = (frac & (1ULL << constants::F64_FRAC_QNAN_BIT)) != 0;
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

static auto is_snan32(float v) -> bool {
    const uint32_t bits = f32_bits(v);
    const uint32_t exp = (bits >> constants::F32_EXP_SHIFT) & constants::F32_EXP_MASK;
    const uint32_t frac = bits & constants::F32_FRAC_MASK;
    return (exp == constants::F32_EXP_MASK) && (frac != 0) &&
           ((frac & (1U << constants::F32_FRAC_QNAN_BIT)) == 0);
}

static auto is_snan64(double v) -> bool {
    const uint64_t bits = f64_bits(v);
    const uint64_t exp = (bits >> constants::F64_EXP_SHIFT) & constants::F64_EXP_MASK;
    const uint64_t frac = bits & constants::F64_FRAC_MASK;
    return (exp == constants::F64_EXP_MASK) && (frac != 0) &&
           ((frac & (1ULL << constants::F64_FRAC_QNAN_BIT)) == 0);
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
        return write_f32_boxed(f32_from_bits(constants::F32_QNAN));
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
        return write_f32_boxed(f32_from_bits(constants::F32_QNAN));
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
        return static_cast<FloatingRegister>(constants::F64_QNAN);
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
        return static_cast<FloatingRegister>(constants::F64_QNAN);
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
        flags |= constants::FFLAGS_NX;
    }
    if ((ex & FE_UNDERFLOW) != 0) {
        flags |= constants::FFLAGS_UF;
    }
    if ((ex & FE_OVERFLOW) != 0) {
        flags |= constants::FFLAGS_OF;
    }
    if ((ex & FE_DIVBYZERO) != 0) {
        flags |= constants::FFLAGS_DZ;
    }
    if ((ex & FE_INVALID) != 0) {
        flags |= constants::FFLAGS_NV;
    }
    return flags;
}

static void accumulate_fp_flags(CSRValue& fcsr) {
    const uint32_t old_flags = fcsr & constants::FFLAGS_MASK;
    const uint32_t new_flags = host_except_to_fflags(std::fetestexcept(FE_ALL_EXCEPT));
    fcsr = (fcsr & ~static_cast<CSRValue>(constants::FFLAGS_MASK)) |
           ((old_flags | new_flags) & constants::FFLAGS_MASK);
}

// ============================================================================
// Rounding Mode Management
// ============================================================================

static auto rm_to_fe_round(Word rm, CSRValue fcsr) -> int {
    Word effective_rm = rm;
    if (effective_rm == constants::RM_DYN) {
        effective_rm = (fcsr >> 5) & 0x7;
    }

    switch (effective_rm) {
        case constants::RM_RNE:
            return FE_TONEAREST;
        case constants::RM_RTZ:
            return FE_TOWARDZERO;
        case constants::RM_RDN:
            return FE_DOWNWARD;
        case constants::RM_RUP:
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

static auto fcvt_to_i32(double value, bool unsigned_mode) -> Register {
    if (std::isnan(value)) {
        std::feraiseexcept(FE_INVALID);
        return unsigned_mode ? std::numeric_limits<uint32_t>::max()
                             : static_cast<Register>(std::numeric_limits<int32_t>::max());
    }

    if (std::isinf(value)) {
        std::feraiseexcept(FE_INVALID);
        if (value < 0) {
            return unsigned_mode ? 0 : std::numeric_limits<int32_t>::min();
        }
        return unsigned_mode ? std::numeric_limits<uint32_t>::max()
                             : std::numeric_limits<int32_t>::max();
    }

    double rounded = std::rint(value);
    int const rmode = std::fegetround();

    // Apply explicit rounding
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

    if (!unsigned_mode) {
        constexpr auto min_v = static_cast<double>(std::numeric_limits<int32_t>::min());
        constexpr auto max_v = static_cast<double>(std::numeric_limits<int32_t>::max());
        if (rounded < min_v) {
            std::feraiseexcept(FE_INVALID);
            return static_cast<Register>(std::numeric_limits<int32_t>::min());
        }
        if (rounded > max_v) {
            std::feraiseexcept(FE_INVALID);
            return static_cast<Register>(std::numeric_limits<int32_t>::max());
        }
        return static_cast<Register>(static_cast<int32_t>(rounded));
    }

    constexpr double min_v = 0.0;
    constexpr auto max_v = static_cast<double>(std::numeric_limits<uint32_t>::max());
    if (rounded < min_v) {
        std::feraiseexcept(FE_INVALID);
        return 0;
    }
    if (rounded > max_v) {
        std::feraiseexcept(FE_INVALID);
        return static_cast<Register>(std::numeric_limits<uint32_t>::max());
    }
    return static_cast<Register>(static_cast<uint32_t>(rounded));
}

}  // namespace simrv::fp

// ============================================================================
// Public ExecuteUnit Methods
// ============================================================================

using namespace simrv::fp;
using namespace simrv::fp::constants;

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
                value = f32_from_bits(F32_QNAN);
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
                value = f64_from_bits(F64_QNAN);
            }
            out.fp_wb_data = f64_bits(value);
            out.fp_wb_enable = true;
        }
    }
    accumulate_fp_flags(fcsr);
    return out;
}

auto ExecuteUnit::opFp(Word funct7, Word funct3, Word rs2_field, Word rs1, Word rs2, Register rrs1,
                       const FloatingRegister* freg, CSRValue& fcsr) -> FpExecResult {
    FpExecResult out;
    const Word rm = funct3;

    if (funct7 == FUNCT7_FADD_S) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            float result = read_f32(freg, rs1) + read_f32(freg, rs2);
            if (std::isnan(result)) {
                result = f32_from_bits(F32_QNAN);
            }
            out.fp_wb_data = write_f32_boxed(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FADD_D) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            double result = read_f64(freg, rs1) + read_f64(freg, rs2);
            if (std::isnan(result)) {
                result = f64_from_bits(F64_QNAN);
            }
            out.fp_wb_data = f64_bits(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FSUB_S) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            float result = read_f32(freg, rs1) - read_f32(freg, rs2);
            if (std::isnan(result)) {
                result = f32_from_bits(F32_QNAN);
            }
            out.fp_wb_data = write_f32_boxed(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FSUB_D) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            double result = read_f64(freg, rs1) - read_f64(freg, rs2);
            if (std::isnan(result)) {
                result = f64_from_bits(F64_QNAN);
            }
            out.fp_wb_data = f64_bits(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FMUL_S) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            float result = read_f32(freg, rs1) * read_f32(freg, rs2);
            if (std::isnan(result)) {
                result = f32_from_bits(F32_QNAN);
            }
            out.fp_wb_data = write_f32_boxed(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FMUL_D) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            double result = read_f64(freg, rs1) * read_f64(freg, rs2);
            if (std::isnan(result)) {
                result = f64_from_bits(F64_QNAN);
            }
            out.fp_wb_data = f64_bits(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FDIV_S) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            float result = read_f32(freg, rs1) / read_f32(freg, rs2);
            if (std::isnan(result)) {
                result = f32_from_bits(F32_QNAN);
            }
            out.fp_wb_data = write_f32_boxed(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FDIV_D) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            double result = read_f64(freg, rs1) / read_f64(freg, rs2);
            if (std::isnan(result)) {
                result = f64_from_bits(F64_QNAN);
            }
            out.fp_wb_data = f64_bits(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FSQRT_S) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            float result = std::sqrt(read_f32(freg, rs1));
            if (std::isnan(result)) {
                result = f32_from_bits(F32_QNAN);
            }
            out.fp_wb_data = write_f32_boxed(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FSQRT_D) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            double result = std::sqrt(read_f64(freg, rs1));
            if (std::isnan(result)) {
                result = f64_from_bits(F64_QNAN);
            }
            out.fp_wb_data = f64_bits(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FSGNJ_S || funct7 == FUNCT7_FSGNJ_D) {
        const bool is_d = (funct7 == FUNCT7_FSGNJ_D);
        const FloatingRegister a = freg[rs1];
        const FloatingRegister b = freg[rs2];
        FloatingRegister value = 0;
        const uint64_t sign_bit = is_d ? (1ULL << 63U) : (1ULL << 31U);
        if (funct3 == FUNCT3_MIN) {
            value = (a & (~sign_bit)) | (b & sign_bit);
        }
        if (funct3 == FUNCT3_MAX) {
            value = (a & (~sign_bit)) | ((~b) & sign_bit);
        }
        if (funct3 == 2) {
            value = a ^ (b & sign_bit);
        }
        out.fp_wb_data = is_d ? value : (0xffffffff00000000ULL | (value & 0xffffffffU));
        out.fp_wb_enable = true;
    } else if (funct7 == FUNCT7_FMINMAX_S) {
        std::feclearexcept(FE_ALL_EXCEPT);
        const float a = read_f32(freg, rs1);
        const float b = read_f32(freg, rs2);
        out.fp_wb_data = (funct3 == FUNCT3_MAX) ? fmax32_riscv(a, b) : fmin32_riscv(a, b);
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FMINMAX_D) {
        std::feclearexcept(FE_ALL_EXCEPT);
        const double a = read_f64(freg, rs1);
        const double b = read_f64(freg, rs2);
        out.fp_wb_data = (funct3 == FUNCT3_MAX) ? fmax64_riscv(a, b) : fmin64_riscv(a, b);
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FCVT_S_D) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            const double src = read_f64(freg, rs1);
            if (std::isnan(src)) {
                out.fp_wb_data = write_f32_boxed(f32_from_bits(F32_QNAN));
            } else {
                out.fp_wb_data = write_f32_boxed(static_cast<float>(src));
            }
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FCVT_D_S) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            const float src = read_f32(freg, rs1);
            if (std::isnan(src)) {
                out.fp_wb_data = static_cast<FloatingRegister>(F64_QNAN);
            } else {
                out.fp_wb_data = f64_bits(static_cast<double>(src));
            }
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FCMP_S || funct7 == FUNCT7_FCMP_D) {
        const bool is_d = (funct7 == FUNCT7_FCMP_D);
        std::feclearexcept(FE_ALL_EXCEPT);
        if (!is_d) {
            const float a = read_f32(freg, rs1);
            const float b = read_f32(freg, rs2);
            if (std::isnan(a) || std::isnan(b)) {
                if (funct3 != FUNCT3_EQ || is_snan32(a) || is_snan32(b)) {
                    std::feraiseexcept(FE_INVALID);
                }
                out.int_wb_data = 0;
            } else {
                if (funct3 == FUNCT3_EQ) {
                    out.int_wb_data = static_cast<Register>(a == b);
                }
                if (funct3 == FUNCT3_LT) {
                    out.int_wb_data = static_cast<Register>(a < b);
                }
                if (funct3 == FUNCT3_LEQ) {
                    out.int_wb_data = static_cast<Register>(a <= b);
                }
            }
        } else {
            const double a = read_f64(freg, rs1);
            const double b = read_f64(freg, rs2);
            if (std::isnan(a) || std::isnan(b)) {
                if (funct3 != FUNCT3_EQ || is_snan64(a) || is_snan64(b)) {
                    std::feraiseexcept(FE_INVALID);
                }
                out.int_wb_data = 0;
            } else {
                if (funct3 == FUNCT3_EQ) {
                    out.int_wb_data = static_cast<Register>(a == b);
                }
                if (funct3 == FUNCT3_LT) {
                    out.int_wb_data = static_cast<Register>(a < b);
                }
                if (funct3 == FUNCT3_LEQ) {
                    out.int_wb_data = static_cast<Register>(a <= b);
                }
            }
        }
        out.int_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FCVT_W_S || funct7 == FUNCT7_FCVT_W_D) {
        const bool is_d = (funct7 == FUNCT7_FCVT_W_D);
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            const double a = !is_d ? static_cast<double>(read_f32(freg, rs1)) : read_f64(freg, rs1);
            out.int_wb_data = fcvt_to_i32(a, rs2_field != 0);
        }
        out.int_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FCVT_S_W || funct7 == FUNCT7_FCVT_D_W) {
        const auto i = static_cast<int32_t>(rrs1);
        const auto u = static_cast<uint32_t>(rrs1);
        const bool is_d = (funct7 == FUNCT7_FCVT_D_W);
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode const scope(rm, fcsr);
            if (is_d) {
                out.fp_wb_data = (rs2_field == 0) ? f64_bits(static_cast<double>(i))
                                                  : f64_bits(static_cast<double>(u));
            } else {
                out.fp_wb_data = (rs2_field == 0) ? write_f32_boxed(static_cast<float>(i))
                                                  : write_f32_boxed(static_cast<float>(u));
            }
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FMV_X_W || funct7 == FUNCT7_FMV_X_D) {
        const bool is_d = (funct7 == FUNCT7_FMV_X_D);
        if (funct3 == FUNCT3_FMV_X_W) {
            out.int_wb_data = static_cast<Register>(freg[rs1] & 0xffffffffU);
            out.int_wb_enable = true;
        } else if (funct3 == FUNCT3_FCLASS) {
            out.int_wb_data = is_d ? fclass64(read_f64(freg, rs1)) : fclass32(read_f32(freg, rs1));
            out.int_wb_enable = true;
        }
    } else if (funct7 == FUNCT7_FMV_W_X || funct7 == FUNCT7_FMV_D_X) {
        const bool is_d = (funct7 == FUNCT7_FMV_D_X);
        if (is_d) {
            out.fp_wb_data = f64_bits(static_cast<double>(rrs1));
        } else {
            out.fp_wb_data = 0xffffffff00000000ULL | static_cast<FloatingRegister>(rrs1);
        }
        out.fp_wb_enable = true;
    }
    return out;
}
