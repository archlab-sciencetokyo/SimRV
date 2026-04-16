/**
 * @file ExecuteUnitFloat.cpp
 * @brief SimRV floating-point execution unit implementation.
 */
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>

#include "ExecuteUnit.hpp"

namespace simrv::fp {

// ============================================================================
// Constants
// ============================================================================

namespace constants {
// IEEE 754 Single-Precision (32-bit) Constants
constexpr uint32_t F32_SIGN_BIT = 31u;
constexpr uint32_t F32_EXP_MASK = 0xffu;
constexpr uint32_t F32_EXP_SHIFT = 23u;
constexpr uint32_t F32_FRAC_MASK = 0x7fffffu;
constexpr uint32_t F32_QNAN = 0x7fc00000u;
constexpr FloatingRegister F32_BOXER_BITS = 0xffffffff00000000ull;
constexpr uint32_t F32_FRAC_QNAN_BIT = 22u;

// IEEE 754 Double-Precision (64-bit) Constants
constexpr uint64_t F64_SIGN_BIT = 63u;
constexpr uint64_t F64_EXP_MASK = 0x7ffu;
constexpr uint64_t F64_EXP_SHIFT = 52u;
constexpr uint64_t F64_FRAC_MASK = 0xfffffffffffffull;
constexpr uint64_t F64_QNAN = 0x7ff8000000000000ull;
constexpr uint64_t F64_FRAC_QNAN_BIT = 51u;

// RISC-V FP Flags
constexpr uint32_t FFLAGS_NX = 0x01;  // Inexact
constexpr uint32_t FFLAGS_UF = 0x02;  // Underflow
constexpr uint32_t FFLAGS_OF = 0x04;  // Overflow
constexpr uint32_t FFLAGS_DZ = 0x08;  // Divide by zero
constexpr uint32_t FFLAGS_NV = 0x10;  // Invalid operation
constexpr uint32_t FFLAGS_MASK = 0x1fu;

// Rounding Modes
constexpr Word RM_RNE = 0x0;  // Round to nearest, ties to even
constexpr Word RM_RTZ = 0x1;  // Round towards zero
constexpr Word RM_RDN = 0x2;  // Round down
constexpr Word RM_RUP = 0x3;  // Round up
constexpr Word RM_RMM = 0x4;  // Round to nearest, ties to max magnitude
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

uint32_t f32_bits(float v) { return std::bit_cast<uint32_t>(v); }
float f32_from_bits(uint32_t v) { return std::bit_cast<float>(v); }
uint64_t f64_bits(double v) { return std::bit_cast<uint64_t>(v); }
double f64_from_bits(uint64_t v) { return std::bit_cast<double>(v); }

// ============================================================================
// Register Access Helpers
// ============================================================================

float read_f32(const FloatingRegister* freg, Word idx) {
    return f32_from_bits(static_cast<uint32_t>(freg[idx] & 0xffffffffu));
}

double read_f64(const FloatingRegister* freg, Word idx) { return f64_from_bits(freg[idx]); }

FloatingRegister write_f32_boxed(float v) {
    return constants::F32_BOXER_BITS | static_cast<FloatingRegister>(f32_bits(v));
}

// ============================================================================
// Classification Functions
// ============================================================================

uint32_t fclass32(float v) {
    const uint32_t bits = f32_bits(v);
    const bool sign = (bits >> constants::F32_SIGN_BIT) != 0;
    const uint32_t exp = (bits >> constants::F32_EXP_SHIFT) & constants::F32_EXP_MASK;
    const uint32_t frac = bits & constants::F32_FRAC_MASK;

    if (exp == constants::F32_EXP_MASK) {
        if (frac == 0) return sign ? (1u << 0) : (1u << 7);
        const bool qnan = (frac & (1u << constants::F32_FRAC_QNAN_BIT)) != 0;
        return qnan ? (1u << 9) : (1u << 8);
    }
    if (exp == 0) {
        if (frac == 0) return sign ? (1u << 3) : (1u << 4);
        return sign ? (1u << 2) : (1u << 5);
    }
    return sign ? (1u << 1) : (1u << 6);
}

uint32_t fclass64(double v) {
    const uint64_t bits = f64_bits(v);
    const bool sign = (bits >> constants::F64_SIGN_BIT) != 0;
    const uint64_t exp = (bits >> constants::F64_EXP_SHIFT) & constants::F64_EXP_MASK;
    const uint64_t frac = bits & constants::F64_FRAC_MASK;

    if (exp == constants::F64_EXP_MASK) {
        if (frac == 0) return sign ? (1u << 0) : (1u << 7);
        const bool qnan = (frac & (1ull << constants::F64_FRAC_QNAN_BIT)) != 0;
        return qnan ? (1u << 9) : (1u << 8);
    }
    if (exp == 0) {
        if (frac == 0) return sign ? (1u << 3) : (1u << 4);
        return sign ? (1u << 2) : (1u << 5);
    }
    return sign ? (1u << 1) : (1u << 6);
}

// ============================================================================
// Signaling NaN Detection
// ============================================================================

bool is_snan32(float v) {
    const uint32_t bits = f32_bits(v);
    const uint32_t exp = (bits >> constants::F32_EXP_SHIFT) & constants::F32_EXP_MASK;
    const uint32_t frac = bits & constants::F32_FRAC_MASK;
    return (exp == constants::F32_EXP_MASK) && (frac != 0) &&
           ((frac & (1u << constants::F32_FRAC_QNAN_BIT)) == 0);
}

bool is_snan64(double v) {
    const uint64_t bits = f64_bits(v);
    const uint64_t exp = (bits >> constants::F64_EXP_SHIFT) & constants::F64_EXP_MASK;
    const uint64_t frac = bits & constants::F64_FRAC_MASK;
    return (exp == constants::F64_EXP_MASK) && (frac != 0) &&
           ((frac & (1ull << constants::F64_FRAC_QNAN_BIT)) == 0);
}

// ============================================================================
// Min/Max Functions
// ============================================================================

FloatingRegister fmin32_riscv(float a, float b) {
    const bool a_nan = std::isnan(a);
    const bool b_nan = std::isnan(b);
    if (is_snan32(a) || is_snan32(b)) std::feraiseexcept(FE_INVALID);
    if (a_nan && b_nan) return write_f32_boxed(f32_from_bits(constants::F32_QNAN));
    if (a_nan) return write_f32_boxed(b);
    if (b_nan) return write_f32_boxed(a);
    if (a == 0.0f && b == 0.0f) {
        const bool neg = std::signbit(a) || std::signbit(b);
        return write_f32_boxed(neg ? -0.0f : 0.0f);
    }
    return write_f32_boxed(std::fmin(a, b));
}

FloatingRegister fmax32_riscv(float a, float b) {
    const bool a_nan = std::isnan(a);
    const bool b_nan = std::isnan(b);
    if (is_snan32(a) || is_snan32(b)) std::feraiseexcept(FE_INVALID);
    if (a_nan && b_nan) return write_f32_boxed(f32_from_bits(constants::F32_QNAN));
    if (a_nan) return write_f32_boxed(b);
    if (b_nan) return write_f32_boxed(a);
    if (a == 0.0f && b == 0.0f) {
        const bool pos = !std::signbit(a) || !std::signbit(b);
        return write_f32_boxed(pos ? 0.0f : -0.0f);
    }
    return write_f32_boxed(std::fmax(a, b));
}

FloatingRegister fmin64_riscv(double a, double b) {
    const bool a_nan = std::isnan(a);
    const bool b_nan = std::isnan(b);
    if (is_snan64(a) || is_snan64(b)) std::feraiseexcept(FE_INVALID);
    if (a_nan && b_nan) return static_cast<FloatingRegister>(constants::F64_QNAN);
    if (a_nan) return f64_bits(b);
    if (b_nan) return f64_bits(a);
    if (a == 0.0 && b == 0.0) {
        const bool neg = std::signbit(a) || std::signbit(b);
        return f64_bits(neg ? -0.0 : 0.0);
    }
    return f64_bits(std::fmin(a, b));
}

FloatingRegister fmax64_riscv(double a, double b) {
    const bool a_nan = std::isnan(a);
    const bool b_nan = std::isnan(b);
    if (is_snan64(a) || is_snan64(b)) std::feraiseexcept(FE_INVALID);
    if (a_nan && b_nan) return static_cast<FloatingRegister>(constants::F64_QNAN);
    if (a_nan) return f64_bits(b);
    if (b_nan) return f64_bits(a);
    if (a == 0.0 && b == 0.0) {
        const bool pos = !std::signbit(a) || !std::signbit(b);
        return f64_bits(pos ? 0.0 : -0.0);
    }
    return f64_bits(std::fmax(a, b));
}

// ============================================================================
// Exception Handling
// ============================================================================

uint32_t host_except_to_fflags(int ex) {
    uint32_t flags = 0;
    if (ex & FE_INEXACT) flags |= constants::FFLAGS_NX;
    if (ex & FE_UNDERFLOW) flags |= constants::FFLAGS_UF;
    if (ex & FE_OVERFLOW) flags |= constants::FFLAGS_OF;
    if (ex & FE_DIVBYZERO) flags |= constants::FFLAGS_DZ;
    if (ex & FE_INVALID) flags |= constants::FFLAGS_NV;
    return flags;
}

void accumulate_fp_flags(CSRValue& fcsr) {
    const uint32_t old_flags = fcsr & constants::FFLAGS_MASK;
    const uint32_t new_flags = host_except_to_fflags(std::fetestexcept(FE_ALL_EXCEPT));
    fcsr = (fcsr & ~static_cast<CSRValue>(constants::FFLAGS_MASK)) |
           ((old_flags | new_flags) & constants::FFLAGS_MASK);
}

// ============================================================================
// Rounding Mode Management
// ============================================================================

int rm_to_fe_round(Word rm, CSRValue fcsr) {
    Word effective_rm = rm;
    if (effective_rm == constants::RM_DYN) effective_rm = (fcsr >> 5) & 0x7;

    switch (effective_rm) {
        case constants::RM_RNE:
            return FE_TONEAREST;
        case constants::RM_RTZ:
            return FE_TOWARDZERO;
        case constants::RM_RDN:
            return FE_DOWNWARD;
        case constants::RM_RUP:
            return FE_UPWARD;
        case constants::RM_RMM:
            return FE_TONEAREST;  // Host fallback for RMM.
        default:
            return FE_TONEAREST;
    }
}

class ScopedRoundingMode {
   public:
    ScopedRoundingMode(Word rm, CSRValue fcsr) {
        old_mode_ = std::fegetround();
        std::fesetround(rm_to_fe_round(rm, fcsr));
    }
    ~ScopedRoundingMode() { std::fesetround(old_mode_); }

