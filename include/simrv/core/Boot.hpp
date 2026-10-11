/**
 * @file Boot.hpp
 * @brief Boot and initialization address constants.
 */
#pragma once

#include "simrv/xlen/Types.hpp"

namespace simrv::boot {
inline constexpr simrv::xlen::Address kStartPc = static_cast<simrv::xlen::Address>(0x80000000u);
inline constexpr simrv::xlen::Address kInitDataAddress =
    static_cast<simrv::xlen::Address>(0x01000000u);
}  // namespace simrv::boot
