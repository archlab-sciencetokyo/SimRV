/**
 * @file Fp.hpp
 * @brief RISC-V Floating-Point (F/D) extensions definitions.
 */
#pragma once

#include <cstdint>

#include "simrv/isa/Base.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::isa {

constexpr size_t FLEN = 64;

/**
 * @enum Funct7Fp
 * @brief Represents the 7-bit function selector for floating-point instructions (bits [31:25]).
 */
enum class Funct7Fp : Word {
    FaddS = 0x00,     ///< Single-precision floating-point Add
    FaddD = 0x01,     ///< Double-precision floating-point Add
    FsubS = 0x04,     ///< Single-precision floating-point Subtract
    FsubD = 0x05,     ///< Double-precision floating-point Subtract
    FmulS = 0x08,     ///< Single-precision floating-point Multiply
    FmulD = 0x09,     ///< Double-precision floating-point Multiply
    FdivS = 0x0c,     ///< Single-precision floating-point Divide
    FdivD = 0x0d,     ///< Double-precision floating-point Divide
    FsqrtS = 0x2c,    ///< Single-precision floating-point Square Root
    FsqrtD = 0x2d,    ///< Double-precision floating-point Square Root
    FsgnjS = 0x10,    ///< Single-precision Sign Injection (FSGNJ, FSGNJN, FSGNJX)
    FsgnjD = 0x11,    ///< Double-precision Sign Injection (FSGNJ, FSGNJN, FSGNJX)
    FminmaxS = 0x14,  ///< Single-precision Minimum/Maximum (FMIN, FMAX)
    FminmaxD = 0x15,  ///< Double-precision Minimum/Maximum (FMIN, FMAX)
    FcvtSD = 0x20,    ///< Convert Single-to-Double (FCVT.S.D)
    FcvtDS = 0x21,    ///< Convert Double-to-Single (FCVT.D.S)
    FcmpS = 0x50,     ///< Single-precision floating-point Compare (FEQ.S, FLT.S, FLE.S)
    FcmpD = 0x51,     ///< Double-precision floating-point Compare (FEQ.D, FLT.D, FLE.D)
    FcvtWS = 0x60,    ///< Convert Single to Integer Word/Doubleword (FCVT.W.S, FCVT.WU.S, FCVT.L.S,
                      ///< FCVT.LU.S)
    FcvtWD = 0x61,    ///< Convert Double to Integer Word/Doubleword (FCVT.W.D, FCVT.WU.D, FCVT.L.D,
                      ///< FCVT.LU.D)
    FcvtSW = 0x68,    ///< Convert Integer Word/Doubleword to Single (FCVT.S.W, FCVT.S.WU, FCVT.S.L,
                      ///< FCVT.S.LU)
    FcvtDW = 0x69,    ///< Convert Integer Word/Doubleword to Double (FCVT.D.W, FCVT.D.WU, FCVT.D.L,
                      ///< FCVT.D.LU)
    FmvXW = 0x70,     ///< Move Float Word to Integer Register (FMV.X.W) or FCLASS.S
    FmvXD = 0x71,     ///< Move Float Double to Integer Register (FMV.X.D) or FCLASS.D
    FmvWX = 0x78,     ///< Move Integer Register to Float Word (FMV.W.X)
    FmvDX = 0x79,     ///< Move Integer Register to Float Double (FMV.D.X)
};

constexpr auto operator==(Word raw, Funct7Fp f7) noexcept -> bool {
    return raw == std::to_underlying(f7);
}
constexpr auto operator==(Funct7Fp f7, Word raw) noexcept -> bool {
    return std::to_underlying(f7) == raw;
}
constexpr auto operator!=(Word raw, Funct7Fp f7) noexcept -> bool { return !(raw == f7); }
constexpr auto operator!=(Funct7Fp f7, Word raw) noexcept -> bool { return !(f7 == raw); }

/**
 * @enum Funct3Fp
 * @brief Represents standard floating-point funct3 sub-opcodes.
 */
enum class Funct3Fp : Word {
    Min = 0x0,     ///< Select Minimum (for Fminmax)
    Max = 0x1,     ///< Select Maximum (for Fminmax)
    Leq = 0x0,     ///< Less than or Equal comparison (for Fcmp)
    Lt = 0x1,      ///< Less than comparison (for Fcmp)
    Eq = 0x2,      ///< Equal comparison (for Fcmp)
    Sgnj = 0x0,    ///< Sign injection (for Fsgnj)
    Sgnjn = 0x1,   ///< Negated sign injection (for Fsgnjn)
    Sgnjx = 0x2,   ///< Xor sign injection (for Fsgnjx)
    FmvXW = 0x0,   ///< Move instruction indicator (for FmvXW/FmvXD)
    Fclass = 0x1,  ///< Classify instruction indicator (for FmvXW/FmvXD)
};

constexpr auto operator==(Funct3 f3, Funct3Fp fp) noexcept -> bool {
    return std::to_underlying(f3) == std::to_underlying(fp);
}
constexpr auto operator==(Funct3Fp fp, Funct3 f3) noexcept -> bool {
    return std::to_underlying(fp) == std::to_underlying(f3);
}
constexpr auto operator!=(Funct3 f3, Funct3Fp fp) noexcept -> bool { return !(f3 == fp); }
constexpr auto operator!=(Funct3Fp fp, Funct3 f3) noexcept -> bool { return !(fp == f3); }

