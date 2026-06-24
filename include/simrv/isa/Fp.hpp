/**
 * @file Fp.hpp
 * @brief RISC-V Floating-Point (F/D) extensions definitions.
 */
#pragma once

#include <cstdint>
#include "simrv/xlen/Types.hpp"

namespace simrv::isa {

constexpr size_t FLEN = 64;

enum class Funct7Fp : Word {
    FaddS = 0x00,
    FaddD = 0x01,
    FsubS = 0x04,
    FsubD = 0x05,
    FmulS = 0x08,
    FmulD = 0x09,
    FdivS = 0x0c,
    FdivD = 0x0d,
    FsqrtS = 0x2c,
    FsqrtD = 0x2d,
    FsgnjS = 0x10,
    FsgnjD = 0x11,
    FminmaxS = 0x14,
    FminmaxD = 0x15,
    FcvtSD = 0x20,
    FcvtDS = 0x21,
    FcmpS = 0x50,
    FcmpD = 0x51,
    FcvtWS = 0x60,
    FcvtWD = 0x61,
    FcvtSW = 0x68,
    FcvtDW = 0x69,
    FmvXW = 0x70,
    FmvXD = 0x71,
    FmvWX = 0x78,
    FmvDX = 0x79,
};

enum class Funct3Fp : Word {
    Min = 0x0,
    Max = 0x1,
    Leq = 0x0,
    Lt = 0x1,
    Eq = 0x2,
    FmvXW = 0x0,
    Fclass = 0x1,
};

enum class FflagsBit : uint32_t {
    Nx = 0x01,
    Uf = 0x02,
    Of = 0x04,
    Dz = 0x08,
    Nv = 0x10,
};

enum class RoundingMode : Word {
    Rne = 0x0,
    Rtz = 0x1,
    Rdn = 0x2,
    Rup = 0x3,
    Rmm = 0x4,
    Dyn = 0x7,
};

} // namespace simrv::isa
