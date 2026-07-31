/**
 * @file Fp.hpp
 * @brief RISC-V Floating-Point (F/D) extensions definitions.
 */
#pragma once

#include <cstdint>

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
    FmvXW = 0x0,   ///< Move instruction indicator (for FmvXW/FmvXD)
    Fclass = 0x1,  ///< Classify instruction indicator (for FmvXW/FmvXD)
};

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

}  // namespace simrv::isa