/**
 * @enum FflagsBit
 * @brief IEEE-754 floating-point cumulative exception flags mapped to CSR bits.
 */
enum class FflagsBit : uint32_t {
    Nx = 0x01,  ///< Inexact flag
    Uf = 0x02,  ///< Underflow flag
    Of = 0x04,  ///< Overflow flag
    Dz = 0x08,  ///< Divide by Zero flag
    Nv = 0x10,  ///< Invalid Operation flag
};

constexpr auto operator|(FflagsBit lhs, FflagsBit rhs) noexcept -> uint32_t {
    return std::to_underlying(lhs) | std::to_underlying(rhs);
}
constexpr auto operator&(FflagsBit lhs, FflagsBit rhs) noexcept -> uint32_t {
    return std::to_underlying(lhs) & std::to_underlying(rhs);
}
constexpr auto operator~(FflagsBit bit) noexcept -> uint32_t { return ~std::to_underlying(bit); }
template <std::integral I>
constexpr auto operator|(I lhs, FflagsBit rhs) noexcept -> I {
    return lhs | static_cast<I>(std::to_underlying(rhs));
}
template <std::integral I>
constexpr auto operator|(FflagsBit lhs, I rhs) noexcept -> I {
    return static_cast<I>(std::to_underlying(lhs)) | rhs;
}
template <std::integral I>
constexpr auto operator&(I lhs, FflagsBit rhs) noexcept -> I {
    return lhs & static_cast<I>(std::to_underlying(rhs));
}
template <std::integral I>
constexpr auto operator&(FflagsBit lhs, I rhs) noexcept -> I {
    return static_cast<I>(std::to_underlying(lhs)) & rhs;
}
template <std::integral I>
constexpr auto operator|=(I& lhs, FflagsBit rhs) noexcept -> I& {
    lhs |= static_cast<I>(std::to_underlying(rhs));
    return lhs;
}
template <std::integral I>
constexpr auto operator&=(I& lhs, FflagsBit rhs) noexcept -> I& {
    lhs &= static_cast<I>(std::to_underlying(rhs));
    return lhs;
}

/**
 * @enum RoundingMode
 * @brief RISC-V floating-point rounding modes.
 */
enum class RoundingMode : Word {
    Rne = 0x0,  ///< Round to Nearest, ties to Even
    Rtz = 0x1,  ///< Round towards Zero
    Rdn = 0x2,  ///< Round down (towards negative infinity)
    Rup = 0x3,  ///< Round up (towards positive infinity)
    Rmm = 0x4,  ///< Round to Nearest, ties to Max Magnitude
    Dyn = 0x7,  ///< Dynamic Rounding Mode (defined in fcsr.frm field)
};

constexpr auto operator==(Word raw, RoundingMode rm) noexcept -> bool {
    return raw == std::to_underlying(rm);
}
constexpr auto operator==(RoundingMode rm, Word raw) noexcept -> bool {
    return std::to_underlying(rm) == raw;
}
constexpr auto operator!=(Word raw, RoundingMode rm) noexcept -> bool { return !(raw == rm); }
constexpr auto operator!=(RoundingMode rm, Word raw) noexcept -> bool { return !(rm == raw); }

constexpr auto operator==(Funct3 f3, RoundingMode rm) noexcept -> bool {
    return std::to_underlying(f3) == std::to_underlying(rm);
}
constexpr auto operator==(RoundingMode rm, Funct3 f3) noexcept -> bool {
    return std::to_underlying(rm) == std::to_underlying(f3);
}
constexpr auto operator!=(Funct3 f3, RoundingMode rm) noexcept -> bool { return !(f3 == rm); }
constexpr auto operator!=(RoundingMode rm, Funct3 f3) noexcept -> bool { return !(rm == f3); }

/// Left-shift operator for RoundingMode (e.g. RoundingMode::Rmm << 5U) to format FCSR bits.
constexpr auto operator<<(RoundingMode rm, unsigned shift) noexcept -> CSRValue {
    return static_cast<CSRValue>(std::to_underlying(rm)) << shift;
}

[[nodiscard]] constexpr auto fcsr_with_rounding_mode(CSRValue fcsr, RoundingMode rm) noexcept
    -> CSRValue {
    return (fcsr & ~static_cast<CSRValue>(0x7U << 5U)) | (rm << 5U);
}

[[nodiscard]] constexpr auto frm_rounding_mode(CSRValue fcsr) noexcept -> RoundingMode {
    return static_cast<RoundingMode>((fcsr >> 5U) & 0x7U);
}

[[nodiscard]] constexpr auto resolve_rounding_mode(Word rm_field, CSRValue fcsr) noexcept
    -> RoundingMode {
    if (rm_field == RoundingMode::Dyn) {
        return frm_rounding_mode(fcsr);
    }
    return static_cast<RoundingMode>(rm_field);
}

[[nodiscard]] constexpr auto resolve_rounding_mode(Funct3 rm_field, CSRValue fcsr) noexcept
    -> RoundingMode {
    return resolve_rounding_mode(std::to_underlying(rm_field), fcsr);
}

[[nodiscard]] constexpr auto is_valid_rounding_mode(RoundingMode rm) noexcept -> bool {
    return std::to_underlying(rm) <= 4;
}

}  // namespace simrv::isa
