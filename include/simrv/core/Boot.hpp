/**
 * @file Boot.hpp
 * @brief Boot and initialization address constants.
 */
#pragma once

#include "simrv/XLen.hpp"

namespace simrv::boot {
inline constexpr Address kStartPc = static_cast<Address>(0x80000000u);
inline constexpr Address kInitDataAddress = static_cast<Address>(0x01000000u);
}  // namespace simrv::boot
