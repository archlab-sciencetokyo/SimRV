/**
 * @file Boot.hpp
 * @brief Boot and initialization address constants.
 */
#pragma once

#include "simrv/core/PlatformMap.h"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::boot {
inline constexpr simrv::xlen::Address kStartPc =
    static_cast<simrv::xlen::Address>(SIMRV_PLATFORM_DRAM_BASE);
inline constexpr simrv::xlen::Address kInitDataAddress =
    static_cast<simrv::xlen::Address>(0x01000000u);
}  // namespace simrv::boot
