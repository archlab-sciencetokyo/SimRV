/**
 * @file Memory.hpp
 * @brief Memory layout, DRAM, TLB, and page table constants.
 */
#pragma once

#include "simrv/XLen.hpp"

namespace simrv::memory {
inline constexpr Address kDramBaseAddress = static_cast<Address>(0x80000000u);
inline constexpr Address kDramSize = static_cast<Address>(64u * 1024u * 1024u);
inline constexpr Address kDramMask = static_cast<Address>(0x03ffffffu);
inline constexpr unsigned kPageShift = 12;
inline constexpr Address kPageMask = static_cast<Address>(0x00000fffu);
inline constexpr uint32_t kTlbSize = 4;
inline constexpr size_t kLocalCoreMemorySize = (static_cast<size_t>(32u * 1024u));
}  // namespace simrv::memory
