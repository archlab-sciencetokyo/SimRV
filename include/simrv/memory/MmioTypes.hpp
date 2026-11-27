/**
 * @file MmioTypes.hpp
 * @brief Memory-Mapped I/O domain types and aliases.
 */
#pragma once

#include <cstdint>

#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::memory {

struct MmioOffsetTag {};
using MmioOffset = simrv::xlen::BasicAddress<MmioOffsetTag>;
static_assert(sizeof(MmioOffset) == sizeof(simrv::xlen::Word));

inline constexpr auto operator+(simrv::xlen::PhysAddr base, MmioOffset offset) noexcept
    -> simrv::xlen::PhysAddr {
    return simrv::xlen::PhysAddr{base.raw() + offset.raw()};
}
inline constexpr auto operator+(MmioOffset offset, simrv::xlen::PhysAddr base) noexcept
    -> simrv::xlen::PhysAddr {
    return simrv::xlen::PhysAddr{base.raw() + offset.raw()};
}
using MmioBytes = uint8_t;
using MmioValue = simrv::xlen::Word;
using MmioData8 = uint8_t;
using MmioData16 = uint16_t;
using MmioData32 = uint32_t;
using MmioData64 = uint64_t;

}  // namespace simrv::memory
