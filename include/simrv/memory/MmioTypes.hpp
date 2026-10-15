/**
 * @file MmioTypes.hpp
 * @brief Memory-Mapped I/O domain types and aliases.
 */
#pragma once

#include <cstdint>

#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::memory {

using MmioOffset = simrv::xlen::Address;
using MmioBytes = uint8_t;
using MmioValue = simrv::xlen::Word;
using MmioData8 = uint8_t;
using MmioData16 = uint16_t;
using MmioData32 = uint32_t;
using MmioData64 = uint64_t;

}  // namespace simrv::memory