   private:
    int old_mode_ = FE_TONEAREST;
};

// ============================================================================
// Floating-Point to Integer Conversion
// ============================================================================

Register fcvt_to_i32(double value, bool unsigned_mode) {
    if (std::isnan(value)) {
        std::feraiseexcept(FE_INVALID);
        return unsigned_mode ? std::numeric_limits<uint32_t>::max()
                             : static_cast<Register>(std::numeric_limits<int32_t>::max());
    }

    if (std::isinf(value)) {
        std::feraiseexcept(FE_INVALID);
        if (value < 0) {
            return unsigned_mode ? 0 : std::numeric_limits<int32_t>::min();
        } else {
            return unsigned_mode ? std::numeric_limits<uint32_t>::max()
                                 : std::numeric_limits<int32_t>::max();
        }
    }

    double rounded = value;
    int rmode = std::fegetround();

    // Apply explicit rounding
    if (rmode == FE_TONEAREST) {
        rounded = std::rint(value);
    } else if (rmode == FE_TOWARDZERO) {
        rounded = std::trunc(value);
    } else if (rmode == FE_DOWNWARD) {
        rounded = std::floor(value);
    } else if (rmode == FE_UPWARD) {
        rounded = std::ceil(value);
    } else {
        rounded = std::rint(value);
    }

    if (rounded != value) {
        std::feraiseexcept(FE_INEXACT);
    }

    if (!unsigned_mode) {
        constexpr double min_v = static_cast<double>(std::numeric_limits<int32_t>::min());
        constexpr double max_v = static_cast<double>(std::numeric_limits<int32_t>::max());
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
    constexpr double max_v = static_cast<double>(std::numeric_limits<uint32_t>::max());
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

FpExecResult ExecuteUnit::fusedFp(Opcode opcode, Word fmt, Word rs1, Word rs2, Word rs3, Word rm,
                                  const FloatingRegister* freg, CSRValue& fcsr) const {
    FpExecResult out;
    std::feclearexcept(FE_ALL_EXCEPT);
    {
        ScopedRoundingMode scope(rm, fcsr);
        if (fmt == 0) {
            const float a = read_f32(freg, rs1);
            const float b = read_f32(freg, rs2);
            const float c = read_f32(freg, rs3);
            float value = 0.0f;
            if (opcode == Opcode::MAdd) value = std::fma(a, b, c);
            if (opcode == Opcode::MSub) value = std::fma(a, b, -c);
            if (opcode == Opcode::NMAdd) value = -std::fma(a, b, c);
            if (opcode == Opcode::NMSub) value = -std::fma(a, b, -c);
            if (std::isnan(value)) value = f32_from_bits(F32_QNAN);
            out.fp_wb_data = write_f32_boxed(value);
            out.fp_wb_enable = true;
        } else if (fmt == 1) {
            const double a = read_f64(freg, rs1);
            const double b = read_f64(freg, rs2);
            const double c = read_f64(freg, rs3);
            double value = 0.0;
            if (opcode == Opcode::MAdd) value = std::fma(a, b, c);
            if (opcode == Opcode::MSub) value = std::fma(a, b, -c);
            if (opcode == Opcode::NMAdd) value = -std::fma(a, b, c);
            if (opcode == Opcode::NMSub) value = -std::fma(a, b, -c);
            if (std::isnan(value)) value = f64_from_bits(F64_QNAN);
            out.fp_wb_data = f64_bits(value);
            out.fp_wb_enable = true;
        }
    }
    accumulate_fp_flags(fcsr);
    return out;
}

FpExecResult ExecuteUnit::opFp(Word funct7, Word funct3, Word rs2_field, Word rs1, Word rs2,
                               Register rrs1, const FloatingRegister* freg, CSRValue& fcsr) const {
    FpExecResult out;
    const Word rm = funct3;

    if (funct7 == FUNCT7_FADD_S) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode scope(rm, fcsr);
            float result = read_f32(freg, rs1) + read_f32(freg, rs2);
            if (std::isnan(result)) result = f32_from_bits(F32_QNAN);
            out.fp_wb_data = write_f32_boxed(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FADD_D) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode scope(rm, fcsr);
            double result = read_f64(freg, rs1) + read_f64(freg, rs2);
            if (std::isnan(result)) result = f64_from_bits(F64_QNAN);
            out.fp_wb_data = f64_bits(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FSUB_S) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode scope(rm, fcsr);
            float result = read_f32(freg, rs1) - read_f32(freg, rs2);
            if (std::isnan(result)) result = f32_from_bits(F32_QNAN);
            out.fp_wb_data = write_f32_boxed(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FSUB_D) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode scope(rm, fcsr);
            double result = read_f64(freg, rs1) - read_f64(freg, rs2);
            if (std::isnan(result)) result = f64_from_bits(F64_QNAN);
            out.fp_wb_data = f64_bits(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FMUL_S) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode scope(rm, fcsr);
            float result = read_f32(freg, rs1) * read_f32(freg, rs2);
            if (std::isnan(result)) result = f32_from_bits(F32_QNAN);
            out.fp_wb_data = write_f32_boxed(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FMUL_D) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode scope(rm, fcsr);
            double result = read_f64(freg, rs1) * read_f64(freg, rs2);
            if (std::isnan(result)) result = f64_from_bits(F64_QNAN);
            out.fp_wb_data = f64_bits(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FDIV_S) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode scope(rm, fcsr);
            float result = read_f32(freg, rs1) / read_f32(freg, rs2);
            if (std::isnan(result)) result = f32_from_bits(F32_QNAN);
            out.fp_wb_data = write_f32_boxed(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FDIV_D) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode scope(rm, fcsr);
            double result = read_f64(freg, rs1) / read_f64(freg, rs2);
            if (std::isnan(result)) result = f64_from_bits(F64_QNAN);
            out.fp_wb_data = f64_bits(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FSQRT_S) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode scope(rm, fcsr);
            float result = std::sqrt(read_f32(freg, rs1));
            if (std::isnan(result)) result = f32_from_bits(F32_QNAN);
            out.fp_wb_data = write_f32_boxed(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FSQRT_D) {
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode scope(rm, fcsr);
            double result = std::sqrt(read_f64(freg, rs1));
            if (std::isnan(result)) result = f64_from_bits(F64_QNAN);
            out.fp_wb_data = f64_bits(result);
        }
        out.fp_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FSGNJ_S || funct7 == FUNCT7_FSGNJ_D) {
        const bool is_d = (funct7 == FUNCT7_FSGNJ_D);
        const FloatingRegister a = freg[rs1];
        const FloatingRegister b = freg[rs2];
        FloatingRegister value = 0;
        const uint64_t sign_bit = is_d ? (1ull << 63u) : (1ull << 31u);
        if (funct3 == FUNCT3_MIN) value = (a & (~sign_bit)) | (b & sign_bit);
        if (funct3 == FUNCT3_MAX) value = (a & (~sign_bit)) | ((~b) & sign_bit);
        if (funct3 == 2) value = a ^ (b & sign_bit);
        out.fp_wb_data = is_d ? value : (0xffffffff00000000ull | (value & 0xffffffffu));
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
            ScopedRoundingMode scope(rm, fcsr);
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
            ScopedRoundingMode scope(rm, fcsr);
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
                if (funct3 != FUNCT3_EQ || is_snan32(a) || is_snan32(b))
                    std::feraiseexcept(FE_INVALID);
                out.int_wb_data = 0;
            } else {
                if (funct3 == FUNCT3_EQ) out.int_wb_data = (a == b);
                if (funct3 == FUNCT3_LT) out.int_wb_data = (a < b);
                if (funct3 == FUNCT3_LEQ) out.int_wb_data = (a <= b);
            }
        } else {
            const double a = read_f64(freg, rs1);
            const double b = read_f64(freg, rs2);
            if (std::isnan(a) || std::isnan(b)) {
                if (funct3 != FUNCT3_EQ || is_snan64(a) || is_snan64(b))
                    std::feraiseexcept(FE_INVALID);
                out.int_wb_data = 0;
            } else {
                if (funct3 == FUNCT3_EQ) out.int_wb_data = (a == b);
                if (funct3 == FUNCT3_LT) out.int_wb_data = (a < b);
                if (funct3 == FUNCT3_LEQ) out.int_wb_data = (a <= b);
            }
        }
        out.int_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FCVT_W_S || funct7 == FUNCT7_FCVT_W_D) {
        const bool is_d = (funct7 == FUNCT7_FCVT_W_D);
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode scope(rm, fcsr);
            const double a = !is_d ? static_cast<double>(read_f32(freg, rs1)) : read_f64(freg, rs1);
            out.int_wb_data = fcvt_to_i32(a, rs2_field != 0);
        }
        out.int_wb_enable = true;
        accumulate_fp_flags(fcsr);
    } else if (funct7 == FUNCT7_FCVT_S_W || funct7 == FUNCT7_FCVT_D_W) {
        const int32_t i = static_cast<int32_t>(rrs1);
        const uint32_t u = static_cast<uint32_t>(rrs1);
        const bool is_d = (funct7 == FUNCT7_FCVT_D_W);
        std::feclearexcept(FE_ALL_EXCEPT);
        {
            ScopedRoundingMode scope(rm, fcsr);
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
            out.int_wb_data = static_cast<Register>(freg[rs1] & 0xffffffffu);
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
            out.fp_wb_data = 0xffffffff00000000ull | static_cast<FloatingRegister>(rrs1);
        }
        out.fp_wb_enable = true;
    }
    return out;
}
